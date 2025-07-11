#include <iostream>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>

// Include nlohmann/json (single header library)
// Download from: https://github.com/nlohmann/json/releases
#include "nlohmann/json.hpp"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h> 
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #ifndef IP_MULTICAST_TTL
        #define IP_MULTICAST_TTL 3
    #endif
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
#endif

using json = nlohmann::json;
using namespace std;


atomic<bool> mdns_running{false};
thread mdns_thread;
atomic<bool> mdns_scanner_running{false};
thread mdns_scanner_thread;
mutex danteLookup_mutex;

atomic<bool> net_listener_running{false};
thread net_listener_thread;

// This function will be called with received data and sender info
using PacketHandler = std::function<void(const std::string&, const sockaddr_in&)>;

int virgilPort = 7889;
//Here, we have a preexisting list of Dante devices we are subscribed to
//You would get this from your preexisting Dante code
//I'm just making up a format to make my life easier
map<string,json> danteLookup = {};


socket_t CreateSocket(int type, int port, sockaddr_in& addr) 
{
    socket_t sock = socket(AF_INET, type, 0);
    if (sock < 0) 
    {
        cerr << "Socket creation failed.\n";
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    #ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    #else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #endif
    return sock;
}

void CloseSocket(socket_t& sock) 
{
    #ifdef _WIN32
        closesocket(sock);
    #else   
        close(sock);
    #endif
}

void AdvertiseMDNS(const string& danteName, const string& function, const string& multicastBase = "")
{
    const char* mdns_ip = "224.0.0.251";
    int mdns_port = 5353;

    string serviceType = "_virgil._udp.local.";

    json mdns_advert;
    mdns_advert["serviceName"] = danteName + "." + serviceType;
    mdns_advert["serviceType"] = serviceType;
    mdns_advert["port"] = virgilPort;
    if(multicastBase.empty() && function == "slave") 
    {
        cerr << "Slaves must have a multicast base\n";
        return;
    }
    else if(function == "slave")
    {
        mdns_advert["txt"]["multicast"] = multicastBase;
    }
    mdns_advert["txt"]["function"] = function;
    mdns_advert["txt"]["model"] = "virgilExample";
    mdns_advert["txt"]["deviceType"] = "computer";

    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_DGRAM, mdns_port, addr);

    // Set destination address to multicast group
    addr.sin_family = AF_INET;
    addr.sin_port = htons(mdns_port);
    inet_pton(AF_INET, mdns_ip, &addr.sin_addr);

    string payload = mdns_advert.dump();

    while (mdns_running) {
        int res = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&addr, sizeof(addr));
        if (res < 0) {
            cerr << "mDNS advertisement failed.\n";
        }
        this_thread::sleep_for(chrono::seconds(1));
    }

    CloseSocket(sock);
}


// mDNS scanner thread function
void MDNSScannerThread() {
    const char* mdns_ip = "224.0.0.251";
    int mdns_port = 5353;
    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_DGRAM, mdns_port, addr);
    if (sock < 0) {
        cerr << "mDNS read socket creation failed.\n";
        return;
    }
    if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "mDNS bind failed.\n";
        CloseSocket(sock);
        return;
    }
    // Join mDNS multicast group
    ip_mreq mreq;
    inet_pton(AF_INET, mdns_ip, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
    char buffer[4096];
    // Track last seen time for each device
    map<string, chrono::steady_clock::time_point> lastSeen;
    const chrono::seconds offlineTimeout(10); // If not seen for 10s, consider offline
    while (mdns_scanner_running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000; // 0.5 second
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        bool updated = false;
        if (ready > 0 && FD_ISSET(sock, &readfds)) {
            sockaddr_in src_addr;
            #ifdef _WIN32
                int addrlen = sizeof(src_addr);
            #else
                socklen_t addrlen = sizeof(src_addr);
            #endif
            int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&src_addr, &addrlen);
            if (len > 0) {
                buffer[len] = '\0';
                // Parse and update danteLookup
                json j;
                try { j = json::parse(buffer, buffer + len); } catch (...) { continue; }
                if (j.contains("serviceType") && j["serviceType"] == "_virgil._udp.local." && j["txt"]["function"] == "slave") {
                    string serviceName = j["serviceName"];
                    string serviceType = j["serviceType"];
                    string danteName = serviceName.erase(serviceName.find(serviceType), serviceName.length());
                    auto now = chrono::steady_clock::now();
                    bool isNew = false;
                    {
                        lock_guard<mutex> lock(danteLookup_mutex);
                        if (danteLookup.find(danteName) == danteLookup.end()) {
                            danteLookup[danteName] = json::object();
                            danteLookup[danteName]["name"] = danteName;
                            danteLookup[danteName]["preamps"] = json::array();
                            isNew = true;
                        }
                        auto& device = danteLookup[danteName];
                        device["multicast"] = j["txt"]["multicast"];
                        device["virgil"] = true;
                        char ipstr[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &src_addr.sin_addr, ipstr, sizeof(ipstr));
                        device["ip"] = string(ipstr);
                    }
                    if (isNew) {
                        cout << "Found Virgil device: " << danteName << endl;
                    }
                    lastSeen[danteName] = now;
                    updated = true;
                }
            }
        }
        // Check for offline devices every second
        static auto lastCheck = chrono::steady_clock::now();
        auto now = chrono::steady_clock::now();
        if (now - lastCheck > chrono::seconds(1)) {
            vector<string> toRemove;
            for (const auto& [danteName, seenTime] : lastSeen) {
                if (now - seenTime > offlineTimeout) {
                    cout << "Device offline: " << danteName << endl;
                    toRemove.push_back(danteName);
                    lock_guard<mutex> lock(danteLookup_mutex);
                    danteLookup.erase(danteName);
                }
            }
            for (const auto& name : toRemove) lastSeen.erase(name);
            lastCheck = now;
        }
        // Sleep a bit to avoid busy loop
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    CloseSocket(sock);
}

void StartMDNSScanner() {
    mdns_scanner_running = true;
    mdns_scanner_thread = thread(MDNSScannerThread);
}

void StopMDNSScanner() {
    mdns_scanner_running = false;
    if (mdns_scanner_thread.joinable()) {
        mdns_scanner_thread.join();
    }
}

void StartMDNS(const string& danteName, const string& function, const string& multicastBase = "") 
{
    mdns_running = true;
    mdns_thread = std::thread(AdvertiseMDNS, danteName, function, multicastBase);
}

void StopMDNS() 
{
    mdns_running = false;
    if (mdns_thread.joinable()) {
        mdns_thread.join();
    }
}

// SendVirgilUDP: Send a Virgil protocol message via UDP (unicast)
bool SendUDP(const string& ip, int port, const json& message) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "UDP socket creation failed.\n";
        return false;
    }
    string payload = message.dump();
    int res = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&addr, sizeof(addr));
    if (res < 0) {
        cerr << "UDP send failed.\n";
        CloseSocket(sock);
        return false;
    }
    CloseSocket(sock);
    return true;
}

bool SendMulticast(const string& multicast_ip, int port, const json& message) 
{
    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_DGRAM, port, addr);

    // Set the destination multicast IP
    if (inet_pton(AF_INET, multicast_ip.c_str(), &addr.sin_addr) != 1) {
        cerr << "Invalid multicast IP address: " << multicast_ip << endl;
        CloseSocket(sock);
        return false;
    }

    std::string messageDump = message.dump();
    int send_result = sendto(sock, messageDump.c_str(), messageDump.size(), 0, (sockaddr*)&addr, sizeof(addr));
    if (send_result < 0) 
    {
        cerr << "Multicast send failed.\n";
        CloseSocket(sock);
        return false;
    }

    CloseSocket(sock);
    return true;
}

void NetListener(int port, const vector<string> multicast_ips, PacketHandler handler)
{
    sockaddr_in tcp_addr;
    socket_t tcp_sock = CreateSocket(SOCK_STREAM, port,tcp_addr);

    if (::bind(tcp_sock, (sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) 
    {
        cerr << "TCP bind failed.\n";
        CloseSocket(tcp_sock);
        return;
    }
    listen(tcp_sock, 5);

    // --- Multicast UDP socket setup ---
    sockaddr_in udp_addr;
    socket_t udp_sock = CreateSocket(SOCK_DGRAM, port, udp_addr);

    if (::bind(udp_sock, (sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) 
    {
        cerr << "UDP bind failed.\n";
        CloseSocket(udp_sock);
        CloseSocket(tcp_sock);
        return;
    }

    // Join multicast group
    for (const auto& ip : multicast_ips) 
    {
        ip_mreq mreq;
        inet_pton(AF_INET, ip.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
    }

    // --- Main loop ---
    fd_set readfds;
    char buffer[4096];
    while (net_listener_running) 
    {
        FD_ZERO(&readfds);
        FD_SET(tcp_sock, &readfds);
        FD_SET(udp_sock, &readfds);
        socket_t maxfd = (tcp_sock > udp_sock) ? tcp_sock : udp_sock;

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(maxfd + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) continue;

        // --- TCP: Accept and read ---
        if (FD_ISSET(tcp_sock, &readfds)) 
        {
            sockaddr_in client_addr;
            #ifdef _WIN32
                int addrlen = sizeof(client_addr);
            #else
                socklen_t addrlen = sizeof(client_addr);
            #endif
            socket_t client_sock = accept(tcp_sock, (sockaddr*)&client_addr, &addrlen);
            if (client_sock >= 0) 
            {
                int len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
                if (len > 0) 
                {
                    buffer[len] = '\0';
                    handler(string(buffer, len), client_addr);
                }
                CloseSocket(client_sock);
            }
        }

        // --- Multicast UDP: Receive ---
        if (FD_ISSET(udp_sock, &readfds)) 
        {
            sockaddr_in src_addr;
            #ifdef _WIN32
                int addrlen = sizeof(src_addr);
            #else
                socklen_t addrlen = sizeof(src_addr);
            #endif
            int len = recvfrom(udp_sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&src_addr, &addrlen);
            if (len > 0) 
            {
                buffer[len] = '\0';
                handler(string(buffer, len), src_addr);
            }
        }
    }

    CloseSocket(tcp_sock);
    CloseSocket(udp_sock);
}

// Start the threaded listener
void StartNetListener(int port, const vector<string> multicast_ips, PacketHandler handler)
{
    net_listener_running = true;
    net_listener_thread = thread(NetListener, port, multicast_ips, handler);
}

// Stop the threaded listener
void StopNetListener()
{
    net_listener_running = false;
    if (net_listener_thread.joinable()) {
        net_listener_thread.join();
    }
}

// Example handler function
void ProcessPacket(const string& data, const sockaddr_in& src)
{
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
    json& device = danteLookup[j["transmittingDevice"]];
    for (const json& message : j["messages"]) {
        if (!message.contains("messageType")) continue;
        string messageType = message["messageType"];
        if (messageType == "statusUpdate" || messageType == "statusResponse" || messageType == "parameterResponse") {
            // Handle device-level responses (preampIndex -1 only)
            if (message.contains("preampIndex") && int(message["preampIndex"]) == -1) {
                // Device-level info: update device fields, not preamp
                json msgCopy = message;
                msgCopy.erase("preampIndex");
                msgCopy.erase("messageType");
                device.update(msgCopy);
                // If preampCount is present, update device's preampCount
                if (msgCopy.contains("preampCount")) {
                    device["preampCount"] = msgCopy["preampCount"];
                }
                continue;
            }
            // Ignore responses with preampIndex -2 (should never occur)
            if (message.contains("preampIndex") && int(message["preampIndex"]) == -2) {
                cerr << "Warning: Received response with preampIndex -2 (invalid per protocol). Ignoring.\n";
                continue;
            }
            // Handle preamp-level responses (preampIndex >= 0)
            if (message.contains("preampIndex")) {
                int idx = message["preampIndex"];
                if (idx >= 0 && device.contains("preamps") && idx < device["preamps"].size()) {
                    json msgCopy = message;
                    msgCopy.erase("preampIndex");
                    msgCopy.erase("messageType");
                    device["preamps"][idx].update(msgCopy);
                }
            }
        }
        // Optionally handle errors, info, etc. here
        else if (messageType == "error") {
            cerr << "Received error from device: " << j["transmittingDevice"] << ": " << message.dump() << endl;
        }
        // Add more messageType handling as needed
    }
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


    // Start mDNS advertising as a master
    StartMDNS("ExampleDevice", "master");
    // Start mDNS scanner in a separate thread
    StartMDNSScanner();

    cout << "Starting mDNS scanner...\n";
    // Wait a moment for initial scan
    this_thread::sleep_for(chrono::seconds(5));



    // Print all found devices and let user select which to request data from
    vector<string> device_keys;
    cout << "Devices found:" << endl;
    int idx = 1;
    {
        lock_guard<mutex> lock(danteLookup_mutex);
        for (auto& [key, device] : danteLookup) {
            cout << idx << ": Name: " << device["name"];
            if (device.contains("ip")) cout << ", IP: " << device["ip"];
            if (device.contains("multicast")) cout << ", Multicast: " << device["multicast"];
            cout << endl;
            device_keys.push_back(key);
            ++idx;
        }
    }
    if (device_keys.empty()) {
        cout << "No devices found. Exiting." << endl;
        StopNetListener();
        StopMDNS();
        StopMDNSScanner();
        #ifdef _WIN32
        WSACleanup();
        #endif
        return 0;
    }
    cout << "Enter device numbers to request data from (comma-separated, or 'all'): ";
    string input;
    getline(cin, input);
    vector<int> selected_indices;
    if (input == "all" || input == "ALL") {
        for (int i = 1; i <= (int)device_keys.size(); ++i) selected_indices.push_back(i);
    } else {
        size_t pos = 0;
        while (pos < input.size()) {
            while (pos < input.size() && !isdigit(input[pos])) ++pos;
            if (pos >= input.size()) break;
            int num = 0;
            while (pos < input.size() && isdigit(input[pos])) {
                num = num * 10 + (input[pos] - '0');
                ++pos;
            }
            if (num > 0 && num <= (int)device_keys.size()) selected_indices.push_back(num);
        }
    }


    // Find all multicast addresses for selected devices
    vector<string> multicast_addresses;
    {
        lock_guard<mutex> lock(danteLookup_mutex);
        for (int i : selected_indices) {
            auto& device = danteLookup[device_keys[i-1]];
            if (device["virgil"] == false) continue;
            string base = device["multicast"];
            multicast_addresses.push_back(base + ".-1"); // Device-level
            multicast_addresses.push_back(base + ".-1"); // All-preamp (if protocol uses -1 for both)
            for (const auto& preamp : device["preamps"]) {
                multicast_addresses.push_back(base + "." + to_string(preamp["preampIndex"]));
            }
        }
    }


    // Start the listener before sending info requests
    StartNetListener(virgilPort, multicast_addresses, ProcessPacket);

    // Send all status requests to selected devices
    {
        lock_guard<mutex> lock(danteLookup_mutex);
        for (int i : selected_indices) {
            auto& device = danteLookup[device_keys[i-1]];
            if (device["virgil"] == false) continue;
            json request;
            request["transmittingDevice"] = "ExampleDevice";
            request["receivingDevice"] = device["name"];
            request["messages"] = json::array();
            // Device-level info
            request["messages"].push_back({{"messageType", "ParameterRequest"}, {"preampIndex", -2}});
            if (!SendUDP(device["ip"], virgilPort, request)) {
                cerr << "Failed to send ParameterRequest to device: " << device["name"] << endl;
            }
            cout << request.dump(2) << endl;
        }
    }

    // Wait for responses interactively (or for a set time)
    cout << "Listening for responses. Press Enter to exit..." << endl;
    cin.get();

    // Cleanup
    #ifdef _WIN32
    WSACleanup();
    #endif
    StopNetListener();
    StopMDNS();
    StopMDNSScanner();
    return 0;
}