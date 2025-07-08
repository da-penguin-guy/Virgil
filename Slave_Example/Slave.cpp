
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
constexpr char SLAVE_DANTE_NAME[] = "SlaveDanteDeviceName";


struct Preamp : public json {
    Preamp(int idx = 0) {
        (*this)["preampIndex"] = idx;
        (*this)["gain"] = {
            {"dataType", "int"}, {"unit", "dB"}, {"precision", 1}, {"value", 0}, {"minValue", -5}, {"maxValue", 50}, {"locked", false}
        };
        (*this)["pad"] = {
            {"dataType", "bool"}, {"value", false}, {"padLevel", -10}, {"locked", false}
        };
        (*this)["lowcut"] = {
            {"dataType", "int"}, {"unit", "Hz"}, {"precision", 1}, {"value", 0}, {"minValue", 0}, {"maxValue", 100}, {"locked", false}
        };
        (*this)["polarity"] = {
            {"dataType", "bool"}, {"value", false}, {"locked", false}
        };
        (*this)["phantomPower"] = {
            {"dataType", "bool"}, {"value", false}, {"locked", false}
        };
        (*this)["rfPower"] = {
            {"dataType", "int"}, {"unit", "dB"}, {"value", -60}, {"locked", true}
        };
        (*this)["rfEnable"] = {
            {"dataType", "bool"}, {"value", false}, {"locked", true}
        };
        (*this)["batteryLevel"] = {
            {"dataType", "int"}, {"unit", "%"}, {"value", 100}, {"locked", true}
        };
    }
    int index() const { return (*this)["preampIndex"]; }
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


void PrintEvent(const string& msg) {
    cout << "[EVENT] " << msg << endl;
}

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
    sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&dest, sizeof(dest));
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
            PrintEvent("mDNS advertisement failed");
        } else {
            PrintEvent("mDNS advertisement sent");
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    CloseSocket(sock);
}

json MakeStatusUpdate(int preampIndex) {
    lock_guard<mutex> lock(state_mutex);
    json msg;
    msg["transmittingDevice"] = SLAVE_DANTE_NAME;
    msg["messages"] = json::array();
    if (preampIndex >= 0 && preampIndex < (int)preamps.size()) {
        json m;
        m["messageType"] = "StatusUpdate";
        m.update(preamps[preampIndex]);
        msg["messages"].push_back(m);
    } else if (preampIndex == -1) {
        json m;
        m["messageType"] = "StatusUpdate";
        m["preampIndex"] = -1;
        m["model"] = deviceInfo.model;
        m["deviceType"] = deviceInfo.deviceType;
        msg["messages"].push_back(m);
    }
    return msg;
}

json MakeParameterResponse(int preampIndex) {
    lock_guard<mutex> lock(state_mutex);
    json msg;
    msg["transmittingDevice"] = SLAVE_DANTE_NAME;
    msg["receivingDevice"] = "MasterDanteDeviceName";
    msg["parameterResponses"] = json::array();
    if (preampIndex == -1) {
        json dev;
        dev["model"]["value"] = deviceInfo.model;
        dev["model"]["locked"] = true;
        dev["deviceType"]["value"] = deviceInfo.deviceType;
        dev["deviceType"]["locked"] = true;
        dev["preampCount"] = deviceInfo.preampCount;
        msg["parameterResponses"].push_back(dev);
    } else if (preampIndex >= 0 && preampIndex < (int)preamps.size()) {
        const Preamp& p = preamps[preampIndex];
        msg["parameterResponses"].push_back(p);
    }
    return msg;
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

void HandlePacket(const string& data, const sockaddr_in& src) {
    json j;
    try {
        j = json::parse(data);
    } catch (...) {
        PrintEvent("Received invalid JSON");
        // Send error response for invalid JSON
        json err = MakeErrorResponse("MalformedMessage", "Invalid JSON format");
        SendUdp(err, src);
        PrintEvent("Sent ErrorResponse (invalid JSON)");
        return;
    }
    if (!j.contains("messages")) {
        PrintEvent("Malformed packet (no messages array)");
        // Send error response for malformed packet
        json err = MakeErrorResponse("MalformedMessage", "Missing 'messages' array");
        SendUdp(err, src);
        PrintEvent("Sent ErrorResponse (malformed packet)");
        return;
    }
    for (const auto& msg : j["messages"]) {
        if (!msg.contains("messageType")) continue;
        string type = msg["messageType"];
        if (type == "ParameterRequest") {
            int idx = msg.value("preampIndex", -1);
            PrintEvent("Received ParameterRequest for preampIndex " + to_string(idx));
            json resp = MakeParameterResponse(idx);
            SendUdp(resp, src);
            PrintEvent("Sent ParameterResponse");
        } else if (type == "StatusRequest") {
            int idx = msg.value("preampIndex", -1);
            PrintEvent("Received StatusRequest for preampIndex " + to_string(idx));
            json resp = MakeStatusUpdate(idx);
            SendUdp(resp, src);
            PrintEvent("Sent StatusUpdate");
        } else if (type == "ParameterCommand") {
            int idx = msg.value("preampIndex", -1);
            PrintEvent("Received ParameterCommand for preampIndex " + to_string(idx));
            if (idx < 0 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("PreampIndexInvalid", "Invalid preamp index");
                SendUdp(err, src);
                PrintEvent("Sent ErrorResponse (invalid preamp index)");
                continue;
            }
            bool changed = false;
            lock_guard<mutex> lock(state_mutex);
            Preamp& p = preamps[idx];
            if (msg.contains("gain")) {
                int val = msg["gain"]["value"];
                if (val < p["gain"]["minValue"] || val > p["gain"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Gain out of range");
                    SendUdp(err, src);
                    PrintEvent("Sent ErrorResponse (gain out of range)");
                    continue;
                }
                p["gain"]["value"] = val;
                changed = true;
            }
            if (msg.contains("lowcut")) {
                int val = msg["lowcut"]["value"];
                if (val < p["lowcut"]["minValue"] || val > p["lowcut"]["maxValue"]) {
                    json err = MakeErrorResponse("ValueOutOfRange", "Lowcut out of range");
                    SendUdp(err, src);
                    PrintEvent("Sent ErrorResponse (lowcut out of range)");
                    continue;
                }
                p["lowcut"]["value"] = val;
                changed = true;
            }
            // Add more parameter handling as needed
            if (changed) {
                PrintEvent("Parameter changed, sending StatusUpdate");
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
    PrintEvent("Slave listening on UDP port " + to_string(VIRGIL_PORT));
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
    CloseSocket(sock);
    mdns_running = false;
    if (mdns_thread.joinable()) mdns_thread.join();
    #ifdef _WIN32
        WSACleanup();
    #endif
    return 0;
}


