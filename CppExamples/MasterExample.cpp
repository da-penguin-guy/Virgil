#include "VirgilNet.hpp"
#include "mdns/mdns.h"
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <set>
#include <map>

// This function will be called with received data and sender info
using PacketHandler = function<void(const string&, const sockaddr_in&)>;

void MDNSWorkerThread();
void StartMDNSWorker(const string& danteName, const string& function, const string& multicastBase = "");
void StopMDNSWorker();
void NetListener(int port, const vector<string> multicast_ips, PacketHandler handler);
void StartNetListener(int port, const vector<string> multicast_ips, PacketHandler handler);
void StopNetListener();
void SendParameterRequest(json& device);
void ProcessPacket(const string& data, const sockaddr_in& src);
void CleanupAndExit(int code);
void SetupWindow();


// Consolidated mDNS worker: advertises and scans in one thread
struct MDNSWorkerConfig {
    string danteName;
    string function;
    string multicastBase;
};
atomic<bool> mdns_worker_running{false};
thread mdns_worker_thread;
MDNSWorkerConfig mdns_worker_config;

mutex danteLookup_mutex;

atomic<bool> net_listener_running{false};
thread net_listener_thread;

mutex multicast_mutex;
set<string> current_multicast_groups;
socket_t netlistener_sock = INVALID_SOCKET;

// This function will be called with received data and sender info
using PacketHandler = function<void(const string&, const sockaddr_in&)>;

int virgilPort = 7889;
//Here, we have a preexisting list of Dante devices we are subscribed to
//You would get this from your preexisting Dante code
//I'm just making up a format to make my life easier
map<string,json> danteLookup = {{"ExampleSlave",{}}}; // Preexisting Dante device list; add more as needed


// Callback for mDNS query responses
int mdns_query_callback(int sock, const struct sockaddr* from, size_t addrlen,
                        mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                        uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                        size_t name_offset, size_t name_length, size_t record_offset,
                        size_t record_length, void* user_data) {
    char addrbuffer[64];
    char namebuffer[256];
    char sendbuffer[1024];
    
    // Track TTL for offline detection
    int record_ttl = (int)ttl;
    if (rtype == MDNS_RECORDTYPE_PTR) {
        mdns_string_t name = mdns_record_parse_ptr(data, size, record_offset, record_length,
                                                   namebuffer, sizeof(namebuffer));
        
        // Check if this is a Virgil service
        if (name.length > 0) {
            string service_name(name.str, name.length);
            if (service_name.find("._virgil._udp.local.") != string::npos) {
                size_t pos = service_name.find("._virgil._udp.local.");
                if (pos != string::npos) {
                    string dante_name = service_name.substr(0, pos);
                    // Goodbye packet: TTL==0 means device is offline
                    if (record_ttl == 0) {
                        lock_guard<mutex> lock(danteLookup_mutex);
                        if (danteLookup.find(dante_name) != danteLookup.end()) {
                            auto& device = danteLookup[dante_name];
                            if (device.value("isFound", false)) {
                                cout << "Device offline (goodbye): " << dante_name << endl;
                                device["isFound"] = false;
                            }
                        }
                    } else {
                        // Query for SRV and TXT records for this service
                        mdns_query_send(sock, MDNS_RECORDTYPE_SRV, service_name.c_str(), service_name.length(),
                                        sendbuffer, sizeof(sendbuffer), 0);
                        mdns_query_send(sock, MDNS_RECORDTYPE_TXT, service_name.c_str(), service_name.length(),
                                        sendbuffer, sizeof(sendbuffer), 0);
                    }
                }
            }
        }
    }
    else if (rtype == MDNS_RECORDTYPE_SRV) {
        mdns_record_srv_t srv = mdns_record_parse_srv(data, size, record_offset, record_length,
                                                      namebuffer, sizeof(namebuffer));
        
        // Get the service name from the query
        mdns_string_t name = mdns_string_extract(data, size, &name_offset, namebuffer, sizeof(namebuffer));
        if (name.length > 0) {
            string service_name(name.str, name.length);
            if (service_name.find("._virgil._udp.local.") != string::npos) {
                size_t pos = service_name.find("._virgil._udp.local.");
                if (pos != string::npos) {
                    string dante_name = service_name.substr(0, pos);
                    // Goodbye packet: TTL==0 means device is offline
                    if (record_ttl == 0) {
                        lock_guard<mutex> lock(danteLookup_mutex);
                        if (danteLookup.find(dante_name) != danteLookup.end()) {
                            auto& device = danteLookup[dante_name];
                            if (device.value("isFound", false)) {
                                cout << "Device offline (goodbye): " << dante_name << endl;
                                device["isFound"] = false;
                            }
                        }
                    } else {
                        lock_guard<mutex> lock(danteLookup_mutex);
                        if (danteLookup.find(dante_name) != danteLookup.end()) {
                            auto& device = danteLookup[dante_name];
                            device["name"] = dante_name;
                            device["port"] = srv.port;
                            device["hostname"] = string(srv.name.str, srv.name.length);
                            inet_ntop(AF_INET, &((sockaddr_in*)from)->sin_addr, addrbuffer, sizeof(addrbuffer));
                            device["ip"] = string(addrbuffer);
                            if (!device.value("isFound", false)) {
                                cout << "Device found via mDNS: " << dante_name << " at " << addrbuffer << ":" << srv.port << endl;
                            }
                            // Store TTL for offline detection
                            device["mdnsTTL"] = record_ttl;
                            device["mdnsLastSeen"] = (int)time(nullptr);
                        }
                    }
                }
            }
        }
    }
    else if (rtype == MDNS_RECORDTYPE_TXT) {
        // Parse TXT records to get device info
        mdns_string_t name = mdns_string_extract(data, size, &name_offset, namebuffer, sizeof(namebuffer));
        if (name.length > 0) {
            string service_name(name.str, name.length);
            if (service_name.find("._virgil._udp.local.") != string::npos) {
                size_t pos = service_name.find("._virgil._udp.local.");
                if (pos != string::npos) {
                    string dante_name = service_name.substr(0, pos);
                    // Goodbye packet: TTL==0 means device is offline
                    if (record_ttl == 0) {
                        lock_guard<mutex> lock(danteLookup_mutex);
                        if (danteLookup.find(dante_name) != danteLookup.end()) {
                            auto& device = danteLookup[dante_name];
                            if (device.value("isFound", false)) {
                                cout << "Device offline (goodbye): " << dante_name << endl;
                                device["isFound"] = false;
                            }
                        }
                    } else {
                        lock_guard<mutex> lock(danteLookup_mutex);
                        if (danteLookup.find(dante_name) != danteLookup.end()) {
                            auto& device = danteLookup[dante_name];
                            // Parse TXT record key-value pairs
                            size_t txt_offset = record_offset;
                            size_t txt_end = record_offset + record_length;
                            while (txt_offset < txt_end) {
                                uint8_t txt_length = ((const uint8_t*)data)[txt_offset];
                                if (txt_length == 0 || txt_offset + 1 + txt_length > txt_end) break;
                                string txt_entry((const char*)data + txt_offset + 1, txt_length);
                                size_t eq_pos = txt_entry.find('=');
                                if (eq_pos != string::npos) {
                                    string key = txt_entry.substr(0, eq_pos);
                                    string value = txt_entry.substr(eq_pos + 1);
                                    if (key == "multicastAddress") {
                                        device["multicastAddress"] = value;
                                    } else if (key == "function") {
                                        device["function"] = value;
                                        if (value == "slave" || value == "both") {
                                            device["virgil"] = true;
                                        }
                                    } else if (key == "model") {
                                        device["model"] = value;
                                    } else if (key == "deviceType") {
                                        device["deviceType"] = value;
                                    }
                                }
                                txt_offset += 1 + txt_length;
                            }
                            // Send parameter request if this is a newly found Virgil slave
                            if (device.value("virgil", false) && !device.value("isFound", false)) {
                                device["isFound"] = true;
                                SendParameterRequest(device);
                            }
                            // Store TTL for offline detection
                            device["mdnsTTL"] = record_ttl;
                            device["mdnsLastSeen"] = (int)time(nullptr);
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}

void MDNSWorkerThread() {
    this_thread::sleep_for(chrono::seconds(1)); // Wait for initialization
    cout << "Starting mDNS scanner...\n";
    
    string danteName = mdns_worker_config.danteName;
    string function = mdns_worker_config.function;
    string multicastBase = mdns_worker_config.multicastBase;
    
    // Create mDNS socket
    int sock = mdns_socket_open_ipv4(nullptr);
    if (sock < 0) {
        cerr << "[MDNSWorkerThread] mDNS socket creation failed.\n";
        return;
    }
    mdns_socket_setup_ipv4(sock, nullptr);
    
    char buffer[2048];
    
    // Use correct model and deviceType for TXT records
    std::string model = "VirgilMasterModel"; // Replace with actual model if available
    std::string deviceType = "mixer"; // Use a valid deviceType from spec
    mdns_record_txt_t txt[3];
    txt[0].key = {"function", strlen("function")};
    txt[0].value = {function.c_str(), function.length()};
    txt[1].key = {"model", strlen("model")};
    txt[1].value = {model.c_str(), model.length()};
    txt[2].key = {"deviceType", strlen("deviceType")};
    txt[2].value = {deviceType.c_str(), deviceType.length()};
    
    mdns_record_t answer = {};
    answer.name = { (danteName + "._virgil._udp.local.").c_str(), (danteName + "._virgil._udp.local.").length() };
    answer.type = MDNS_RECORDTYPE_TXT;
    answer.data.txt.key = txt[0].key;
    answer.data.txt.value = txt[0].value;
    answer.rclass = MDNS_CLASS_IN;
    answer.ttl = 60;
    
    auto last_query = chrono::steady_clock::now() - chrono::seconds(5);
    auto last_advertise = chrono::steady_clock::now() - chrono::seconds(2);
    // Use TTL and last seen for offline detection
    const chrono::seconds offlineCheckInterval(1);
    
    while (mdns_worker_running) {
        auto now = chrono::steady_clock::now();
        
        // 1. Periodically advertise this master
        if (now - last_advertise > chrono::seconds(1)) {
            for (int i = 0; i < 3; ++i) {
                answer.data.txt.key = txt[i].key;
                answer.data.txt.value = txt[i].value;
                mdns_announce_multicast(sock, buffer, sizeof(buffer), answer, nullptr, 0, nullptr, 0);
            }
            last_advertise = now;
        }
        
        // 2. Periodically query for Virgil slaves
        if (now - last_query > chrono::seconds(5)) {
            mdns_query_send(sock, MDNS_RECORDTYPE_PTR, "_virgil._udp.local.", strlen("_virgil._udp.local."),
                            buffer, sizeof(buffer), 0);
            last_query = now;
        }
        
        // 3. Poll for incoming mDNS responses
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000; // 0.2s
        
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(sock, &readfds)) {
            mdns_query_recv(sock, buffer, sizeof(buffer), mdns_query_callback, nullptr, 0);
        }
        
        // 4. Check for offline devices using TTL
        static auto lastCheck = chrono::steady_clock::now();
        if (now - lastCheck > offlineCheckInterval) {
            lock_guard<mutex> lock(danteLookup_mutex);
            int now_sec = (int)time(nullptr);
            for (auto it = danteLookup.begin(); it != danteLookup.end(); ++it) {
                const std::string& danteName = it->first;
                json& device = it->second;
                if (device.value("isFound", false)) {
                    int ttl = device.value("mdnsTTL", 60);
                    int lastSeen = device.value("mdnsLastSeen", now_sec);
                    if (now_sec - lastSeen > ttl) {
                        cout << "Device offline (TTL expired): " << danteName << endl;
                        device["isFound"] = false;
                    }
                }
            }
            lastCheck = now;
        }
        
        this_thread::sleep_for(chrono::milliseconds(50));
    }
    
    mdns_socket_close(sock);
}

void StartMDNSWorker(const string& danteName, const string& function, const string& multicastBase) {
    mdns_worker_config.danteName = danteName;
    mdns_worker_config.function = function;
    mdns_worker_config.multicastBase = multicastBase;
    mdns_worker_running = true;
    mdns_worker_thread = thread(MDNSWorkerThread);
}


void StopMDNSWorker() {
    mdns_worker_running = false;
    if (mdns_worker_thread.joinable()) {
        mdns_worker_thread.join();
    }
}

// --- Multicast group management ---
bool JoinMulticastGroup(const string& ip) {
    lock_guard<mutex> lock(multicast_mutex);
    if (current_multicast_groups.count(ip)) return false; // Already joined
    ip_mreq mreq;
    inet_pton(AF_INET, ip.c_str(), &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(netlistener_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) == 0) {
        current_multicast_groups.insert(ip);
        return true;
    }
    return false;
}

bool LeaveMulticastGroup(const string& ip) {
    lock_guard<mutex> lock(multicast_mutex);
    if (!current_multicast_groups.count(ip)) return false; // Not joined
    ip_mreq mreq;
    inet_pton(AF_INET, ip.c_str(), &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(netlistener_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) == 0) {
        current_multicast_groups.erase(ip);
        return true;
    }
    return false;
}

void NetListener(int port, const vector<string> multicast_ips, PacketHandler handler){
    this_thread::sleep_for(chrono::seconds(1)); // Wait for gui to initialize
    sockaddr_in udp_addr{};
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(port);
    netlistener_sock = CreateSocket(port);

    if (netlistener_sock == INVALID_SOCKET) {
        cerr << "NetListener socket creation failed.\n";
        return;
    }

    if (::bind(netlistener_sock, (sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) 
    {
        cerr << "UDP bind failed.\n";
        CloseSocket(netlistener_sock);
        netlistener_sock = INVALID_SOCKET;
        return;
    }

    // Join initial multicast groups
    for (const auto& ip : multicast_ips) {
        JoinMulticastGroup(ip);
    }

    fd_set readfds;
    char buffer[4096];
    while (net_listener_running) {
        FD_ZERO(&readfds);
        FD_SET(netlistener_sock, &readfds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(netlistener_sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) continue;

        if (FD_ISSET(netlistener_sock, &readfds)) {
            sockaddr_in src_addr;
            #ifdef _WIN32
                int addrlen = sizeof(src_addr);
            #else
                socklen_t addrlen = sizeof(src_addr);
            #endif
            int len = recvfrom(netlistener_sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&src_addr, &addrlen);
            if (len > 0) {
                buffer[len] = '\0';
                handler(string(buffer, len), src_addr);
            }
        }
        // Optionally: poll for subscription requests here (e.g., via a queue or callback)
    }

    // Leave all multicast groups before closing
    {
        lock_guard<mutex> lock(multicast_mutex);
        for (const auto& ip : current_multicast_groups) {
            LeaveMulticastGroup(ip);
        }
        current_multicast_groups.clear();
    }
    CloseSocket(netlistener_sock);
    netlistener_sock = INVALID_SOCKET;
}

// Start the threaded listener
void StartNetListener(int port, const vector<string> multicast_ips, PacketHandler handler){
    net_listener_running = true;
    net_listener_thread = thread(NetListener, port, multicast_ips, handler);
}

// Stop the threaded listener
void StopNetListener(){
    net_listener_running = false;
    if (net_listener_thread.joinable()) {
        net_listener_thread.join();
    }
}

void SendParameterRequest(json& device){
    // Do not lock danteLookup_mutex here; caller must hold the lock
    if (!device.is_object() || !device.value("virgil", false)) {
        cerr << "[SendParameterRequest] Device is not valid or not a Virgil device. Skipping.\n";
        return;
    }
    if (!device.contains("ip") || !device["ip"].is_string() || device["ip"].get<string>().empty()) {
        cerr << "[SendParameterRequest] Device missing valid 'ip' field: " << device.dump() << endl;
        return;
    }
    if (!device.contains("name") || !device["name"].is_string() || device["name"].get<string>().empty()) {
        cerr << "[SendParameterRequest] Device missing valid 'name' field: " << device.dump() << endl;
        return;
    }
    json request;
    request["transmittingDevice"] = "ExampleMaster";
    request["receivingDevice"] = device["name"];
    request["messages"] = json::array();
    // Device-level info
    request["messages"].push_back({{"messageType", "ParameterRequest"}, {"channelIndex", -2}});
    // Serialize the request to a string and send to the device's IP and Virgil port
    SendUdp(request, device["ip"].get<string>());
}

// Example handler function
void ProcessPacket(const string& data, const sockaddr_in& src){
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(src.sin_addr), ipstr, sizeof(ipstr));
    cout << "Received packet from " << ipstr << ":" << ntohs(src.sin_port) << endl;
    // Robustly parse and handle all valid Virgil protocol responses
    json j;
    try {
        j = json::parse(data);
    } catch (...) {
        cerr << "Received invalid JSON packet.\n";
        return;
    }
    if (!j.contains("transmittingDevice") || !j.contains("messages")) {
        cerr << "Malformed Virgil packet.\n";
        return;
    }
    const string& txDevice = j["transmittingDevice"];
    auto it = danteLookup.find(txDevice);
    if (it == danteLookup.end()) {
        cerr << "Ignoring packet from unknown device: " << txDevice << endl;
        return;
    }
    json& device = it->second;
    for (const json& message : j["messages"]) {
        if (!message.contains("messageType")) continue;
        string messageType = message["messageType"];
        if (messageType == "StatusUpdate" || messageType == "ParameterResponse") {
            // Handle device-level responses (channelIndex -1 only)
            if (message.contains("channelIndex") && int(message["channelIndex"]) == -1) {
                // Device-level info: update device fields, not channel
                json msgCopy = message;
                msgCopy.erase("channelIndex");
                msgCopy.erase("messageType");
                device.update(msgCopy);
            }
            // Ignore responses with channelIndex -2 (should never occur)
            else if (message.contains("channelIndex") && int(message["channelIndex"]) == -2) {
                cerr << "Warning: Received response with channelIndex -2 (invalid per protocol). Ignoring.\n";
            }
            // Handle channel-level responses (channelIndex >= 0)
            else if (message.contains("channelIndex")) {
                int idx = message["channelIndex"];
                if (idx >= 0) {
                    // Ensure 'channels' array exists
                    if (!device.contains("channels") || !device["channels"].is_array()) {
                        device["channels"] = json::array();
                    }
                    // Resize array if needed
                    while ((int)device["channels"].size() <= idx) {
                        device["channels"].push_back(json::object());
                    }
                    json msgCopy = message;
                    msgCopy.erase("messageType");
                    device["channels"][idx].update(msgCopy);
                }
            }
        }
        // Optionally handle errors, info, etc. here
        else if (messageType == "error") {
            std::string errorCode = message.value("errorCode", "");
            std::string errorString = message.value("errorString", "");
            cerr << "Received error from device: " << txDevice;
            if (!errorCode.empty()) cerr << " [" << errorCode << "]";
            if (!errorString.empty()) cerr << ": " << errorString;
            cerr << ": " << message.dump() << endl;
        }
    }
}

void CleanupAndExit(int code) {
    // Send mDNS goodbye packet (TTL=0) before shutdown
    {
        string danteName = mdns_worker_config.danteName;
        string function = mdns_worker_config.function;
        std::string model = "VirgilMasterModel"; // Replace with actual model if available
        std::string deviceType = "mixer"; // Use a valid deviceType from spec
        char buffer[2048];
        int sock = mdns_socket_open_ipv4(nullptr);
        if (sock >= 0) {
            mdns_socket_setup_ipv4(sock, nullptr);
            mdns_record_txt_t txt[3];
            txt[0].key = {"function", strlen("function")};
            txt[0].value = {function.c_str(), function.length()};
            txt[1].key = {"model", strlen("model")};
            txt[1].value = {model.c_str(), model.length()};
            txt[2].key = {"deviceType", strlen("deviceType")};
            txt[2].value = {deviceType.c_str(), deviceType.length()};
            mdns_record_t answer = {};
            answer.name = { (danteName + "._virgil._udp.local.").c_str(), (danteName + "._virgil._udp.local.").length() };
            answer.type = MDNS_RECORDTYPE_TXT;
            answer.data.txt.key = txt[0].key;
            answer.data.txt.value = txt[0].value;
            answer.rclass = MDNS_CLASS_IN;
            answer.ttl = 0; // Goodbye packet
            for (int i = 0; i < 3; ++i) {
                answer.data.txt.key = txt[i].key;
                answer.data.txt.value = txt[i].value;
                mdns_announce_multicast(sock, buffer, sizeof(buffer), answer, nullptr, 0, nullptr, 0);
            }
            mdns_socket_close(sock);
        }
    }
    #ifdef _WIN32
    WSACleanup();
    #endif
    StopNetListener();
    StopMDNSWorker();
    exit(code);
}

void SubscribeChannel(const string& deviceName, int channelIndex){
    SubscribeChannel(danteLookup[deviceName], channelIndex);
}

void SubscribeChannel(const json& deviceInfo, int channelIndex){
    cout << "Subscribing to channel " << channelIndex << " of device " << deviceInfo["name"] << endl;
    if (!deviceInfo.is_object() || !deviceInfo.value("virgil", false)) {
        cerr << "[SubscribeChannel] Device is not valid or not a Virgil device. Skipping.\n";
        return;
    }
    if(!deviceInfo.contains("multicastAddress") || !deviceInfo["multicastAddress"].is_string() || deviceInfo["multicastAddress"].get<string>().empty()) {
        cerr << "[SubscribeChannel] Device missing valid 'multicastAddress' field: " << deviceInfo.dump() << endl;
        return;
    }
    if (!deviceInfo.contains("channelIndices") || !deviceInfo["channelIndices"].is_array()) {
        cerr << "[SubscribeChannel] Device missing valid 'channelIndices' field: " << deviceInfo.dump() << endl;
        return;
    }
    bool found = find(deviceInfo["channelIndices"].begin(), deviceInfo["channelIndices"].end(), channelIndex) != deviceInfo["channelIndices"].end();
    if (!found) {
        cerr << "[SubscribeChannel] Channel index " << channelIndex << " not found in device" << endl;
        return;
    }
    JoinMulticastGroup(deviceInfo["multicastAddress"].get<string>() + "." + to_string(channelIndex));
    cout << "Subscribed to channel " << channelIndex << " of device " << deviceInfo["name"] << endl;
}

void UnsubscribeChannel(const string& deviceName, int channelIndex){
    UnsubscribeChannel(danteLookup[deviceName], channelIndex);
}

void UnsubscribeChannel(const json& deviceInfo, int channelIndex){
    cout << "Unsubscribing from channel " << channelIndex << " of device " << deviceInfo["name"] << endl;
    if (!deviceInfo.is_object() || !deviceInfo.value("virgil", false)) {
        cerr << "[UnsubscribeChannel] Device is not valid or not a Virgil device. Skipping.\n";
        return;
    }
    if(!deviceInfo.contains("multicastAddress") || !deviceInfo["multicastAddress"].is_string() || deviceInfo["multicastAddress"].get<string>().empty()) {
        cerr << "[UnsubscribeChannel] Device missing valid 'multicastAddress' field: " << deviceInfo.dump() << endl;
        return;
    }
    if (!deviceInfo.contains("channelIndices") || !deviceInfo["channelIndices"].is_array()) {
        cerr << "[UnsubscribeChannel] Device missing valid 'channelIndices' field: " << deviceInfo.dump() << endl;
        return;
    }
    bool found = find(deviceInfo["channelIndices"].begin(), deviceInfo["channelIndices"].end(), channelIndex) != deviceInfo["channelIndices"].end();
    if (!found) {
        cerr << "[UnsubscribeChannel] Channel index " << channelIndex << " not found in device" << endl;
        return;
    }
    LeaveMulticastGroup(deviceInfo["multicastAddress"].get<string>() + "." + to_string(channelIndex));
    cout << "Unsubscribed from channel " << channelIndex << " of device " << deviceInfo["name"] << endl;
}

template<typename T>
enable_if_t<is_convertible_v<T, string>>
SendCommand(const string& deviceName, int channelIndex, const string& param, T value) {
    SendCommand(danteLookup[deviceName], channelIndex, param, value);
}

template<typename T>
enable_if_t<is_convertible_v<T, string>>
SendCommand(const json& deviceInfo, int channelIndex, const string& param, T value) {
    if (!deviceInfo.is_object() || !deviceInfo.value("virgil", false)) {
        cerr << "[SendCommand] Device is not valid or not a Virgil device. Skipping.\n";
        return;
    }
    if (!deviceInfo.contains("channelIndices") || !deviceInfo["channelIndices"].is_array()) {
        cerr << "[SendCommand] Device missing valid 'channelIndices' field: " << deviceInfo.dump() << endl;
        return;
    }
    bool found = find(deviceInfo["channelIndices"].begin(), deviceInfo["channelIndices"].end(), channelIndex) != deviceInfo["channelIndices"].end();
    if (!found) {
        cerr << "[SendCommand] Channel index " << channelIndex << " not found in device" << endl;
        return;
    }
    if(!deviceInfo["channels"][channelIndex].contains(param))
    {
        cerr << "[SendCommand] Channel " << channelIndex << " of device " << deviceInfo["name"] << " is missing parameter: " << param << endl;
        return;
    }
    //I don't feel like doing data validation, but let's just say it's to test error messages :)
    json command;
    command["transmittingDevice"] = "ExampleMaster";
    command["messages"] = json::array();
    command["messages"].push_back({
        {"messageType", "ParameterCommand"},
        {"channelIndex", channelIndex},
        {param, {"value", value}}
    });
    SendUdp(command, deviceInfo["ip"].get<string>());
}

int main() 
{
    // Windows network initialization
    #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
        {
            cerr << "WSAStartup failed.\n";
            return 1;
        }
    #endif

    cout << "This doesn't work. It's not that the code isn't functional, it's that I couldn't find a way to have good input without a GUI, and that would make it a lot more complicated." << endl;
    // Start mDNS advertising as a master
    StartNetListener(virgilPort, vector<string>{}, ProcessPacket);
    StartMDNSWorker("ExampleMaster", "master");
    
    
    CleanupAndExit(0);
}

