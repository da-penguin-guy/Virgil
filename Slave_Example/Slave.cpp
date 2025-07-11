
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
            cout << "[EVENT] mDNS advertisement failed" << endl;
        } else {
            cout << "[EVENT] mDNS advertisement sent" << endl;
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
    msg["messages"] = json::array();
    if (preampIndex == -1) {
        json dev;
        dev["model"]["value"] = deviceInfo.model;
        dev["model"]["locked"] = true;
        dev["deviceType"]["value"] = deviceInfo.deviceType;
        dev["deviceType"]["locked"] = true;
        dev["preampCount"] = deviceInfo.preampCount;
        msg["messages"].push_back(dev);
    } else if (preampIndex == -2) {
        // -2: include device-level info and all preamps
        json dev;
        dev["model"]["value"] = deviceInfo.model;
        dev["model"]["locked"] = true;
        dev["deviceType"]["value"] = deviceInfo.deviceType;
        dev["deviceType"]["locked"] = true;
        dev["preampCount"] = deviceInfo.preampCount;
        msg["messages"].push_back(dev);
        for (const Preamp& p : preamps) {
            msg["messages"].push_back(p);
        }
    } else if (preampIndex >= 0 && preampIndex < (int)preamps.size()) {
        const Preamp& p = preamps[preampIndex];
        msg["messages"].push_back(p);
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
    for (const auto& msg : j["messages"]) {
        cout << "Processing message: " << msg.dump(2) << endl;
        if (!msg.contains("messageType")) {
            cout << "Message has no type, skipping." << endl;
            continue;
        }
        string type = msg["messageType"];
        if (type == "ParameterRequest") {
            int idx = msg.value("preampIndex", -1);
            cout << "[EVENT] Received ParameterRequest for preampIndex " << idx << endl;
            json resp = MakeParameterResponse(idx);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(resp, fixed_src);
            cout << "[EVENT] Sent ParameterResponse" << endl;
        } else if (type == "StatusRequest") {
            int idx = msg.value("preampIndex", -1);
            cout << "[EVENT] Received StatusRequest for preampIndex " << idx << endl;
            json resp = MakeStatusUpdate(idx);
            sockaddr_in fixed_src = src;
            fixed_src.sin_port = htons(VIRGIL_PORT);
            SendUdp(resp, fixed_src);
            cout << "[EVENT] Sent StatusUpdate" << endl;
        } else if (type == "ParameterCommand") {
            int idx = msg.value("preampIndex", -1);
            cout << "[EVENT] Received ParameterCommand for preampIndex " << idx << endl;
            if (idx < 0 || idx >= (int)preamps.size()) {
                json err = MakeErrorResponse("PreampIndexInvalid", "Invalid preamp index");
                sockaddr_in fixed_src = src;
                fixed_src.sin_port = htons(VIRGIL_PORT);
                SendUdp(err, fixed_src);
                cout << "[EVENT] Sent ErrorResponse (invalid preamp index)" << endl;
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
            cout << string(buffer, len) << endl;
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


