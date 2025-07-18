
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

// Enhanced logging function
void log_detailed(const string& level, const string& category, const string& message, const string& data = "") {
    if (log_file.is_open()) {
        log_file << "[" << get_timestamp() << "] [" << level << "] [" << category << "] " << message;
        if (!data.empty()) {
            log_file << "\n    DATA: " << data;
        }
        log_file << endl;
        log_file.flush(); // Ensure immediate write to disk
    }
    
    // Also output to console for immediate feedback
    cout << "[" << get_timestamp() << "] [" << level << "] [" << category << "] " << message;
    if (!data.empty()) {
        cout << "\n    DATA: " << data;
    }
    cout << endl;
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
            {"dataType", "percent"}, {"precision", 1}, {"value", 85}, {"minValue", 0}, {"maxValue", 100}, {"locked", true}
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
    
    log_detailed("SEND", "UDP", "Sending UDP response", 
                "To: " + string(ipstr) + ":" + to_string(ntohs(dest.sin_port)) + 
                ", Size: " + to_string(payload.size()) + " bytes\n    Content:\n" + formatted_payload);
    
    int result = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&dest, sizeof(dest));
    if (result < 0) {
        log_detailed("ERROR", "UDP", "Failed to send UDP response", "Error: " + to_string(result));
    } else {
        log_detailed("DEBUG", "UDP", "Successfully sent UDP response", "Bytes sent: " + to_string(result));
    }
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
    
    log_detailed("SEND", "MULTICAST", "Sending multicast message", 
                "To: " + multicast_ip + ":" + to_string(VIRGIL_PORT) + 
                ", Size: " + to_string(payload.size()) + " bytes\n    Content:\n" + formatted_payload);
    
    int result = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&addr, sizeof(addr));
    if (result < 0) {
        log_detailed("ERROR", "MULTICAST", "Failed to send multicast", "Error: " + to_string(result));
    } else {
        log_detailed("DEBUG", "MULTICAST", "Successfully sent multicast", "Bytes sent: " + to_string(result));
    }
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

    log_detailed("INFO", "MDNS", "Starting mDNS advertisement", "Target: " + string(mdns_ip) + ":" + to_string(mdns_port));

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
        
        log_detailed("DEBUG", "MDNS", "Sending mDNS advertisement", 
                    "Size: " + to_string(payload.size()) + " bytes\n    Content:\n" + mdns_advert.dump(2));
        
        int res = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&addr, sizeof(addr));
        if (res < 0) {
            log_detailed("ERROR", "MDNS", "mDNS advertisement failed", "Error: " + to_string(res));
        } else {
            log_detailed("DEBUG", "MDNS", "mDNS advertisement sent", "Bytes sent: " + to_string(res));
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    CloseSocket(sock);
    log_detailed("INFO", "MDNS", "mDNS advertisement stopped", "");
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
    
    log_detailed("RECV", "UDP", "Received UDP packet", 
                "From: " + string(ipstr) + ":" + to_string(ntohs(src.sin_port)) + 
                ", Size: " + to_string(data.size()) + " bytes\n    Content:\n" + formatted_data);
    
    json j;
    try {
        j = json::parse(data);
    } catch (...) {
        log_detailed("ERROR", "JSON", "Received invalid JSON", "Raw data: " + data);
        // Send error response for invalid JSON
        json err = MakeErrorResponse("MalformedMessage", "Invalid JSON format");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        log_detailed("EVENT", "ERROR", "Sent ErrorResponse for invalid JSON", "");
        return;
    }
    if (!j.contains("messages")) {
        log_detailed("ERROR", "JSON", "Malformed packet - no messages array", "JSON: " + j.dump(2));
        // Send error response for malformed packet
        json err = MakeErrorResponse("MalformedMessage", "Missing 'messages' array");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        log_detailed("EVENT", "ERROR", "Sent ErrorResponse for malformed packet", "");
        return;
    }
    if (!j.contains("transmittingDevice")) {
        log_detailed("ERROR", "JSON", "Malformed packet - no transmittingDevice", "JSON: " + j.dump(2));
        json err = MakeErrorResponse("MalformedMessage", "Missing 'transmittingDevice' field");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        log_detailed("EVENT", "ERROR", "Sent ErrorResponse for missing transmittingDevice", "");
        return;
    }
    for (const auto& msg : j["messages"]) {
        if (!msg.contains("messageType")) {
            cout << "Message has no type, skipping." << endl;
            continue;
        }
        string type = msg["messageType"];
        log_detailed("INFO", "MESSAGE", "Processing message", "Type: " + type + ", Content: " + msg.dump(2));
        
        if (type == "ParameterRequest") {
            int idx = msg.value("channelIndex", -1);
            log_detailed("EVENT", "PARAM_REQ", "Received ParameterRequest", "channelIndex: " + to_string(idx));
            json resp = MakeParameterResponse(idx);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(resp, fixed_src);
            log_detailed("EVENT", "PARAM_REQ", "Sent ParameterResponse", "");
        } else if (type == "StatusRequest") {
            int idx = msg.value("channelIndex", -1);
            log_detailed("EVENT", "STATUS_REQ", "Received StatusRequest", "channelIndex: " + to_string(idx));
            if (idx < -1 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                log_detailed("EVENT", "STATUS_REQ", "Sent ErrorResponse for invalid channel index", "");
                continue;
            }
            json resp = MakeStatusUpdate(idx);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(resp, fixed_src);
            log_detailed("EVENT", "STATUS_REQ", "Sent StatusUpdate", "");
        } else if (type == "ParameterCommand") {
            int idx = msg.value("channelIndex", -1);
            log_detailed("EVENT", "PARAM_CMD", "Received ParameterCommand", "channelIndex: " + to_string(idx));
            if (!msg.contains("channelIndex")) {
                json err = MakeErrorResponse("MalformedMessage", "Missing channelIndex");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                log_detailed("EVENT", "PARAM_CMD", "Sent ErrorResponse for missing channelIndex", "");
                continue;
            }
            if (idx < 0 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                log_detailed("EVENT", "PARAM_CMD", "Sent ErrorResponse for invalid channel index", "");
                continue;
            }
            bool changed = false;
            lock_guard<mutex> lock(state_mutex);
            Preamp& p = preamps[idx];
            if (msg.contains("gain")) {
                if (!msg["gain"].contains("value") || !msg["gain"]["value"].is_number()) {
                    json err = MakeErrorResponse("InvalidValueType", "Gain value must be a number");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    cout << "[EVENT] Sent ErrorResponse (invalid gain type)" << endl;
                    continue;
                }
                int val = msg["gain"]["value"];
                if (val < p["gain"]["minValue"] || val > p["gain"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Gain out of range");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    cout << "[EVENT] Sent ErrorResponse (gain out of range)" << endl;
                    continue;
                }
                p["gain"]["value"] = val;
                changed = true;
            }
            if (msg.contains("lowcut")) {
                int val = msg["lowcut"]["value"];
                if (val < p["lowcut"]["minValue"] || val > p["lowcut"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Lowcut out of range");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    cout << "[EVENT] Sent ErrorResponse (lowcut out of range)" << endl;
                    continue;
                }
                p["lowcut"]["value"] = val;
                changed = true;
            }
            if (msg.contains("pad")) {
                bool val = msg["pad"]["value"];
                p["pad"]["value"] = val;
                changed = true;
            }
            if (msg.contains("polarity")) {
                bool val = msg["polarity"]["value"];
                p["polarity"]["value"] = val;
                changed = true;
            }
            if (msg.contains("phantomPower")) {
                bool val = msg["phantomPower"]["value"];
                p["phantomPower"]["value"] = val;
                changed = true;
            }
            if (msg.contains("lowcutEnable")) {
                bool val = msg["lowcutEnable"]["value"];
                p["lowcutEnable"]["value"] = val;
                changed = true;
            }
            if (msg.contains("rfEnable")) {
                bool val = msg["rfEnable"]["value"];
                p["rfEnable"]["value"] = val;
                changed = true;
            }
            if (msg.contains("squelch")) {
                int val = msg["squelch"]["value"];
                if (val < p["squelch"]["minValue"] || val > p["squelch"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Squelch out of range");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    cout << "[EVENT] Sent ErrorResponse (squelch out of range)" << endl;
                    continue;
                }
                p["squelch"]["value"] = val;
                changed = true;
            }
            if (msg.contains("transmitPower")) {
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
                    cout << "[EVENT] Sent ErrorResponse (invalid transmitPower)" << endl;
                    continue;
                }
                p["transmitPower"]["value"] = val;
                changed = true;
            }
            // Check for attempts to change locked parameters
            vector<string> locked_params = {"transmitterConnected", "subDevice", "audioLevel", "rfLevel", "batteryLevel"};
            for (const string& param : locked_params) {
                if (msg.contains(param)) {
                    json err = MakeErrorResponse("ParameterLocked", "Parameter " + param + " is locked");
                    sockaddr_in fixed_src = src;
                    fixed_src.sin_port = htons(VIRGIL_PORT);
                    SendUdp(err, fixed_src);
                    cout << "[EVENT] Sent ErrorResponse (parameter locked: " << param << ")" << endl;
                    continue;
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
                        cout << "[EVENT] Sent ErrorResponse (unsupported parameter: " << key << ")" << endl;
                        return;
                    }
                }
            }
            
            // Add more parameter handling as needed
            if (changed) {
                log_detailed("EVENT", "PARAM_CMD", "Parameters changed, sending StatusUpdate", "channelIndex: " + to_string(idx));
                json update = MakeStatusUpdate(idx);
                // Multicast to all masters
                string mcast_ip = string(MULTICAST_BASE) + "." + to_string(idx);
                SendMulticast(update, mcast_ip);
            }
        } else {
            // Handle unsupported message types
            log_detailed("EVENT", "MESSAGE", "Received unsupported message type", "Type: " + type);
            json err = MakeErrorResponse("UnrecognizedCommand", "Unsupported message type: " + type);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(err, fixed_src);
            log_detailed("EVENT", "MESSAGE", "Sent ErrorResponse for unsupported message type", "");
        }
    }
}

int main() {
    // Initialize log file
    log_file.open("slave_debug.log", ios::app);
    if (!log_file.is_open()) {
        cerr << "Warning: Could not open log file" << endl;
    }
    
    log_detailed("INFO", "STARTUP", "Slave starting up", "Version: Example Slave v1.0");
    
    #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            log_detailed("ERROR", "STARTUP", "WSAStartup failed", "Error code: " + to_string(WSAGetLastError()));
            return 0;
        }
        log_detailed("INFO", "STARTUP", "WSAStartup successful", "");
    #endif

    // Initialize preamps
    deviceInfo = DeviceInfo();
    preamps.clear();
    for (int i = 0; i < deviceInfo.preampCount; ++i) {
        preamps.emplace_back(i);
    }
    log_detailed("INFO", "STARTUP", "Initialized preamps", "Count: " + to_string(deviceInfo.preampCount));

    // Start mDNS advertisement in a background thread
    std::thread mdns_thread(AdvertiseMDNS);
    log_detailed("INFO", "STARTUP", "Started mDNS thread", "");

    // Create UDP socket for listening
    socket_t sock = CreateUdpSocket(VIRGIL_PORT);
    if (sock < 0) {
        log_detailed("ERROR", "STARTUP", "Failed to create UDP socket", "Port: " + to_string(VIRGIL_PORT));
        mdns_running = false;
        if (mdns_thread.joinable()) mdns_thread.join();
        return 0;
    }
    log_detailed("INFO", "STARTUP", "Slave listening on UDP port", "Port: " + to_string(VIRGIL_PORT));
    
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
            HandlePacket(string(buffer, len), src_addr);
        }
    }
    
    log_detailed("INFO", "SHUTDOWN", "Slave shutting down", "");
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


