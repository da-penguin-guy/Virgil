#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <set>
#include <map>
#include "mdns/mdns.h"

#include "VirgilNet.hpp"

using namespace std;

string MULTICAST_BASE = "244.1.1"; // Will be set dynamically
constexpr char SLAVE_DANTE_NAME[] = "ExampleSlave";


struct channel : public json {
    channel(int idx = 0) {
        (*this)["channelIndex"] = idx;
        (*this)["gain"] = {
            {"dataType", "number"}, {"unit", "dB"}, {"precision", 1}, {"value", 0}, {"minValue", -5}, {"maxValue", 50}, {"locked", false}
        };
        (*this)["pad"] = {
            {"dataType", "bool"}, {"value", false}, {"padLevel", -10}, {"locked", false}
        };
        (*this)["lowcut"] = {
            {"dataType", "number"}, {"unit", "Hz"}, {"precision", 1}, {"value", 0}, {"minValue", 0}, {"maxValue", 100}, {"locked", false}
        };
        (*this)["lowcutEnable"] = {
            {"dataType", "bool"}, {"value", false}, {"locked", false}
        };
        (*this)["polarity"] = {
            {"dataType", "bool"}, {"value", false}, {"locked", false}
        };
        (*this)["phantomPower"] = {
            {"dataType", "bool"}, {"value", false}, {"locked", false}
        };
        (*this)["rfEnable"] = {
            {"dataType", "bool"}, {"value", true}, {"locked", false}
        };
        (*this)["transmitPower"] = {
            {"dataType", "enum"}, {"value", "high"}, {"enumValues", json::array({"low", "medium", "high"})}, {"locked", false}
        };
        (*this)["transmitterConnected"] = {
            {"dataType", "bool"}, {"value", true}, {"locked", true}
        };
        (*this)["squelch"] = {
            {"dataType", "number"}, {"unit", "dB"}, {"precision", 1}, {"value", -60}, {"minValue", -80}, {"maxValue", -20}, {"locked", false}
        };
        (*this)["subDevice"] = {
            {"dataType", "string"}, {"value", "handheld"}, {"locked", true}
        };
        (*this)["audioLevel"] = {
            {"dataType", "number"}, {"unit", "dB"}, {"value", -20.5}, {"locked", true}
        };
        (*this)["rfLevel"] = {
            {"dataType", "number"}, {"unit", "dB"}, {"value", -45.2}, {"locked", true}
        };
        (*this)["batteryLevel"] = {
            {"dataType", "number"}, {"unit", "%"}, {"value", 85}, {"locked", true}
        };
    }
    int index() const { return (*this)["channelIndex"]; }
};

struct DeviceInfo {
    string model = "ChannelModelName";
    string deviceType = "digitalStageBox";
    vector<int> channelIndices = {0, 1, 2}; // Default to 3 channels, can be set as needed
};

map<int, channel> channels;
DeviceInfo deviceInfo;
mutex state_mutex;

atomic<bool> mdns_running{true};


// Callback for mDNS query responses to collect used multicast addresses
int mdns_query_callback(int sock, const struct sockaddr* from, size_t addrlen,
                        mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                        uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                        size_t name_offset, size_t name_length, size_t record_offset,
                        size_t record_length, void* user_data) {
    if (rtype == MDNS_RECORDTYPE_TXT) {
        // Parse TXT record
        char txt[256] = {0};
        size_t txt_len = (record_length < sizeof(txt)-1) ? record_length : sizeof(txt)-1;
        memcpy(txt, (const char*)data + record_offset, txt_len);
        string txt_str(txt, txt_len);
        // Look for "multicastAddress=..." in TXT
        size_t pos = txt_str.find("multicastAddress=");
        if (pos != string::npos) {
            size_t end = txt_str.find_first_of("\n\r", pos);
            string val = txt_str.substr(pos + 16, end - (pos + 16));
            ((set<string>*)user_data)->insert(val);
        }
    }
    return 0;
}

// Scan mDNS for existing slaves and pick the lowest available multicast address
string PickLowestAvailableMulticastAddress() {
    int sock = mdns_socket_open_ipv4(nullptr);
    if (sock < 0) return "244.1.1";
    mdns_socket_setup_ipv4(sock, nullptr);

    char buffer[2048];
    // Query for PTR records of _virgil._udp.local.
    mdns_query_send(sock, MDNS_RECORDTYPE_PTR, "_virgil._udp.local.", strlen("_virgil._udp.local."), buffer, sizeof(buffer), 0);

    set<string> used_multicast;
    auto start = chrono::steady_clock::now();
    while (chrono::steady_clock::now() - start < chrono::seconds(5)) {
        mdns_query_recv(sock, buffer, sizeof(buffer), mdns_query_callback, &used_multicast, 0);
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    mdns_socket_close(sock);

    // Find the lowest available multicast address in the range 224.1.1 to 239.255.255
    for (int a = 224; a <= 239; ++a) {
        for (int b = 1; b <= 255; ++b) {
            for (int c = 1; c <= 255; ++c) {
                string candidate = to_string(a) + "." + to_string(b) + "." + to_string(c);
                if (used_multicast.find(candidate) == used_multicast.end()) {
                    return candidate;
                }
            }
        }
    }
    // Fallback
    return "244.1.1";
}

void AdvertiseMDNS() {
    int sock = mdns_socket_open_ipv4(nullptr);
    mdns_socket_setup_ipv4(sock, nullptr);
    char buffer[2048];

    // Prepare TXT records
    mdns_record_txt_t txt[4];
    txt[0].key = {"multicastAddress", strlen("multicastAddress")};
    txt[0].value = {MULTICAST_BASE.c_str(), MULTICAST_BASE.length()};
    txt[1].key = {"function", strlen("function")};
    txt[1].value = {"slave", strlen("slave")};
    txt[2].key = {"model", strlen("model")};
    txt[2].value = {deviceInfo.model.c_str(), deviceInfo.model.length()};
    txt[3].key = {"deviceType", strlen("deviceType")};
    txt[3].value = {deviceInfo.deviceType.c_str(), deviceInfo.deviceType.length()};

    // Prepare mDNS answer record
    mdns_record_t answer = {};
    answer.name = { (string(SLAVE_DANTE_NAME) + "._virgil._udp.local.").c_str(), strlen((string(SLAVE_DANTE_NAME) + "._virgil._udp.local.").c_str()) };
    answer.type = MDNS_RECORDTYPE_TXT;
    answer.data.txt.key = txt[0].key;
    answer.data.txt.value = txt[0].value;
    answer.rclass = MDNS_CLASS_IN;
    answer.ttl = 60;

    while (mdns_running) {
        // Announce all TXT records
        for (int i = 0; i < 4; ++i) {
            answer.data.txt.key = txt[i].key;
            answer.data.txt.value = txt[i].value;
            mdns_announce_multicast(sock, buffer, sizeof(buffer), answer, nullptr, 0, nullptr, 0);
        }
        this_thread::sleep_for(chrono::seconds(1));
    }
    mdns_socket_close(sock);
}

json MakeErrorResponse(const string& errorValue, const string& errorString, bool onlyMessage = false) {
    if(onlyMessage){
        json m;
        m["messageType"] = "ErrorResponse";
        m["errorValue"] = errorValue;
        m["errorString"] = errorString;
        return m;
    }
    json msg;
    msg["transmittingDevice"] = SLAVE_DANTE_NAME;
    msg["messages"] = json::array();
    json m;
    m["messageType"] = "ErrorResponse";
    m["errorValue"] = errorValue;
    m["errorString"] = errorString;
    msg["messages"].push_back(m);
    return msg;
}

json MakeParameterResponse(int channelIndex) {
    lock_guard<mutex> lock(state_mutex);
    json msg;
    msg["transmittingDevice"] = SLAVE_DANTE_NAME;
    msg["messages"] = json::array();
    
    if (channelIndex == -1) {
        // Device-level response
        json dev;
        dev["messageType"] = "ParameterResponse";
        dev["channelIndex"] = -1;
        dev["model"] = deviceInfo.model;
        dev["deviceType"] = deviceInfo.deviceType;
        dev["virgilVersion"] = "1.0.0";
        dev["multicastAddress"] = MULTICAST_BASE;
        json channelIndices = json::array();
        for (int idx : deviceInfo.channelIndices) {
            channelIndices.push_back(idx);
        }
        dev["channelIndices"] = channelIndices;
        msg["messages"].push_back(dev);
    } else if (channelIndex == -2) {
        // All: include device-level info and all channels
        json dev;
        dev["messageType"] = "ParameterResponse";
        dev["channelIndex"] = -1;
        dev["model"] = deviceInfo.model;
        dev["deviceType"] = deviceInfo.deviceType;
        dev["virgilVersion"] = "1.0.0";
        dev["multicastAddress"] = MULTICAST_BASE;
        json channelIndices = json::array();
        for (int idx : deviceInfo.channelIndices) {
            channelIndices.push_back(idx);
        }
        dev["channelIndices"] = channelIndices;
        msg["messages"].push_back(dev);
        
        for (map<int, channel>::const_iterator it = channels.begin(); it != channels.end(); ++it) {
            int idx = it->first;
            const channel& p = it->second;
            json channel_msg;
            channel_msg["messageType"] = "ParameterResponse";
            channel_msg["channelIndex"] = idx;
            // Copy all parameters except channelIndex to avoid duplication
            for (json::const_iterator jt = p.begin(); jt != p.end(); ++jt) {
                const std::string& key = jt.key();
                const json& value = jt.value();
                if (key != "channelIndex") {
                    channel_msg[key] = value;
                }
            }
            msg["messages"].push_back(channel_msg);
        }
    } else if (channelIndex >= 0 && channels.find(channelIndex) != channels.end()) {
        // Single channel response
        json p_msg;
        p_msg["messageType"] = "ParameterResponse";
        p_msg["channelIndex"] = channelIndex;
        // Copy all parameters except channelIndex to avoid duplication
        const channel& ch = channels.at(channelIndex);
        for (json::const_iterator jt = ch.begin(); jt != ch.end(); ++jt) {
            const std::string& key = jt.key();
            const json& value = jt.value();
            if (key != "channelIndex") {
                p_msg[key] = value;
            }
        }
        msg["messages"].push_back(p_msg);
    } else {
        // Invalid channel index - should send error
        return MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index: " + to_string(channelIndex));
    }
    return msg;
}

void MakeStatusUpdate(int channelIndex, const vector<string>& params){
    json msg;

    msg["transmittingDevice"] = SLAVE_DANTE_NAME;
    msg["messages"] = json::array();
    json statusUpdate = json();
    statusUpdate["messageType"] = "StatusUpdate";
    statusUpdate["channelIndex"] = channelIndex;
    for (vector<string>::const_iterator it = params.begin(); it != params.end(); ++it) {
        const std::string& p = *it;
        if (channels.at(channelIndex).contains(p)) {
            statusUpdate[p] = channels.at(channelIndex)[p]["value"];
        } else {
            cerr << "Warning: Channel " << channelIndex << " does not have parameter '" << p << "' defined." << endl;
        }
    }
    msg["messages"].push_back(statusUpdate);
    SendMulticast(msg, MULTICAST_BASE + "." + to_string(channelIndex));
}

void HandlePacket(const string& data, const sockaddr_in& src) {
    // Log all received packets
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src.sin_addr, ipstr, sizeof(ipstr));
    
    string formatted_data = data;
    // Try to format JSON for logging
    try {
        auto j = json::parse(data);
        formatted_data = j.dump(2);
    } catch (...) {
        // If parsing fails, use original data
    }
    
    
    json j;
    try {
        j = json::parse(data);
    } catch (...) {
        // Send error response for invalid JSON
        json err = MakeErrorResponse("MalformedMessage", "Invalid JSON format");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        return;
    }
    if (!j.contains("messages")) {
        // Send error response for malformed packet
        json err = MakeErrorResponse("MalformedMessage", "Missing 'messages' array");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        return;
    }
    
    // Check for empty messages array
    if (j["messages"].empty()) {
        json err = MakeErrorResponse("MalformedMessage", "Empty messages array");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        return;
    }
    if (!j.contains("transmittingDevice")) {
        json err = MakeErrorResponse("MalformedMessage", "Missing 'transmittingDevice' field");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        return;
    }
    for (auto& msg : j["messages"]) {
        if (!msg.contains("messageType")) {
            json err = MakeErrorResponse("MalformedMessage", "Missing messageType");
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(err, fixed_src);
            continue;
        }
        string type = msg["messageType"];
        
        if (type == "ParameterRequest") {
            int idx = msg.value("channelIndex", -1);
            json resp = MakeParameterResponse(idx);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(resp, fixed_src);
        } else if (type == "ParameterCommand") {
            int idx = msg.value("channelIndex", -1);
            if (!msg.contains("channelIndex")) {
                json err = MakeErrorResponse("MalformedMessage", "Missing channelIndex");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                continue;
            }
            if (channels.find(idx) == channels.end()) {
                json err = MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                continue;
            }
            msg.erase("messageType");
            msg.erase("channelIndex");
            vector<json> errors = {};
            vector<string> changedParams = {};
            for (json::const_iterator jt = msg.begin(); jt != msg.end(); ++jt) {
                const std::string& key = jt.key();
                const json& value = jt.value();
                if(!channels[idx].contains(key)) {
                    // Update channel parameter
                    cerr << "Warning: Channel " << idx << " does not have parameter '" << key << "' defined." << endl;
                    errors.push_back(MakeErrorResponse("ParameterUnsupported", "Device does not have parameter '" + key + "'", true));
                    continue;
                }
                if(channels[idx][key]["locked"].get<bool>()) {
                    // Parameter is locked, cannot change
                    errors.push_back(MakeErrorResponse("ParameterLocked", "Parameter '" + key + "' is locked", true));
                    continue;
                }
                // Check value type
                string expectedType = channels[idx][key]["dataType"];
                if (expectedType == "number" && !value.is_number()) {
                    errors.push_back(MakeErrorResponse("InvalidValueType", "Parameter '" + key + "' expects a number", true));
                    continue;
                }
                if (expectedType == "number" && value.is_number()) {
                    if (channels[idx][key].contains("minValue") && channels[idx][key].contains("maxValue")) {
                        double v = value.get<double>();
                        double minV = channels[idx][key]["minValue"].get<double>();
                        double maxV = channels[idx][key]["maxValue"].get<double>();
                        double precision = channels[idx][key]["precision"].get<double>();
                        if (v < minV || v > maxV) {
                            errors.push_back(MakeErrorResponse("ValueOutOfRange", "Parameter '" + key + "' value " + to_string(v) + " is out of range [" + to_string(minV) + ", " + to_string(maxV) + "]", true));
                            continue;
                        }
                        //I know that this looks a bit weird, but it's mathamatically correct
                        if(fmod(v-minV,precision) != 0) {
                            errors.push_back(MakeErrorResponse("ValueNotMultipleOfPrecision", "Parameter '" + key + "' value " + to_string(v) + " is not a multiple of precision " + to_string(precision), true));
                            continue;
                        }
                    }
                }
                if (expectedType == "bool" && !value.is_boolean()) {
                    errors.push_back(MakeErrorResponse("InvalidValueType", "Parameter '" + key + "' expects a boolean", true));
                    continue;
                }
                if (expectedType == "string" && !value.is_string()) {
                    errors.push_back(MakeErrorResponse("InvalidValueType", "Parameter '" + key + "' expects a string", true));
                    continue;
                }
                if (expectedType == "enum") {
                    if (!value.is_string()) {
                        errors.push_back(MakeErrorResponse("InvalidValueType", "Parameter '" + key + "' expects an enum string", true));
                        continue;
                    }
                    bool found = false;
                    for (json::const_iterator vt = channels[idx][key]["enumValues"].begin(); vt != channels[idx][key]["enumValues"].end(); ++vt) {
                        if (*vt == value) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        errors.push_back(MakeErrorResponse("ValueOutOfRange", "Parameter '" + key + "' has invalid enum value: " + value.get<string>(), true));
                        continue;
                    }
                }
                channels[idx][key]["value"] = value;
                changedParams.push_back(key);
            }
            if(!errors.empty())
            {
                json msg;
                msg["transmittingDevice"] = SLAVE_DANTE_NAME;
                msg["messages"] = json::array();
                for (const auto& err : errors) {
                    msg["messages"].push_back(err);
                }
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(msg, fixed_src);
            }
            if(!changedParams.empty()) {
                MakeStatusUpdate(idx, changedParams);
            }
        } else {
            // Handle unsupported message types
            json err = MakeErrorResponse("UnrecognizedCommand", "Unsupported message type: " + type);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(err, fixed_src);
        }
    }
}



int main() {
    #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            return 0;
        }
    #endif

    // Scan mDNS and pick lowest available multicast address
    MULTICAST_BASE = PickLowestAvailableMulticastAddress();

    // Initialize channels
    deviceInfo = DeviceInfo();
    channels.clear();
    for (int idx : deviceInfo.channelIndices) {
        channels[idx] = channel(idx);
    }

    // Start mDNS advertisement in a background thread
    thread mdns_thread(AdvertiseMDNS);

    // Create UDP socket for listening
    socket_t sock = CreateSocket(VIRGIL_PORT);
    if (sock < 0) {
        mdns_running = false;
        if (mdns_thread.joinable()) mdns_thread.join();
        return 0;
    }
    // Set socket to non-blocking mode
    #ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
    #else
        fcntl(sock, F_SETFL, O_NONBLOCK);
    #endif
    cout << "Starting";
    char buffer[4096];
    auto lastStatusUpdate = chrono::steady_clock::now();
    while (true) {
        // Check for incoming packets
        sockaddr_in src_addr;
        #ifdef _WIN32
            int addrlen = sizeof(src_addr);
        #else
            socklen_t addrlen = sizeof(src_addr);
        #endif
        int len = recvfrom(sock, buffer, sizeof(buffer)-1, 0, (sockaddr*)&src_addr, &addrlen);
        if (len > 0) {
            buffer[len] = '\0';
            string packet_data = string(buffer, len);
            cout << "[EVENT] Received packet: " << packet_data << endl;
            HandlePacket(packet_data, src_addr);
        }
        // Periodically send status updates for continuous parameters
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::milliseconds>(now - lastStatusUpdate).count() >= 500) {
            for (map<int, channel>::const_iterator it = channels.begin(); it != channels.end(); ++it) {
                int idx = it->first;
                const channel& ch = it->second;
                vector<string> continuousParams;
                for (json::const_iterator jt = ch.begin(); jt != ch.end(); ++jt) {
                    const std::string& key = jt.key();
                    if (key == "audioLevel" || key == "rfLevel" || key == "batteryLevel") {
                        continuousParams.push_back(key);
                    }
                }
                if (!continuousParams.empty()) {
                    MakeStatusUpdate(idx, continuousParams);
                }
            }
            lastStatusUpdate = now;
        }
        this_thread::sleep_for(chrono::milliseconds(10)); // Prevent busy loop
    }

    // Send mDNS goodbye packet (TTL=0) before shutdown
    {
        char buffer[2048];
        int sock_mdns = mdns_socket_open_ipv4(nullptr);
        if (sock_mdns >= 0) {
            mdns_socket_setup_ipv4(sock_mdns, nullptr);
            mdns_record_txt_t txt[4];
            txt[0].key = {"multicastAddress", strlen("multicastAddress")};
            txt[0].value = {MULTICAST_BASE.c_str(), MULTICAST_BASE.length()};
            txt[1].key = {"function", strlen("function")};
            txt[1].value = {"slave", strlen("slave")};
            txt[2].key = {"model", strlen("model")};
            txt[2].value = {deviceInfo.model.c_str(), deviceInfo.model.length()};
            txt[3].key = {"deviceType", strlen("deviceType")};
            txt[3].value = {deviceInfo.deviceType.c_str(), deviceInfo.deviceType.length()};
            mdns_record_t answer = {};
            answer.name = { (string(SLAVE_DANTE_NAME) + "._virgil._udp.local.").c_str(), strlen((string(SLAVE_DANTE_NAME) + "._virgil._udp.local.").c_str()) };
            answer.type = MDNS_RECORDTYPE_TXT;
            answer.data.txt.key = txt[0].key;
            answer.data.txt.value = txt[0].value;
            answer.rclass = MDNS_CLASS_IN;
            answer.ttl = 0; // Goodbye packet
            for (int i = 0; i < 4; ++i) {
                answer.data.txt.key = txt[i].key;
                answer.data.txt.value = txt[i].value;
                mdns_announce_multicast(sock_mdns, buffer, sizeof(buffer), answer, nullptr, 0, nullptr, 0);
            }
            mdns_socket_close(sock_mdns);
        }
    }
    CloseSocket(sock);
    mdns_running = false;
    if (mdns_thread.joinable()) mdns_thread.join();
    #ifdef _WIN32
        WSACleanup();
    #endif
    return 0;
}


