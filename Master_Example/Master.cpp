#include <iostream>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>
#include "nlohmann/json.hpp"

#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_IMPLEMENTATION

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "nuklear/nuklear.h"
#define NK_GLFW_GL3_IMPLEMENTATION
#include "nuklear/demo/glfw_opengl3/nuklear_glfw_gl3.h"

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

// This function will be called with received data and sender info
using PacketHandler = function<void(const string&, const sockaddr_in&)>;

// Forward declarations
socket_t CreateSocket(int type, int port, sockaddr_in& addr);
void CloseSocket(socket_t& sock);
void MDNSWorkerThread();
void StartMDNSWorker(const string& danteName, const string& function, const string& multicastBase = "");
void StopMDNSWorker();
bool SendUDP(const string& ip, int port, const json& message);
bool SendMulticast(const string& multicast_ip, int port, const json& message);
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

// This function will be called with received data and sender info
using PacketHandler = function<void(const string&, const sockaddr_in&)>;

int virgilPort = 7889;
//Here, we have a preexisting list of Dante devices we are subscribed to
//You would get this from your preexisting Dante code
//I'm just making up a format to make my life easier
map<string,json> danteLookup = {{"ExampleSlave",{}}}; // Preexisting Dante device list; add more as needed

// Custom streambuf to redirect cout to Nuklear log buffer
class NuklearLogBuf : public streambuf {
public:
    NuklearLogBuf(char* buf, int bufsize, int* len_ptr, streambuf* orig_buf)
        : buffer(buf), bufsize(bufsize), len_ptr(len_ptr), orig_buf(orig_buf) {}
protected:
    int overflow(int c) override {
        if (c == EOF) return 0;
        if (*len_ptr < bufsize - 1) {
            buffer[(*len_ptr)++] = (char)c;
            buffer[*len_ptr] = '\0';
        }
        if (orig_buf) orig_buf->sputc(c);
        return c;
    }
    streamsize xsputn(const char* s, streamsize n) override {
        int to_copy = (int)min<streamsize>(n, bufsize - 1 - *len_ptr);
        if (to_copy > 0) {
            memcpy(buffer + *len_ptr, s, to_copy);
            *len_ptr += to_copy;
            buffer[*len_ptr] = '\0';
        }
        if (orig_buf) orig_buf->sputn(s, n);
        return n;
    }
private:
    char* buffer;
    int bufsize;
    int* len_ptr;
    streambuf* orig_buf;
};



socket_t CreateSocket(int type, int port, sockaddr_in& addr) {
    socket_t sock = socket(AF_INET, type, 0);
    if (sock < 0) 
    {
        cerr << "Socket creation failed.\n";
        return INVALID_SOCKET; // Return invalid socket instead of continuing
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

void CloseSocket(socket_t& sock) {
    #ifdef _WIN32
        closesocket(sock);
    #else   
        close(sock);
    #endif
}


void MDNSWorkerThread() {
    this_thread::sleep_for(chrono::seconds(1)); // Wait for gui to initialize
    cout << "Starting mDNS scanner...\n";
    const char* mdns_ip = "224.0.0.251";
    int mdns_port = 5353;
    string danteName = mdns_worker_config.danteName;
    string function = mdns_worker_config.function;
    string multicastBase = mdns_worker_config.multicastBase;
    string serviceType = "_virgil._udp.local.";

    // Prepare advertisement payload
    json mdns_advert;
    mdns_advert["serviceName"] = danteName + "." + serviceType;
    mdns_advert["serviceType"] = serviceType;
    mdns_advert["port"] = virgilPort;
    if(multicastBase.empty() && function == "slave") {
        cerr << "[MDNSWorkerThread] Slaves must have a multicast base\n";
        return;
    } else if(function == "slave") {
        mdns_advert["txt"]["multicast"] = multicastBase;
    }
    mdns_advert["txt"]["function"] = function;
    mdns_advert["txt"]["model"] = "virgilExample";
    mdns_advert["txt"]["deviceType"] = "computer";
    string payload = mdns_advert.dump();

    // Setup socket for both send and receive
    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_DGRAM, mdns_port, addr);
    if (sock == INVALID_SOCKET) {
        cerr << "[MDNSWorkerThread] mDNS socket creation failed.\n";
        return;
    }
    if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "[MDNSWorkerThread] mDNS bind failed.\n";
        CloseSocket(sock);
        return;
    }
    ip_mreq mreq;
    inet_pton(AF_INET, mdns_ip, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    int setopt_result = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));


    // Set destination address for sending
    sockaddr_in send_addr = {};
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(mdns_port);
    inet_pton(AF_INET, mdns_ip, &send_addr.sin_addr);

    char buffer[4096];
    map<string, chrono::steady_clock::time_point> lastSeen;
    const chrono::seconds offlineTimeout(10);
    auto last_advertise = chrono::steady_clock::now() - chrono::seconds(2);
    while (mdns_worker_running) {
        // 1. Periodically advertise
        auto now = chrono::steady_clock::now();
        if (now - last_advertise > chrono::seconds(1)) {
            int res = sendto(sock, payload.c_str(), payload.size(), 0, (sockaddr*)&send_addr, sizeof(send_addr));
            if (res < 0) {
                cerr << "[MDNSWorkerThread] mDNS advertisement failed.\n";
            }
            last_advertise = now;
        }
        // 2. Poll for incoming packets
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000; // 0.2s
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
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
                // Only print JSON parse error if it looks like JSON
                bool looks_like_json = (buffer[0] == '{' || buffer[0] == '[');
                char ipstr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &src_addr.sin_addr, ipstr, sizeof(ipstr));
                json j;
                try {
                    j = json::parse(buffer, buffer + len);
                } catch (...) {
                    if (looks_like_json) {
                        cerr << "[MDNSWorkerThread] JSON parse error from " << ipstr << endl;
                    }
                    continue;
                }
                if (
                    j.contains("serviceType") && j["serviceType"].is_string() && j["serviceType"] == "_virgil._udp.local."
                    && j.contains("serviceName") && j["serviceName"].is_string()
                    && j.contains("txt") && j["txt"].is_object()
                    && j["txt"].contains("function") && j["txt"]["function"].is_string() && j["txt"]["function"] == "slave"
                    && j["txt"].contains("multicast") && j["txt"]["multicast"].is_string()
                ) {
                    string serviceName = j["serviceName"];
                    string serviceType = j["serviceType"];
                    string danteName = serviceName.erase(serviceName.find(serviceType), serviceName.length());
                    if (!danteName.empty() && danteName.back() == '.') {
                        danteName.pop_back();
                    }
                    auto now2 = chrono::steady_clock::now();
                    lock_guard<mutex> lock(danteLookup_mutex);
                    if (danteLookup.find(danteName) != danteLookup.end()) {
                        auto& device = danteLookup[danteName];
                        device["name"] = danteName;
                        device["multicast"] = j["txt"]["multicast"];
                        device["virgil"] = true;
                        // ...existing code...
                        inet_ntop(AF_INET, &src_addr.sin_addr, ipstr, sizeof(ipstr));
                        if(device.contains("ip") && device["ip"] != string(ipstr)) {
                            cout << "IP changed for device: " << danteName << " from " << device["ip"] << " to " << ipstr << endl;
                        }
                        device["ip"] = string(ipstr);
                        if (!device.value("isFound", false)) {
                            cout << "Device found: " << danteName << endl;
                            SendParameterRequest(device);
                        }
                        device["isFound"] = true;
                        lastSeen[danteName] = now2;
                    }
                }
            }
        }
        // 3. Check for offline devices every second
        static auto lastCheck = chrono::steady_clock::now();
        now = chrono::steady_clock::now();
        if (now - lastCheck > chrono::seconds(1)) {
            for (const auto& [danteName, seenTime] : lastSeen) {
                if (now - seenTime > offlineTimeout) {
                    cout << "Device offline: " << danteName << endl;
                    auto& device = danteLookup[danteName];
                    device["isFound"] = false;
                }
            }
            lastCheck = now;
        }
        this_thread::sleep_for(chrono::milliseconds(50));
    }
    CloseSocket(sock);
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

bool SendMulticast(const string& multicast_ip, int port, const json& message) {
    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_DGRAM, port, addr);
    
    if (sock == INVALID_SOCKET) {
        cerr << "SendMulticast socket creation failed.\n";
        return false;
    }

    // Set the destination multicast IP
    if (inet_pton(AF_INET, multicast_ip.c_str(), &addr.sin_addr) != 1) {
        cerr << "Invalid multicast IP address: " << multicast_ip << endl;
        CloseSocket(sock);
        return false;
    }

    string messageDump = message.dump();
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

void NetListener(int port, const vector<string> multicast_ips, PacketHandler handler){
    this_thread::sleep_for(chrono::seconds(1)); // Wait for gui to initialize
    // --- Multicast UDP socket setup ---
    sockaddr_in udp_addr;
    socket_t udp_sock = CreateSocket(SOCK_DGRAM, port, udp_addr);
    
    if (udp_sock == INVALID_SOCKET) {
        cerr << "NetListener socket creation failed.\n";
        return;
    }

    if (::bind(udp_sock, (sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) 
    {
        cerr << "UDP bind failed.\n";
        CloseSocket(udp_sock);
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
        FD_SET(udp_sock, &readfds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(udp_sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) continue;

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

    CloseSocket(udp_sock);
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
    request["messages"].push_back({{"messageType", "ParameterRequest"}, {"preampIndex", -2}});
    if (!SendUDP(device["ip"], virgilPort, request)) {
        cerr << "[SendParameterRequest] Failed to send ParameterRequest to device: " << device["name"] << endl;
    }
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
        if (messageType == "StatusUpdate" || messageType == "StatusResponse" || messageType == "ParameterResponse") {
            // Handle device-level responses (preampIndex -1 only)
            if (message.contains("preampIndex") && int(message["preampIndex"]) == -1) {
                // Device-level info: update device fields, not preamp
                json msgCopy = message;
                msgCopy.erase("preampIndex");
                msgCopy.erase("messageType");
                device.update(msgCopy);
            }
            // Ignore responses with preampIndex -2 (should never occur)
            else if (message.contains("preampIndex") && int(message["preampIndex"]) == -2) {
                cerr << "Warning: Received response with preampIndex -2 (invalid per protocol). Ignoring.\n";
            }
            // Handle preamp-level responses (preampIndex >= 0)
            else if (message.contains("preampIndex")) {
                int idx = message["preampIndex"];
                if (idx >= 0) {
                    // Ensure 'preamps' array exists
                    if (!device.contains("preamps") || !device["preamps"].is_array()) {
                        device["preamps"] = json::array();
                    }
                    // Resize array if needed
                    while ((int)device["preamps"].size() <= idx) {
                        device["preamps"].push_back(json::object());
                    }
                    json msgCopy = message;
                    msgCopy.erase("messageType");
                    device["preamps"][idx].update(msgCopy);
                }
            }
        }
        // Optionally handle errors, info, etc. here
        else if (messageType == "error") {
            cerr << "Received error from device: " << txDevice << ": " << message.dump() << endl;
        }
    }
}

void CleanupAndExit(int code) {
    #ifdef _WIN32
    WSACleanup();
    #endif
    StopNetListener();
    StopMDNSWorker();
    exit(code);
}

void SetupWindow() {
    if (!glfwInit()) {
        cerr << "Failed to initialize GLFW" << endl;
        return;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow* win = glfwCreateWindow(900, 700, "Master Controller", nullptr, nullptr);
    if (!win) {
        cerr << "Failed to create GLFW window" << endl;
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(win);
    // Initialize GLEW after context is current
    if (glewInit() != GLEW_OK) {
        cerr << "Failed to initialize GLEW" << endl;
        glfwDestroyWindow(win);
        glfwTerminate();
        return;
    }
    struct nk_context* ctx = nk_glfw3_init(win, NK_GLFW3_INSTALL_CALLBACKS);
    if (!ctx) {
        cerr << "Failed to initialize Nuklear context" << endl;
        glfwDestroyWindow(win);
        glfwTerminate();
        return;
    }

    /* Font setup: use Raleway-Bold.ttf from extra_font */
    struct nk_font_atlas* atlas;
    nk_glfw3_font_stash_begin(&atlas);
    struct nk_font* rfont = nullptr;
    
    // Try to load custom font, fall back to default if it fails
    try {
        rfont = nk_font_atlas_add_from_file(atlas, "nuklear/extra_font/Raleway-Bold.ttf", 16.0f, 0);
    } catch (...) {
        cerr << "Warning: Could not load custom font, using default" << endl;
    }
    
    nk_glfw3_font_stash_end();
    if (rfont) {
        nk_style_set_font(ctx, &rfont->handle);
    } else {
        cout << "Using default font" << endl;
    }

    struct nk_colorf bg;
    bg.r = 0.10f; bg.g = 0.18f; bg.b = 0.24f; bg.a = 1.0f;

    // Simple static log buffer for demonstration
    static char log_buffer[2048] = "";
    static int log_len = (int)strlen(log_buffer);

    // Redirect cout to Nuklear log
    streambuf* old_cout = cout.rdbuf();
    static NuklearLogBuf logbuf(log_buffer, sizeof(log_buffer), &log_len, old_cout);
    cout.rdbuf(&logbuf);

    static int selected_device = 0;
    static int selected_preamp = 0;
    vector<string> device_names;
    while (!glfwWindowShouldClose(win)) {
        // Update device_names every frame, only include devices where isFound is true
        device_names.clear();
        {
            lock_guard<mutex> lock(danteLookup_mutex);
            for (const auto& pair : danteLookup) {
                if (pair.second.is_object() && pair.second.value("isFound", false)) {
                    device_names.push_back(pair.first);
                }
            }
        }
        glfwPollEvents();
        nk_glfw3_new_frame();
        int width, height;
        glfwGetFramebufferSize(win, &width, &height);
        if (nk_begin(ctx, "ButtonWindow", nk_rect(0, 0, (float)width, (float)height), NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BORDER)) {
            float half_width = width * 0.5f;
            float left_width = half_width;
            float right_width = width - half_width;
            float combo_height = 36.0f;
            float group_height = (float)height;

            // Layout: one row, two columns (left/right)
            float col_widths[2] = { left_width, right_width };
            nk_layout_row(ctx, NK_STATIC, group_height, 2, col_widths);

            // --- Left column ---
            if (nk_group_begin(ctx, "LeftCol", NK_WINDOW_NO_SCROLLBAR)) {
                // Combo box at the top
                nk_layout_row_static(ctx, combo_height, left_width - 20, 1);
                if (!device_names.empty()) {
                    if (selected_device >= (int)device_names.size()) selected_device = 0;
                    vector<const char*> device_cstrs;
                    for (const auto& name : device_names) device_cstrs.push_back(name.c_str());
                    selected_device = nk_combo(ctx, device_cstrs.data(), (int)device_cstrs.size(), selected_device, 20, nk_vec2(left_width - 20, 100));

                    // --- Preamp dropdown ---
                    // Find selected device's preamps
                    lock_guard<mutex> lock(danteLookup_mutex);
                    const string& dev_name = device_names[selected_device];
                    auto it = danteLookup.find(dev_name);
                    if (it != danteLookup.end() && it->second.contains("preamps") && it->second["preamps"].is_array()) {
                        const auto& preamps = it->second["preamps"];
                        int preamp_count = (int)preamps.size();
                        if (preamp_count > 0) {
                            if (selected_preamp >= preamp_count) selected_preamp = 0;
                            vector<string> preamp_labels;
                            for (int i = 0; i < preamp_count; ++i) {
                                // Use preampIndex if available, else just index
                                int idx = i;
                                if (preamps[i].contains("preampIndex")) idx = preamps[i]["preampIndex"];
                                preamp_labels.push_back("Preamp " + to_string(idx));
                            }
                            vector<const char*> preamp_cstrs;
                            for (const auto& s : preamp_labels) preamp_cstrs.push_back(s.c_str());
                            nk_layout_row_static(ctx, combo_height, left_width - 20, 1);
                            selected_preamp = nk_combo(ctx, preamp_cstrs.data(), preamp_count, selected_preamp, 20, nk_vec2(left_width - 20, 100));
                        }
                    }
                } else {
                    nk_label(ctx, "No devices", NK_TEXT_LEFT);
                }
                nk_group_end(ctx);
            }

            // --- Right column ---
            if (nk_group_begin(ctx, "RightCol", NK_WINDOW_NO_SCROLLBAR)) {
                // Log/console fills the right column
                nk_layout_row_dynamic(ctx, (float)height - 20 - 24, 1);
                nk_edit_string_zero_terminated(ctx, NK_EDIT_READ_ONLY|NK_EDIT_BOX, log_buffer, (int)sizeof(log_buffer), nk_filter_default);
                nk_group_end(ctx);
            }
        }
        nk_end(ctx);
        glViewport(0, 0, width, height);
        glClearColor(bg.r, bg.g, bg.b, bg.a);
        glClear(GL_COLOR_BUFFER_BIT);
        nk_glfw3_render(NK_ANTI_ALIASING_ON, 512 * 1024, 128 * 1024);
        glfwSwapBuffers(win);
    }
    nk_glfw3_shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    // Restore cout
    cout.rdbuf(old_cout);

    // Close the program when the window is closed
    CleanupAndExit(0);
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
    StartNetListener(virgilPort, vector<string>{}, ProcessPacket);
    StartMDNSWorker("ExampleMaster", "master");
    
    SetupWindow();
    
    CleanupAndExit(0);
}

