
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include "nlohmann/json.hpp"
#include <thread>
#include <atomic>


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
    cout << "[DEBUG] Sending UDP response to " << ipstr << ":" << ntohs(dest.sin_port) << endl;
    cout << "[DEBUG] Response data: " << message.dump(2) << endl;
    
    int result = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&dest, sizeof(dest));
    if (result < 0) {
        cout << "[ERROR] Failed to send UDP response, error: " << result << endl;
    } else {
        cout << "[DEBUG] Successfully sent " << result << " bytes" << endl;
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
    sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&addr, sizeof(addr));
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
        if (res < 0) {
            cerr << "[EVENT] mDNS advertisement failed" << endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    CloseSocket(sock);
}

json MakeErrorResponse(const string& errorValue, const string& errorString) {
    json msg;
    msg["transmittingDevice"] = SLAVE_DANTE_NAME;
    msg["receivingDevice"] = "MasterDanteDeviceName";
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
    msg["receivingDevice"] = "MasterDanteDeviceName"; // Add receiving device
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
    msg["receivingDevice"] = "MasterDanteDeviceName";
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
    cout << "[DEBUG] Received packet from " << ipstr << ":" << ntohs(src.sin_port) << endl;
    cout << "[DEBUG] Raw data: " << data << endl;
    
    json j;
    try {
        j = json::parse(data);
        cout << "[DEBUG] Parsed JSON: " << j.dump(2) << endl;
    } catch (...) {
        cout << "[EVENT] Received invalid JSON" << endl;
        // Send error response for invalid JSON
        json err = MakeErrorResponse("MalformedMessage", "Invalid JSON format");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        cout << "[EVENT] Sent ErrorResponse (invalid JSON)" << endl;
        return;
    }
    if (!j.contains("messages")) {
        cout << "[EVENT] Malformed packet (no messages array)" << endl;
        // Send error response for malformed packet
        json err = MakeErrorResponse("MalformedMessage", "Missing 'messages' array");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        cout << "[EVENT] Sent ErrorResponse (malformed packet)" << endl;
        return;
    }
    if (!j.contains("transmittingDevice")) {
        cout << "[EVENT] Malformed packet (no transmittingDevice)" << endl;
        json err = MakeErrorResponse("MalformedMessage", "Missing 'transmittingDevice' field");
        sockaddr_in fixed_src = src;
        fixed_src.sin_port = htons(VIRGIL_PORT);
        SendUdp(err, fixed_src);
        cout << "[EVENT] Sent ErrorResponse (missing transmittingDevice)" << endl;
        return;
    }
    for (const auto& msg : j["messages"]) {
        cout << "Processing message: " << msg.dump(2) << endl;
        if (!msg.contains("messageType")) {
            cout << "Message has no type, skipping." << endl;
            continue;
        }
        string type = msg["messageType"];
        if (type == "ParameterRequest") {
            int idx = msg.value("channelIndex", -1);
            cout << "[EVENT] Received ParameterRequest for channelIndex " << idx << endl;
            json resp = MakeParameterResponse(idx);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(resp, fixed_src);
            cout << "[EVENT] Sent ParameterResponse" << endl;
        } else if (type == "StatusRequest") {
            int idx = msg.value("channelIndex", -1);
            cout << "[EVENT] Received StatusRequest for channelIndex " << idx << endl;
            if (idx < -1 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                cout << "[EVENT] Sent ErrorResponse (invalid channel index)" << endl;
                continue;
            }
            json resp = MakeStatusUpdate(idx);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(resp, fixed_src);
            cout << "[EVENT] Sent StatusUpdate" << endl;
        } else if (type == "ParameterCommand") {
            int idx = msg.value("channelIndex", -1);
            cout << "[EVENT] Received ParameterCommand for channelIndex " << idx << endl;
            if (!msg.contains("channelIndex")) {
                json err = MakeErrorResponse("MalformedMessage", "Missing channelIndex");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                cout << "[EVENT] Sent ErrorResponse (missing channelIndex)" << endl;
                continue;
            }
            if (idx < 0 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("ChannelIndexInvalid", "Invalid channel index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                cout << "[EVENT] Sent ErrorResponse (invalid channel index)" << endl;
                continue;
            }
            bool changed = false;
            lock_guard<mutex> lock(state_mutex);
            Preamp& p = preamps[idx];
            if (msg.contains("gain")) {
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
            // Add more parameter handling as needed
            if (changed) {
                cout << "[EVENT] Parameter changed, sending StatusUpdate" << endl;
                json update = MakeStatusUpdate(idx);
                // Multicast to all masters
                string mcast_ip = string(MULTICAST_BASE) + "." + to_string(idx);
                SendMulticast(update, mcast_ip);
            }
        }
    }
}

int main() {
    #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            cerr << "WSAStartup failed." << endl;
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
    cout << "[EVENT] Slave listening on UDP port " << VIRGIL_PORT << endl;
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
            // Debug: print all received UDP packets (raw)
            char ipstr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src_addr.sin_addr, ipstr, sizeof(ipstr));
            cout << "[DEBUG] Received UDP packet from " << ipstr << ":" << ntohs(src_addr.sin_port) << ":\n";
            HandlePacket(string(buffer, len), src_addr);
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


