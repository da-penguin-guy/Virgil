#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include "nlohmann/json.hpp"
#include <thread>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>


#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#endif

using json = nlohmann::json;
using namespace std;

// Global log file stream
ofstream log_file;

// Utility function to get current timestamp
string get_timestamp() {
    auto now = chrono::system_clock::now();
    auto time_t = chrono::system_clock::to_time_t(now);
    auto ms = chrono::duration_cast<chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    stringstream ss;
    ss << put_time(localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << setfill('0') << setw(3) << ms.count();
    return ss.str();
}


constexpr int VIRGIL_PORT = 7889;
constexpr char MULTICAST_BASE[] = "244.1.1";
constexpr char SLAVE_DANTE_NAME[] = "ExampleSlave";


struct Preamp : public json {
    Preamp(int idx = 0) {
        (*this)["channelIndex"] = idx;
        (*this)["gain"] = {
            {"dataType", "int"}, {"unit", "dB"}, {"precision", 1}, {"value", 0}, {"minValue", -5}, {"maxValue", 50}, {"locked", false}
        };
        (*this)["pad"] = {
            {"dataType", "bool"}, {"value", false}, {"padLevel", -10}, {"locked", false}
        };
        (*this)["lowcut"] = {
            {"dataType", "int"}, {"unit", "Hz"}, {"precision", 1}, {"value", 0}, {"minValue", 0}, {"maxValue", 100}, {"locked", false}
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
            {"dataType", "int"}, {"unit", "dB"}, {"precision", 1}, {"value", -60}, {"minValue", -80}, {"maxValue", -20}, {"locked", false}
        };
        (*this)["subDevice"] = {
            {"dataType", "string"}, {"value", "handheld"}, {"locked", true}
        };
        (*this)["audioLevel"] = {
            {"dataType", "float"}, {"unit", "dB"}, {"precision", 0.1}, {"value", -20.5}, {"locked", true}
        };
        (*this)["rfLevel"] = {
            {"dataType", "float"}, {"unit", "dB"}, {"precision", 0.1}, {"value", -45.2}, {"locked", true}
        };
        (*this)["batteryLevel"] = {
            {"dataType", "int"}, {"unit", "%"}, {"precision", 1}, {"value", 85}, {"minValue", 0}, {"maxValue", 100}, {"locked", true}
        };
    }
    int index() const { return (*this)["channelIndex"]; }
};

struct DeviceInfo {
    string model = "PreampModelName";
    string deviceType = "digitalStageBox";
    int preampCount = 3;
};

vector<Preamp> preamps;
DeviceInfo deviceInfo;
mutex state_mutex;

std::atomic<bool> mdns_running{true};




socket_t CreateUdpSocket(int port) {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "Socket creation failed." << endl;
        return -1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Bind failed." << endl;
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    return sock;
}

void CloseSocket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

void SendUdp(const json& message, const sockaddr_in& dest) {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    string payload = message.dump();
    
    // Log the response being sent
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dest.sin_addr, ipstr, sizeof(ipstr));
    
    string formatted_payload = payload;
    // Try to format JSON for logging
    try {
        auto j = json::parse(payload);
        formatted_payload = j.dump(2);
    } catch (...) {
        // If parsing fails, use original payload
    }
    int result = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&dest, sizeof(dest));
    CloseSocket(sock);
}

void SendMulticast(const json& message, const string& multicast_ip) {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VIRGIL_PORT);
    inet_pton(AF_INET, multicast_ip.c_str(), &addr.sin_addr);
    string payload = message.dump();
    
    string formatted_payload = payload;
    // Try to format JSON for logging
    try {
        auto j = json::parse(payload);
        formatted_payload = j.dump(2);
    } catch (...) {
        // If parsing fails, use original payload
    }
    
    int result = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&addr, sizeof(addr));
    CloseSocket(sock);
}

void AdvertiseMDNS() {
    const char* mdns_ip = "224.0.0.251";
    int mdns_port = 5353;
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(mdns_port);
    inet_pton(AF_INET, mdns_ip, &addr.sin_addr);

    while (mdns_running) {
        json mdns_advert;
        mdns_advert["serviceName"] = string(SLAVE_DANTE_NAME) + "." + "_virgil._udp.local.";
        mdns_advert["serviceType"] = "_virgil._udp.local.";
        mdns_advert["port"] = VIRGIL_PORT;
        mdns_advert["txt"]["multicast"] = MULTICAST_BASE;
        mdns_advert["txt"]["function"] = "slave";
        mdns_advert["txt"]["model"] = deviceInfo.model;
        mdns_advert["txt"]["deviceType"] = deviceInfo.deviceType;
        string payload = mdns_advert.dump();
        
        int res = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&addr, sizeof(addr));
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    CloseSocket(sock);
}

json MakeErrorResponse(const string& errorValue, const string& errorString) {
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

json MakeStatusUpdate(int channelIndex) {
    lock_guard<mutex> lock(state_mutex);
    json msg;
    msg["transmittingDevice"] = SLAVE_DANTE_NAME;
    msg["messages"] = json::array();
    if (channelIndex >= 0 && channelIndex < (int)preamps.size()) {
        json m;
        m["messageType"] = "StatusUpdate";
        m["channelIndex"] = channelIndex;
        // Copy all parameters from the preamp except channelIndex to avoid duplication
        for (auto& [key, value] : preamps[channelIndex].items()) {
            if (key != "channelIndex") {
                m[key] = value;
            }
        }
        msg["messages"].push_back(m);
    } else if (channelIndex == -1) {
        // Send status for all channels
        for (int i = 0; i < (int)preamps.size(); ++i) {
            json m;
            m["messageType"] = "StatusUpdate";
            m["channelIndex"] = i;
            for (auto& [key, value] : preamps[i].items()) {
                if (key != "channelIndex") {
                    m[key] = value;
                }
            }
            msg["messages"].push_back(m);
        }
    }
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
        dev["virgilVersion"] = "1.0";
        json channelIndices = json::array();
        for (int i = 0; i < deviceInfo.preampCount; ++i) {
            channelIndices.push_back(i);
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
        dev["virgilVersion"] = "1.0";
        json channelIndices = json::array();
        for (int i = 0; i < deviceInfo.preampCount; ++i) {
            channelIndices.push_back(i);
        }
        dev["channelIndices"] = channelIndices;
        msg["messages"].push_back(dev);
        
        for (const Preamp& p : preamps) {
            json channel_msg;
            channel_msg["messageType"] = "ParameterResponse";
            channel_msg["channelIndex"] = p.index();
            // Copy all parameters except channelIndex to avoid duplication
            for (auto& [key, value] : p.items()) {
                if (key != "channelIndex") {
                    channel_msg[key] = value;
                }
            }
            msg["messages"].push_back(channel_msg);
        }
    } else if (channelIndex >= 0 && channelIndex < (int)preamps.size()) {
        // Single channel response
        json p_msg;
        p_msg["messageType"] = "ParameterResponse";
        p_msg["channelIndex"] = channelIndex;
        // Copy all parameters except channelIndex to avoid duplication
        for (auto& [key, value] : preamps[channelIndex].items()) {
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
    for (const auto& msg : j["messages"]) {
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
        } else if (type == "StatusRequest") {
            int idx = msg.value("channelIndex", -1);
            if (idx < -1 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                continue;
            }
            json resp = MakeStatusUpdate(idx);
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
            if (idx < 0 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                continue;
            }
            bool changed = false;
            lock_guard<mutex> lock(state_mutex);
            Preamp& p = preamps[idx];
            
            // Check for attempts to change locked parameters first
            vector<string> locked_params = {"transmitterConnected", "subDevice", "audioLevel", "rfLevel", "batteryLevel"};
            for (const string& param : locked_params) {
                if (msg.contains(param)) {
                    json err = MakeErrorResponse("ParameterLocked", "Parameter " + param + " is locked");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
            }
            
            // Check for unsupported parameters
            vector<string> supported_params = {"gain", "lowcut", "lowcutEnable", "pad", "phantomPower", "polarity", "rfEnable", "transmitPower", "squelch", "transmitterConnected", "subDevice", "audioLevel", "rfLevel", "batteryLevel"};
            for (auto& [key, value] : msg.items()) {
                if (key != "messageType" && key != "channelIndex") {
                    if (find(supported_params.begin(), supported_params.end(), key) == supported_params.end()) {
                        json err = MakeErrorResponse("ParameterUnsupported", "Parameter " + key + " is not supported");
                        sockaddr_in fixed_src = src;
                        fixed_src.sin_port = htons(VIRGIL_PORT);
                        SendUdp(err, fixed_src);
                        return;
                    }
                }
            }
            if (msg.contains("gain")) {
                if (!msg["gain"].is_object() || !msg["gain"].contains("value") || !msg["gain"]["value"].is_number()) {
                    json err = MakeErrorResponse("InvalidValueType", "Gain value must be a number");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                int val = msg["gain"]["value"];
                if (val < p["gain"]["minValue"] || val > p["gain"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Gain out of range");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                p["gain"]["value"] = val;
                changed = true;
            }
            if (msg.contains("lowcut")) {
                if (!msg["lowcut"].is_object() || !msg["lowcut"].contains("value") || !msg["lowcut"]["value"].is_number()) {
                    json err = MakeErrorResponse("InvalidValueType", "Lowcut value must be a number");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                int val = msg["lowcut"]["value"];
                if (val < p["lowcut"]["minValue"] || val > p["lowcut"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Lowcut out of range");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                p["lowcut"]["value"] = val;
                changed = true;
            }
            if (msg.contains("pad")) {
                if (!msg["pad"].is_object() || !msg["pad"].contains("value") || !msg["pad"]["value"].is_boolean()) {
                    json err = MakeErrorResponse("InvalidValueType", "Pad value must be a boolean");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                bool val = msg["pad"]["value"];
                p["pad"]["value"] = val;
                changed = true;
            }
            if (msg.contains("polarity")) {
                if (!msg["polarity"].is_object() || !msg["polarity"].contains("value") || !msg["polarity"]["value"].is_boolean()) {
                    json err = MakeErrorResponse("InvalidValueType", "Polarity value must be a boolean");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                bool val = msg["polarity"]["value"];
                p["polarity"]["value"] = val;
                changed = true;
            }
            if (msg.contains("phantomPower")) {
                if (!msg["phantomPower"].is_object() || !msg["phantomPower"].contains("value") || !msg["phantomPower"]["value"].is_boolean()) {
                    json err = MakeErrorResponse("InvalidValueType", "PhantomPower value must be a boolean");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                bool val = msg["phantomPower"]["value"];
                p["phantomPower"]["value"] = val;
                changed = true;
            }
            if (msg.contains("lowcutEnable")) {
                if (!msg["lowcutEnable"].is_object() || !msg["lowcutEnable"].contains("value") || !msg["lowcutEnable"]["value"].is_boolean()) {
                    json err = MakeErrorResponse("InvalidValueType", "LowcutEnable value must be a boolean");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                bool val = msg["lowcutEnable"]["value"];
                p["lowcutEnable"]["value"] = val;
                changed = true;
            }
            if (msg.contains("rfEnable")) {
                if (!msg["rfEnable"].is_object() || !msg["rfEnable"].contains("value") || !msg["rfEnable"]["value"].is_boolean()) {
                    json err = MakeErrorResponse("InvalidValueType", "RfEnable value must be a boolean");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                bool val = msg["rfEnable"]["value"];
                p["rfEnable"]["value"] = val;
                changed = true;
            }
            if (msg.contains("squelch")) {
                if (!msg["squelch"].is_object() || !msg["squelch"].contains("value") || !msg["squelch"]["value"].is_number()) {
                    json err = MakeErrorResponse("InvalidValueType", "Squelch value must be a number");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                int val = msg["squelch"]["value"];
                if (val < p["squelch"]["minValue"] || val > p["squelch"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Squelch out of range");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                p["squelch"]["value"] = val;
                changed = true;
            }
            if (msg.contains("transmitPower")) {
                if (!msg["transmitPower"].is_object() || !msg["transmitPower"].contains("value") || !msg["transmitPower"]["value"].is_string()) {
                    json err = MakeErrorResponse("InvalidValueType", "TransmitPower value must be a string");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                string val = msg["transmitPower"]["value"];
                // Validate enum value
                bool valid = false;
                for (const auto& enumVal : p["transmitPower"]["enumValues"]) {
                    if (enumVal == val) {
                        valid = true;
                        break;
                    }
                }
                if (!valid) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Invalid transmitPower value");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    return; // Exit completely
                }
                p["transmitPower"]["value"] = val;
                changed = true;
            }
            
            // Add more parameter handling as needed
            if (changed) {
                json update = MakeStatusUpdate(idx);
                // Multicast to all masters
                string mcast_ip = string(MULTICAST_BASE) + "." + to_string(idx);
                SendMulticast(update, mcast_ip);
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
    // Initialize log file
    log_file.open("slave_debug.log", ios::app);
    if (!log_file.is_open()) {
        cerr << "Warning: Could not open log file" << endl;
    }
    
    
    #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            return 0;
        }
    #endif

    // Initialize preamps
    deviceInfo = DeviceInfo();
    preamps.clear();
    for (int i = 0; i < deviceInfo.preampCount; ++i) {
        preamps.emplace_back(i);
    }

    // Start mDNS advertisement in a background thread
    std::thread mdns_thread(AdvertiseMDNS);

    // Create UDP socket for listening
    socket_t sock = CreateUdpSocket(VIRGIL_PORT);
    if (sock < 0) {
        mdns_running = false;
        if (mdns_thread.joinable()) mdns_thread.join();
        return 0;
    }
    
    char buffer[4096];
    while (true) {
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
    }
    
    CloseSocket(sock);
    mdns_running = false;
    if (mdns_thread.joinable()) mdns_thread.join();
    #ifdef _WIN32
        WSACleanup();
    #endif
    
    if (log_file.is_open()) {
        log_file.close();
    }
    return 0;
}


