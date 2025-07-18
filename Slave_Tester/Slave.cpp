#include <set>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include "nlohmann/json.hpp"


using namespace std;
using json = nlohmann::json;
using namespace chrono_literals;

constexpr int VIRGIL_PORT = 7889;
constexpr int BUF_SIZE = 4096;

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
}


struct DeviceIdentity {
    string danteName;
    string model;
    string deviceType;
    vector<int> channelIndices;
    bool initialized = false;
};

void scan_mdns(const string& expected_ip, DeviceIdentity& identity) {
    cout << "\n--- mDNS Scan for Virgil Devices ---\n";
    log_detailed("INFO", "MDNS", "Starting mDNS scan for Virgil devices", "Expected IP: " + expected_ip);
    
    // Initialize Winsock for mDNS
    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        cerr << "[mDNS] WSAStartup failed with error: " << wsa_result << "\n";
        log_detailed("ERROR", "MDNS", "WSAStartup failed", "Error code: " + to_string(wsa_result));
        return;
    }
    
    const char* mdns_ip = "224.0.0.251";
    int mdns_port = 5353;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        int error = WSAGetLastError();
        cerr << "[mDNS] Socket creation failed with WSA error: " << error << "\n";
        log_detailed("ERROR", "MDNS", "Socket creation failed", "WSA Error: " + to_string(error));
        WSACleanup();
        return;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(mdns_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    // Set socket options for multicast
    BOOL reuse = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    
    if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        cerr << "[mDNS] Bind failed with error: " << error << ". Continuing without mDNS scan.\n";
        log_detailed("WARN", "MDNS", "Bind failed, skipping mDNS scan", 
            "WSA Error: " + to_string(error) + " (Port 5353 may be in use by another service like Windows DNS Client or Bonjour)");
        closesocket(sock);
        WSACleanup();
        cout << "[mDNS] Skipping mDNS discovery due to port conflict. Proceeding with protocol tests.\n";
        return;
    }
    ip_mreq mreq;
    inet_pton(AF_INET, mdns_ip, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));

    char buffer[4096];
    set<string> seen;
    auto start = chrono::steady_clock::now();
    const set<string> allowed_device_types = {"digitalStageBox", "wirelessReceiver", "wirelessTransmitter", "wirelessCombo", "mixer", "dsp", "computer"};
    while (chrono::steady_clock::now() - start < chrono::seconds(5)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout{0, 500000}; // 0.5s
        int ready = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(sock, &readfds)) {
            sockaddr_in src_addr;
            int addrlen = sizeof(src_addr);
            int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&src_addr, &addrlen);
            if (len > 0) {
                buffer[len] = '\0';
                char ipstr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &src_addr.sin_addr, ipstr, sizeof(ipstr));
                string src_ip(ipstr);
                if (src_ip != expected_ip) continue;
                
                log_detailed("DEBUG", "MDNS", "Received mDNS packet", 
                    "From: " + src_ip + ", Size: " + to_string(len) + " bytes\n    Raw data: " + string(buffer, len));
            
                try {
                    auto j = json::parse(buffer, buffer + len);
                    if (
                        j.contains("serviceType") && j["serviceType"].is_string() && j["serviceType"] == "_virgil._udp.local."
                        && j.contains("serviceName") && j["serviceName"].is_string()
                        && j.contains("txt") && j["txt"].is_object()
                        && j.contains("port") && j["port"].is_number()
                    ) {
                        const auto& txt = j["txt"];
                        bool valid = true;
                        string fail_reason;
                        // Required TXT fields
                        vector<string> required = {"function", "model", "deviceType"};
                        for (const auto& f : required) {
                            if (!txt.contains(f) || !txt[f].is_string()) {
                                valid = false;
                                fail_reason += "Missing or invalid '" + f + "'. ";
                            }
                        }
                        // Validate port
                        if (j["port"] != 7889) {
                            valid = false;
                            fail_reason += "Port is not 7889. ";
                        }
                        // Validate deviceType
                        if (txt.contains("deviceType") && !allowed_device_types.count(txt["deviceType"])) {
                            valid = false;
                            fail_reason += "deviceType '" + txt["deviceType"].get<string>() + "' is not allowed. ";
                        }
                        // Validate model (must be non-empty string)
                        if (txt.contains("model") && txt["model"].is_string() && txt["model"].get<string>().empty()) {
                            valid = false;
                            fail_reason += "model is empty. ";
                        }
                        if (txt.contains("function") && txt["function"] == "slave") {
                            if (!txt.contains("multicast") || !txt["multicast"].is_string()) {
                                valid = false;
                                fail_reason += "Missing or invalid 'multicast' for slave. ";
                            } else {
                                // Validate multicast address format (should be like "244.1.1")
                                string multicast = txt["multicast"].get<string>();
                                if (multicast.find("244.") != 0 || count(multicast.begin(), multicast.end(), '.') != 2) {
                                    valid = false;
                                    fail_reason += "Invalid multicast format (should be 244.x.x). ";
                                }
                            }
                        }
                        string key = j["serviceName"].get<string>() + "@" + src_ip;
                        if (seen.count(key)) continue;
                        seen.insert(key);
                        log_detailed("INFO", "MDNS", "Found Virgil mDNS service", 
                            "From: " + src_ip + ", Service: " + j["serviceName"].get<string>() + 
                            "\n    Full JSON:\n" + j.dump(2));
                        
                        // Store and check identity
                        string danteName = j["serviceName"].get<string>();
                        string model = txt["model"].get<string>();
                        string deviceType = txt["deviceType"].get<string>();
                        if (!identity.initialized) {
                            identity.danteName = danteName;
                            identity.model = model;
                            identity.deviceType = deviceType;
                            identity.initialized = true;
                        } else {
                            if (identity.danteName != danteName) fail_reason += "Device name changed. ";
                            if (identity.model != model) fail_reason += "Model changed. ";
                            if (identity.deviceType != deviceType) fail_reason += "DeviceType changed. ";
                            if (!fail_reason.empty()) valid = false;
                        }
                        if (valid) {
                            cout << "[PASS] All required mDNS fields present and valid.\n";
                            log_detailed("PASS", "MDNS", "mDNS validation passed", 
                                "Service: " + j["serviceName"].get<string>() + ", Model: " + model + ", DeviceType: " + deviceType);
                        } else {
                            cout << "[FAIL] " << fail_reason << "\n";
                            log_detailed("FAIL", "MDNS", "mDNS validation failed", 
                                "Service: " + j["serviceName"].get<string>() + ", Errors: " + fail_reason);
                        }
                    }
                } catch (...) { 
                    log_detailed("WARN", "MDNS", "Failed to parse mDNS packet", 
                        "From: " + src_ip + ", Size: " + to_string(len) + " bytes\n    Raw data: " + string(buffer, len));
                    continue; 
                }
            }
        }
    }
    closesocket(sock);
    WSACleanup();
}

struct TestResult {
    string name;
    bool passed;
    string details;
};

class UdpClient {
public:
    UdpClient(const string& ip, int port) {
        log_detailed("INFO", "UDP", "Initializing UDP client", "Target: " + ip + ":" + to_string(port));
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            log_detailed("ERROR", "UDP", "WSAStartup failed", "Error code: " + to_string(WSAGetLastError()));
            throw runtime_error("WSAStartup failed");
        }
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            log_detailed("ERROR", "UDP", "Socket creation failed", "Error code: " + to_string(WSAGetLastError()));
            throw runtime_error("socket() failed");
        }

        // Bind to VIRGIL_PORT (7889) so we receive responses on the correct port
        sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        bind_addr.sin_port = htons(VIRGIL_PORT);
        if (::bind(sock, (sockaddr*)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) {
            int error = WSAGetLastError();
            log_detailed("ERROR", "UDP", "Bind to port 7889 failed", "WSA Error: " + to_string(error));
            closesocket(sock);
            WSACleanup();
            throw runtime_error("Failed to bind UDP socket to port 7889");
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        int inet_result = inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        if (inet_result != 1) {
            log_detailed("ERROR", "UDP", "Invalid IP address", "IP: " + ip + ", inet_pton result: " + to_string(inet_result));
            throw runtime_error("Invalid IP address: " + ip);
        }

        DWORD timeout = 5000; // 5s - increased for cross-network communication
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

        // Log the resolved address and bound port
        char resolved_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, resolved_ip, sizeof(resolved_ip));
        
        // Get and log the actual bound port
        sockaddr_in bound_addr;
        int bound_addr_len = sizeof(bound_addr);
        if (getsockname(sock, (sockaddr*)&bound_addr, &bound_addr_len) == 0) {
            int bound_port = ntohs(bound_addr.sin_port);
            log_detailed("INFO", "UDP", "UDP client initialized successfully", 
                        "Socket bound to port " + to_string(bound_port) + ", target: " + string(resolved_ip) + ":" + to_string(port));
        } else {
            log_detailed("INFO", "UDP", "UDP client initialized successfully", 
                        "Socket ready for communication to " + string(resolved_ip) + ":" + to_string(port));
        }
    }
    ~UdpClient() { closesocket(sock); WSACleanup(); }
    void send(const string& msg) {
        string formatted_msg = msg;
        // Try to format JSON for logging
        try {
            auto j = json::parse(msg);
            formatted_msg = j.dump(2);
        } catch (...) {
            // If parsing fails, use original message
        }
        
        log_detailed("SEND", "UDP", "Sending message", "Size: " + to_string(msg.size()) + " bytes\n    Content:\n" + formatted_msg);
        int send_result = sendto(sock, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&addr, sizeof(addr));
        if (send_result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            log_detailed("ERROR", "UDP", "Send failed", "WSA Error: " + to_string(error));
        } else {
            log_detailed("DEBUG", "UDP", "Send successful", "Bytes sent: " + to_string(send_result));
        }
        
        // Add small delay to ensure message is sent before next operation
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    string recv() {
        char buf[BUF_SIZE] = {0};
        sockaddr_in recv_addr;  // Use separate address structure for receiving
        int len = sizeof(recv_addr);
        
        // Get and print the bound port every time we wait for a packet
        sockaddr_in bound_addr;
        int bound_addr_len = sizeof(bound_addr);
        int listening_port = 0;
        if (getsockname(sock, (sockaddr*)&bound_addr, &bound_addr_len) == 0) {
            listening_port = ntohs(bound_addr.sin_port);
        }
        
        log_detailed("DEBUG", "UDP", "Attempting to receive data", "Listening on port " + to_string(listening_port) + ", waiting for response...");
        
        int ret = recvfrom(sock, buf, BUF_SIZE, 0, (sockaddr*)&recv_addr, &len);
        
        if (ret <= 0) {
            int error = WSAGetLastError();
            string error_desc;
            if (ret == 0) {
                error_desc = "Connection closed gracefully";
            } else if (error == WSAETIMEDOUT) {
                error_desc = "Timeout (WSAETIMEDOUT)";
            } else if (error == WSAECONNRESET) {
                error_desc = "Connection reset (WSAECONNRESET)";
            } else if (error == WSAEINTR) {
                error_desc = "Interrupted (WSAEINTR)";
            } else {
                error_desc = "Error code " + to_string(error);
            }
            
            log_detailed("RECV", "UDP", "No data received", "Return code: " + to_string(ret) + ", WSA Error: " + error_desc);
            return "";
        }
        
        // Log source address (using recv_addr, not addr)
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &recv_addr.sin_addr, src_ip, sizeof(src_ip));
        int src_port = ntohs(recv_addr.sin_port);
        
        string response(buf, ret);

        // Log all raw data received for debugging
        log_detailed("DEBUG", "UDP", "Raw data received", "From: " + string(src_ip) + ":" + to_string(src_port) + 
                    ", Size: " + to_string(ret) + " bytes\n    Raw: " + response);

        string formatted_response = response;
        // Try to format JSON for logging
        try {
            auto j = json::parse(response);
            formatted_response = j.dump(2);
        } catch (...) {
            // If parsing fails, use original response
        }

        log_detailed("RECV", "UDP", "Received message", "From: " + string(src_ip) + ":" + to_string(src_port) + 
                    ", Size: " + to_string(ret) + " bytes\n    Content:\n" + formatted_response);
        return response;
    }
private:
    SOCKET sock;
    sockaddr_in addr;
};

struct ExpectedResponse {
    string type;
    function<bool(const json&)> validator;
    string description;
};

class VirgilTester {
public:
    VirgilTester(const string& slave_ip, DeviceIdentity& identity)
        : client(slave_ip, VIRGIL_PORT), slave_ip(slave_ip), identity(identity) {}

    void run_all_tests() {
        results.clear();
        test_parameter_request();
        this_thread::sleep_for(chrono::milliseconds(100)); // Delay between test suites
        test_parameter_validation();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_locked_parameters();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_parameter_command();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_status_request();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_error_cases();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_device_level();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_continuous_status();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_precision_validation();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_enum_parameters();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_message_format_edge_cases();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_multicast_functionality();
        this_thread::sleep_for(chrono::milliseconds(100));
        test_gain_pad_independence();
        print_summary();
    }

    UdpClient client;
    string slave_ip;
    vector<TestResult> results;
    DeviceIdentity& identity;

    void add_result(const string& name, bool passed, const string& details = "") {
        results.push_back({name, passed, details});
        cout << (passed ? "[PASS] " : "[FAIL] ") << name << (details.empty() ? "" : (": " + details)) << endl;
        
        // Detailed logging for file
        log_detailed(passed ? "PASS" : "FAIL", "TEST", name, details.empty() ? "No additional details" : details);
    }

    void print_summary() {
        int pass = 0, fail = 0;
        for (const auto& r : results) (r.passed ? pass : fail)++;
        cout << "\n==== TEST SUMMARY ====" << endl;
        cout << "Passed: " << pass << ", Failed: " << fail << ", Total: " << results.size() << endl;
    }

    // Helper: Wait for a response and validate
    bool wait_for_response(const ExpectedResponse& expected, string& out_details, int tries = 5, int ms_wait = 800) {
        log_detailed("INFO", "WAIT", "Waiting for response", "Type: " + expected.type + ", Tries: " + to_string(tries) + ", Wait: " + to_string(ms_wait) + "ms");
        
        // Add initial delay to allow message to be processed
        this_thread::sleep_for(chrono::milliseconds(100));
        
        for (int i = 0; i < tries; ++i) {
            string resp = client.recv();
            if (resp.empty()) {
                log_detailed("DEBUG", "WAIT", "Empty response received", "Attempt " + to_string(i + 1) + "/" + to_string(tries));
                this_thread::sleep_for(chrono::milliseconds(ms_wait));
                continue;
            }
            try {
                auto j = json::parse(resp);
                log_detailed("DEBUG", "WAIT", "Parsing received JSON", "Content:\n" + j.dump(2));
                
                if (!j.contains("messages")) {
                    log_detailed("DEBUG", "WAIT", "No messages array in response", "JSON:\n" + j.dump(2));
                    continue;
                }
                for (const auto& msg : j["messages"]) {
                    if (msg.contains("messageType") && msg["messageType"] == expected.type) {
                        log_detailed("DEBUG", "WAIT", "Found matching message type", "Type: " + expected.type + "\n    Message:\n" + msg.dump(2));
                        
                        if (expected.validator(msg)) {
                            out_details = "Valid " + expected.type + " received.";
                            log_detailed("SUCCESS", "WAIT", "Response validation passed", out_details);
                            return true;
                        } else {
                            out_details = "Invalid " + expected.type + " content.";
                            log_detailed("WARN", "WAIT", "Response validation failed", out_details + "\n    Message:\n" + msg.dump(2));
                            return false;
                        }
                    }
                }
                log_detailed("DEBUG", "WAIT", "No matching message type found", "Expected: " + expected.type + "\n    Messages:\n" + j["messages"].dump(2));
            } catch (const exception& e) { 
                log_detailed("ERROR", "WAIT", "JSON parse error", "Error: " + string(e.what()) + "\n    Raw data: " + resp);
                continue; 
            }
        }
        out_details = "No valid " + expected.type + " received.";
        log_detailed("FAIL", "WAIT", "No valid response received", out_details + " after " + to_string(tries) + " tries");
        return false;
    }

    // --- Test Functions ---
    void test_parameter_request() {
        // Edge cases
        vector<int> indices = {0, 1, -1, -2, 999, -999};
        // Add all channel indices if available
        for (int idx : identity.channelIndices) {
            if (find(indices.begin(), indices.end(), idx) == indices.end()) {
                indices.push_back(idx);
            }
        }
        sort(indices.begin(), indices.end());
        indices.erase(unique(indices.begin(), indices.end()), indices.end());
        for (int idx : indices) {
            json req = {
                {"transmittingDevice", "TestMaster"},
                {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", idx}}}}
            };
            client.send(req.dump());
            
            // Add delay between different test cases to prevent overwhelming the network
            this_thread::sleep_for(chrono::milliseconds(50));
            
            ExpectedResponse expected;
            if (idx >= 0) {
                // Channel-level: check for gain and correct types
                expected = {"ParameterResponse", [this, idx](const json& m){
                    if (!m.contains("channelIndex") || !m.contains("gain")) return false;
                    if (m["channelIndex"] != idx) return false;
                    const auto& gain = m["gain"];
                    if (!gain.contains("value") || !gain.contains("dataType")) return false;
                    if (gain["dataType"] != "int" && gain["dataType"] != "float") return false;
                    if (!gain["value"].is_number()) return false;
                    return true;
                }, "Channel ParameterResponse with gain and correct types"};
            } else if (idx == -1) {
                // Device-level: check for model, deviceType, channelIndices, virgilVersion
                expected = {"ParameterResponse", [this](const json& m){
                    if (!(m.contains("model") && m.contains("deviceType") && m.contains("channelIndices") && m.contains("virgilVersion") && m["channelIndices"].is_array() && m["virgilVersion"].is_string())) return false;
                    // Check identity
                    if (identity.model != m["model"] || identity.deviceType != m["deviceType"]) return false;
                    // Store channelIndices for later tests
                    identity.channelIndices.clear();
                    for (const auto& idx : m["channelIndices"]) {
                        if (idx.is_number_integer()) {
                            identity.channelIndices.push_back(idx.get<int>());
                        }
                    }
                    return true;
                }, "Device ParameterResponse with channelIndices and virgilVersion"};
            } else if (idx == -2) {
                // All: must have model and at least one channelIndex
                expected = {"ParameterResponse", [this](const json& m){
                    if (!m.contains("model")) return false;
                    if (identity.model != m["model"]) return false;
                    return m.contains("channelIndex") || m.contains("channelIndices");
                }, "All ParameterResponse"};
            } else {
                expected = {"ErrorResponse", [](const json& m){ return m.contains("errorValue"); }, "Error for invalid channelIndex"};
            }
            string details;
            bool ok = wait_for_response(expected, details);
            add_result("ParameterRequest channelIndex=" + to_string(idx), ok, details);
        }
    }

    void test_parameter_command() {
        if (identity.channelIndices.empty()) {
            add_result("ParameterCommand test", false, "No channels available");
            return;
        }
        
        int test_channel = identity.channelIndices[0];
        
        // Valid command
        json valid_cmd = {{"messageType", "ParameterCommand"}, {"channelIndex", test_channel}, {"gain", {{"value", 0}}}};
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {valid_cmd}}};
        client.send(req.dump());
        ExpectedResponse expected = {"StatusUpdate", [](const json& m){ return m.contains("channelIndex") && m.contains("gain"); }, "StatusUpdate after valid command"};
        string details;
        bool ok = wait_for_response(expected, details);
        add_result("ParameterCommand valid", ok, details);

        // Out of range (using extreme value)
        json out_of_range = {{"messageType", "ParameterCommand"}, {"channelIndex", test_channel}, {"gain", {{"value", 999}}}};
        req = {{"transmittingDevice", "TestMaster"}, {"messages", {out_of_range}}};
        client.send(req.dump());
        expected = {"ErrorResponse", [](const json& m){ return m["errorValue"] == "ValueOutOfRange"; }, "ErrorResponse for out of range"};
        ok = wait_for_response(expected, details);
        add_result("ParameterCommand out_of_range", ok, details);

        // Invalid type
        json invalid_type = {{"messageType", "ParameterCommand"}, {"channelIndex", test_channel}, {"gain", {{"value", "notanumber"}}}};
        req = {{"transmittingDevice", "TestMaster"}, {"messages", {invalid_type}}};
        client.send(req.dump());
        expected = {"ErrorResponse", [](const json& m){ return m["errorValue"] == "InvalidValueType"; }, "ErrorResponse for invalid type"};
        ok = wait_for_response(expected, details);
        add_result("ParameterCommand invalid_type", ok, details);

        // Test invalid channel index
        json invalid_channel = {{"messageType", "ParameterCommand"}, {"channelIndex", 999}, {"gain", {{"value", 0}}}};
        req = {{"transmittingDevice", "TestMaster"}, {"messages", {invalid_channel}}};
        client.send(req.dump());
        expected = {"ErrorResponse", [](const json& m){ return m["errorValue"] == "ChannelIndexInvalid"; }, "ErrorResponse for invalid channel"};
        ok = wait_for_response(expected, details);
        add_result("ParameterCommand invalid_channel", ok, details);

        // Test unsupported parameter
        json unsupported_param = {{"messageType", "ParameterCommand"}, {"channelIndex", test_channel}, {"nonexistentParam", {{"value", 0}}}};
        req = {{"transmittingDevice", "TestMaster"}, {"messages", {unsupported_param}}};
        client.send(req.dump());
        expected = {"ErrorResponse", [](const json& m){ return m["errorValue"] == "ParameterUnsupported"; }, "ErrorResponse for unsupported parameter"};
        ok = wait_for_response(expected, details);
        add_result("ParameterCommand unsupported_param", ok, details);

        // Test multiple parameter changes in one command
        json multi_param = {
            {"messageType", "ParameterCommand"}, 
            {"channelIndex", test_channel}, 
            {"gain", {{"value", 0}}},
            {"polarity", {{"value", false}}}
        };
        req = {{"transmittingDevice", "TestMaster"}, {"messages", {multi_param}}};
        client.send(req.dump());
        expected = {"StatusUpdate", [](const json& m){ return m.contains("channelIndex"); }, "StatusUpdate after multi-parameter command"};
        ok = wait_for_response(expected, details);
        add_result("ParameterCommand multi_parameter", ok, details);
    }

    void test_status_request() {
        vector<int> indices = {-1, 999};
        
        // Add valid channel indices
        for (int idx : identity.channelIndices) {
            if (find(indices.begin(), indices.end(), idx) == indices.end()) {
                indices.push_back(idx);
            }
        }
        
        for (int idx : indices) {
            json req = {
                {"transmittingDevice", "TestMaster"},
                {"messages", {{{"messageType", "StatusRequest"}, {"channelIndex", idx}}}}
            };
            client.send(req.dump());
            ExpectedResponse expected;
            if (find(identity.channelIndices.begin(), identity.channelIndices.end(), idx) != identity.channelIndices.end()) {
                expected = {"StatusUpdate", [idx](const json& m){ 
                    return m.contains("channelIndex") && m["channelIndex"] == idx; 
                }, "StatusUpdate for valid channel"};
            } else if (idx == -1) {
                expected = {"StatusUpdate", [](const json& m){ 
                    return m.contains("channelIndex"); 
                }, "StatusUpdate for all channels"};
            } else {
                expected = {"ErrorResponse", [](const json& m){ 
                    return m["errorValue"] == "ChannelIndexInvalid"; 
                }, "Error for invalid channelIndex"};
            }
            string details;
            bool ok = wait_for_response(expected, details);
            add_result("StatusRequest channelIndex=" + to_string(idx), ok, details);
        }

        // Test that StatusUpdate contains all current values for the channel
        if (!identity.channelIndices.empty()) {
            int test_channel = identity.channelIndices[0];
            json req = {
                {"transmittingDevice", "TestMaster"},
                {"messages", {{{"messageType", "StatusRequest"}, {"channelIndex", test_channel}}}}
            };
            client.send(req.dump());
            
            string resp = client.recv();
            if (!resp.empty()) {
                try {
                    auto j = json::parse(resp);
                    if (j.contains("messages")) {
                        for (const auto& msg : j["messages"]) {
                            if (msg.contains("messageType") && msg["messageType"] == "StatusUpdate") {
                                bool has_gain = msg.contains("gain") && msg["gain"].contains("value");
                                add_result("StatusUpdate completeness", has_gain, 
                                          has_gain ? "Contains required gain value" : "Missing gain value");
                                break;
                            }
                        }
                    }
                } catch (...) {
                    add_result("StatusUpdate parsing", false, "JSON parse error");
                }
            }
        }
    }

    void test_error_cases() {
        // Test all specific error types
        vector<pair<string, string>> error_tests = {
            {"UnrecognizedCommand", "NotAType"},
            {"ChannelIndexInvalid", "999"},
            {"MalformedMessage", ""},
            {"ParameterUnsupported", ""},
            {"InvalidValueType", "string_instead_of_number"}
        };

        // Malformed message (missing transmittingDevice)
        json malformed = {{"messages", {}}};
        client.send(malformed.dump());
        ExpectedResponse expected = {"ErrorResponse", [](const json& m){ 
            return m.contains("errorValue") && m.contains("errorString") && m["errorString"].is_string(); 
        }, "Error for malformed message"};
        string details;
        bool ok = wait_for_response(expected, details);
        add_result("Malformed message (missing transmittingDevice)", ok, details);

        // Empty messages array
        json empty_msgs = {{"transmittingDevice", "TestMaster"}, {"messages", {}}};
        client.send(empty_msgs.dump());
        ok = wait_for_response(expected, details);
        add_result("Empty messages array", ok, details);

        // Unsupported messageType
        json unsupported = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "NotAType"}}}}};
        client.send(unsupported.dump());
        expected = {"ErrorResponse", [](const json& m){ return m["errorValue"] == "UnrecognizedCommand"; }, "ErrorResponse for unsupported messageType"};
        ok = wait_for_response(expected, details);
        add_result("Unsupported messageType", ok, details);

        // Test invalid JSON structure
        client.send("{\"incomplete\":}");
        expected = {"ErrorResponse", [](const json& m){ return m.contains("errorValue"); }, "Error for invalid JSON"};
        ok = wait_for_response(expected, details);
        add_result("Invalid JSON structure", ok, details);

        // Test missing channelIndex
        json missing_channel = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterCommand"}, {"gain", {{"value", 0}}}}}}};
        client.send(missing_channel.dump());
        ok = wait_for_response(expected, details);
        add_result("Missing channelIndex", ok, details);
    }

    void test_device_level() {
        // Device-level info
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", -1}}}}};
        client.send(req.dump());
        ExpectedResponse expected = {"ParameterResponse", [](const json& m){ return m.contains("model") && m.contains("deviceType") && m.contains("channelIndices"); }, "Device-level ParameterResponse"};
        string details;
        bool ok = wait_for_response(expected, details);
        add_result("Device-level ParameterResponse", ok, details);
    }

    void test_continuous_status() {
        // First check what continuous parameters the device advertises
        bool device_has_audio_level = false, device_has_rf_level = false, device_has_battery_level = false;
        
        // Query device-level parameters to see what continuous parameters are available
        json param_req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", -1}}}}};
        client.send(param_req.dump());
        string param_resp = client.recv();
        
        if (!param_resp.empty()) {
            try {
                auto param_j = json::parse(param_resp);
                if (param_j.contains("messages")) {
                    for (const auto& msg : param_j["messages"]) {
                        if (msg.contains("messageType") && msg["messageType"] == "ParameterResponse") {
                            if (msg.contains("audioLevel")) device_has_audio_level = true;
                            if (msg.contains("rfLevel")) device_has_rf_level = true;
                            if (msg.contains("batteryLevel")) device_has_battery_level = true;
                        }
                    }
                }
            } catch (...) {}
        }
        
        // If device has no continuous parameters, skip the test
        if (!device_has_audio_level && !device_has_rf_level && !device_has_battery_level) {
            add_result("Continuous status test", true, "Skipped - device has no continuous parameters");
            return;
        }
        
        // Request status and expect multiple StatusUpdate messages
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "StatusRequest"}, {"channelIndex", 0}}}}};
        client.send(req.dump());
        
        int count = 0;
        vector<chrono::steady_clock::time_point> timestamps;
        auto start_time = chrono::steady_clock::now();
        
        // Look for continuous parameters specifically
        bool found_audio_level = false, found_rf_level = false, found_battery_level = false;
        
        for (int i = 0; i < 6; ++i) {  // Check for 3 seconds
            string resp = client.recv();
            if (!resp.empty()) {
                try {
                    auto j = json::parse(resp);
                    if (j.contains("messages")) {
                        for (const auto& msg : j["messages"]) {
                            if (msg.contains("messageType") && msg["messageType"] == "StatusUpdate") {
                                count++;
                                timestamps.push_back(chrono::steady_clock::now());
                                
                                // Check for continuous parameters
                                if (msg.contains("audioLevel")) found_audio_level = true;
                                if (msg.contains("rfLevel")) found_rf_level = true;
                                if (msg.contains("batteryLevel")) found_battery_level = true;
                            }
                        }
                    }
                } catch (...) {}
            }
            this_thread::sleep_for(500ms);
        }
        
        add_result("Continuous StatusUpdate count", count >= 2, "Received " + to_string(count) + " updates");
        
        // Check timing intervals (should be around 500ms)
        bool timing_ok = true;
        if (timestamps.size() >= 2) {
            for (size_t i = 1; i < timestamps.size(); ++i) {
                auto interval = chrono::duration_cast<chrono::milliseconds>(timestamps[i] - timestamps[i-1]).count();
                if (interval < 400 || interval > 600) {  // 500ms ± 100ms tolerance
                    timing_ok = false;
                    break;
                }
            }
        }
        add_result("Continuous update timing", timing_ok && timestamps.size() >= 2, 
                  "Timing validation with " + to_string(timestamps.size()) + " samples");
        
        // Report which continuous parameters were found (only if device advertises them)
        if (device_has_audio_level) {
            add_result("audioLevel continuous updates", found_audio_level, 
                      found_audio_level ? "Found audioLevel in updates" : "Device advertises audioLevel but not found in updates");
        }
        if (device_has_rf_level) {
            add_result("rfLevel continuous updates", found_rf_level, 
                      found_rf_level ? "Found rfLevel in updates" : "Device advertises rfLevel but not found in updates");
        }
        if (device_has_battery_level) {
            add_result("batteryLevel continuous updates", found_battery_level, 
                      found_battery_level ? "Found batteryLevel in updates" : "Device advertises batteryLevel but not found in updates");
        }
    }

    void test_parameter_validation() {
        // Get device parameters first
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", -1}}}}};
        client.send(req.dump());
        string resp = client.recv();
        if (resp.empty()) {
            add_result("Parameter validation setup", false, "No device response");
            return;
        }

        try {
            auto j = json::parse(resp);
            if (!j.contains("messages")) return;
            
            for (const auto& msg : j["messages"]) {
                if (msg.contains("messageType") && msg["messageType"] == "ParameterResponse") {
                    // Test channel parameters for completeness
                    if (!identity.channelIndices.empty()) {
                        int test_channel = identity.channelIndices[0];
                        json channel_req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", test_channel}}}}};
                        client.send(channel_req.dump());
                        
                        string channel_resp = client.recv();
                        if (!channel_resp.empty()) {
                            auto channel_j = json::parse(channel_resp);
                            if (channel_j.contains("messages")) {
                                for (const auto& channel_msg : channel_j["messages"]) {
                                    if (channel_msg.contains("messageType") && channel_msg["messageType"] == "ParameterResponse") {
                                        validate_parameter_completeness(channel_msg, test_channel);
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        } catch (...) {
            add_result("Parameter validation", false, "JSON parse error");
        }
    }

    void validate_parameter_completeness(const json& msg, int channel_idx) {
        // Test that gain parameter has all required fields
        if (msg.contains("gain")) {
            const auto& gain = msg["gain"];
            bool valid = true;
            string issues;
            
            // Required fields
            if (!gain.contains("value")) { valid = false; issues += "Missing value. "; }
            if (!gain.contains("dataType")) { valid = false; issues += "Missing dataType. "; }
            if (!gain.contains("locked")) { valid = false; issues += "Missing locked. "; }
            
            // Validate dataType
            if (gain.contains("dataType") && gain["dataType"] != "int" && gain["dataType"] != "float") {
                valid = false; issues += "Invalid dataType for gain. ";
            }
            
            // Check numeric constraints
            if (gain.contains("minValue") && !gain["minValue"].is_number()) {
                valid = false; issues += "minValue not numeric. ";
            }
            if (gain.contains("maxValue") && !gain["maxValue"].is_number()) {
                valid = false; issues += "maxValue not numeric. ";
            }
            if (gain.contains("precision") && !gain["precision"].is_number()) {
                valid = false; issues += "precision not numeric. ";
            }
            
            // Check precision is positive
            if (gain.contains("precision") && gain["precision"].is_number()) {
                float precision = gain["precision"].get<float>();
                if (precision <= 0) {
                    valid = false; issues += "precision must be positive. ";
                }
            }
            
            // Check minValue <= maxValue
            if (gain.contains("minValue") && gain.contains("maxValue") && 
                gain["minValue"].is_number() && gain["maxValue"].is_number()) {
                float minVal = gain["minValue"].get<float>();
                float maxVal = gain["maxValue"].get<float>();
                if (minVal > maxVal) {
                    valid = false; issues += "minValue (" + to_string(minVal) + ") > maxValue (" + to_string(maxVal) + "). ";
                }
            }
            
            add_result("Gain parameter completeness ch" + to_string(channel_idx), valid, issues);
        }

        // Test other parameters if present
        vector<string> param_names = {"pad", "lowcut", "lowcutEnable", "polarity", "phantomPower", 
                                               "rfEnable", "transmitPower", "transmitterConnected", "squelch", 
                                               "subDevice", "audioLevel", "rfLevel", "batteryLevel"};
        
        for (const auto& param : param_names) {
            if (msg.contains(param)) {
                validate_parameter_structure(msg[param], param, channel_idx);
            }
        }
    }

    void validate_parameter_structure(const json& param, const string& name, int channel_idx) {
        bool valid = true;
        string issues;
        
        // All parameters must have these fields
        if (!param.contains("locked")) { valid = false; issues += "Missing locked. "; }
        if (!param.contains("dataType")) { valid = false; issues += "Missing dataType. "; }
        if (!param.contains("value")) { valid = false; issues += "Missing value. "; }
        
        // Validate locked field
        if (param.contains("locked") && !param["locked"].is_boolean()) {
            valid = false; issues += "locked not boolean. ";
        }
        
        // Validate dataType values
        if (param.contains("dataType")) {
            string dataType = param["dataType"];
            if (dataType != "int" && dataType != "float" && dataType != "bool" && 
                dataType != "string" && dataType != "enum") {
                valid = false; issues += "Invalid dataType. ";
            }
            
            // Validate value matches dataType
            if (dataType == "bool" && !param["value"].is_boolean()) {
                valid = false; issues += "Value type doesn't match bool dataType. ";
            } else if ((dataType == "int" || dataType == "float") && !param["value"].is_number()) {
                valid = false; issues += "Value type doesn't match numeric dataType. ";
            } else if (dataType == "string" && !param["value"].is_string()) {
                valid = false; issues += "Value type doesn't match string dataType. ";
            }
        }
        
        // Validate unit-specific constraints
        if (param.contains("unit")) {
            string unit = param["unit"];
            if (unit == "%") {
                // Percent unit validation
                if (param.contains("dataType") && param["dataType"] != "int") {
                    valid = false; issues += "Percent unit must have int dataType. ";
                }
                if (param.contains("precision") && param["precision"] != 1) {
                    valid = false; issues += "Percent unit must have precision of 1. ";
                }
                if (param.contains("minValue") && param["minValue"] != 0) {
                    valid = false; issues += "Percent unit must have minValue of 0. ";
                }
                if (param.contains("maxValue") && param["maxValue"] != 100) {
                    valid = false; issues += "Percent unit must have maxValue of 100. ";
                }
            } else if (unit == "dB" || unit == "Hz") {
                // dB and Hz should be numeric types
                if (param.contains("dataType") && param["dataType"] != "int" && param["dataType"] != "float") {
                    valid = false; issues += unit + " unit must have numeric dataType. ";
                }
            }
        }
        
        // Validate numeric constraints for all parameters
        if (param.contains("precision") && param["precision"].is_number()) {
            double precision = param["precision"].get<double>();
            if (precision <= 0) {
                valid = false; issues += "precision must be positive. ";
            }
        }
        
        // Check minValue <= maxValue for all parameters
        if (param.contains("minValue") && param.contains("maxValue") && 
            param["minValue"].is_number() && param["maxValue"].is_number()) {
            double minVal = param["minValue"].get<double>();
            double maxVal = param["maxValue"].get<double>();
            if (minVal > maxVal) {
                valid = false; issues += "minValue (" + to_string(minVal) + ") > maxValue (" + to_string(maxVal) + "). ";
            }
        }
        
        // Special validations for specific parameters
        if (name == "subDevice" && param.contains("value")) {
            string value = param["value"];
            static const vector<string> valid_subdevices = {
                "handheld", "beltpack", "gooseneck", "iem", "xlr", "trs", "disconnected", "other"
            };
            if (find(valid_subdevices.begin(), valid_subdevices.end(), value) == valid_subdevices.end()) {
                valid = false; issues += "Invalid subDevice value. ";
            }
        }
        
        if (name == "transmitPower" && param.contains("dataType") && param["dataType"] == "enum") {
            if (!param.contains("enumValues") || !param["enumValues"].is_array()) {
                valid = false; issues += "Missing enumValues for enum type. ";
            }
        }
        
        // Continuous parameters should be locked
        if ((name == "audioLevel" || name == "rfLevel" || name == "batteryLevel") && 
            param.contains("locked") && !param["locked"].get<bool>()) {
            valid = false; issues += "Continuous parameter should be locked. ";
        }
        
        // Battery level should have percent unit
        if (name == "batteryLevel") {
            if (!param.contains("unit") || param["unit"] != "%") {
                valid = false; issues += "batteryLevel should have unit '%'. ";
            }
        }
        
        // Audio and RF levels should have dB unit
        if ((name == "audioLevel" || name == "rfLevel") && param.contains("unit") && param["unit"] != "dB") {
            valid = false; issues += name + " should have unit 'dB'. ";
        }
        
        add_result(name + " parameter structure ch" + to_string(channel_idx), valid, issues);
    }

    void test_locked_parameters() {
        // First get a channel's parameters to find locked ones
        if (identity.channelIndices.empty()) {
            add_result("Locked parameter test", false, "No channels available");
            return;
        }
        
        int test_channel = identity.channelIndices[0];
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", test_channel}}}}};
        client.send(req.dump());
        
        string resp = client.recv();
        if (resp.empty()) {
            add_result("Locked parameter test setup", false, "No response");
            return;
        }
        
        try {
            auto j = json::parse(resp);
            if (!j.contains("messages")) return;
            
            for (const auto& msg : j["messages"]) {
                if (msg.contains("messageType") && msg["messageType"] == "ParameterResponse") {
                    // Find locked parameters and try to change them
                    for (auto& [key, value] : msg.items()) {
                        if (key != "messageType" && key != "channelIndex" && value.is_object() && 
                            value.contains("locked") && value["locked"].get<bool>()) {
                            
                            // Try to change this locked parameter
                            json change_cmd = {
                                {"messageType", "ParameterCommand"}, 
                                {"channelIndex", test_channel}
                            };
                            
                            // Set a different value based on dataType
                            if (value.contains("dataType")) {
                                if (value["dataType"] == "bool") {
                                    change_cmd[key] = {{"value", !value["value"].get<bool>()}};
                                } else if (value["dataType"] == "int" || value["dataType"] == "float") {
                                    change_cmd[key] = {{"value", value["value"].get<double>() + 1}};
                                } else if (value["dataType"] == "string") {
                                    change_cmd[key] = {{"value", "changed"}};
                                }
                                
                                json req = {{"transmittingDevice", "TestMaster"}, {"messages", {change_cmd}}};
                                client.send(req.dump());
                                
                                ExpectedResponse expected = {"ErrorResponse", [](const json& m){ 
                                    return m["errorValue"] == "Parameterlocked" || m["errorValue"] == "UnableToChangeValue"; 
                                }, "Error for locked parameter change"};
                                string details;
                                bool ok = wait_for_response(expected, details);
                                add_result("Locked parameter " + key + " rejection", ok, details);
                            }
                        }
                    }
                    break;
                }
            }
        } catch (...) {
            add_result("Locked parameter test", false, "JSON parse error");
        }
    }

    void test_precision_validation() {
        // Test that precision constraints are enforced
        if (identity.channelIndices.empty()) return;
        
        int test_channel = identity.channelIndices[0];
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", test_channel}}}}};
        client.send(req.dump());
        
        string resp = client.recv();
        if (resp.empty()) return;
        
        try {
            auto j = json::parse(resp);
            if (!j.contains("messages")) return;
            
            for (const auto& msg : j["messages"]) {
                if (msg.contains("messageType") && msg["messageType"] == "ParameterResponse" && msg.contains("gain")) {
                    const auto& gain = msg["gain"];
                    
                    if (gain.contains("precision") && gain.contains("minValue") && 
                        gain["precision"].is_number() && gain["minValue"].is_number()) {
                        
                        double precision = gain["precision"].get<double>();
                        double minValue = gain["minValue"].get<double>();
                        
                        if (precision != 1.0) {
                            // Test invalid precision value (not aligned)
                            double invalid_value = minValue + 1.5 * precision;
                            
                            json cmd = {
                                {"messageType", "ParameterCommand"},
                                {"channelIndex", test_channel},
                                {"gain", {{"value", invalid_value}}}
                            };
                            json req = {{"transmittingDevice", "TestMaster"}, {"messages", {cmd}}};
                            client.send(req.dump());
                            
                            ExpectedResponse expected = {"ErrorResponse", [](const json& m){ 
                                return m["errorValue"] == "ValueOutOfRange" || m["errorValue"] == "InvalidValueType"; 
                            }, "Error for precision violation"};
                            string details;
                            bool ok = wait_for_response(expected, details);
                            add_result("Precision validation", ok, details);
                        }
                    }
                    break;
                }
            }
        } catch (...) {}
    }

    void test_enum_parameters() {
        // Look for enum parameters and test invalid values
        if (identity.channelIndices.empty()) return;
        
        int test_channel = identity.channelIndices[0];
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", test_channel}}}}};
        client.send(req.dump());
        
        string resp = client.recv();
        if (resp.empty()) return;
        
        try {
            auto j = json::parse(resp);
            if (!j.contains("messages")) return;
            
            for (const auto& msg : j["messages"]) {
                if (msg.contains("messageType") && msg["messageType"] == "ParameterResponse") {
                    for (auto& [key, value] : msg.items()) {
                        if (key != "messageType" && key != "channelIndex" && value.is_object() && 
                            value.contains("dataType") && value["dataType"] == "enum") {
                            
                            // Test invalid enum value
                            json cmd = {
                                {"messageType", "ParameterCommand"},
                                {"channelIndex", test_channel},
                                {key, {{"value", "invalid_enum_value"}}}
                            };
                            json req = {{"transmittingDevice", "TestMaster"}, {"messages", {cmd}}};
                            client.send(req.dump());
                            
                            ExpectedResponse expected = {"ErrorResponse", [](const json& m){ 
                                return m["errorValue"] == "InvalidValueType" || m["errorValue"] == "ValueOutOfRange"; 
                            }, "Error for invalid enum value"};
                            string details;
                            bool ok = wait_for_response(expected, details);
                            add_result("Enum parameter " + key + " validation", ok, details);
                        }
                    }
                    break;
                }
            }
        } catch (...) {}
    }

    void test_message_format_edge_cases() {
        // Test multiple messages in single request
        json multi_req = {
            {"transmittingDevice", "TestMaster"},
            {"messages", {
                {{"messageType", "StatusRequest"}, {"channelIndex", 0}},
                {{"messageType", "StatusRequest"}, {"channelIndex", 1}}
            }}
        };
        client.send(multi_req.dump());
        
        // Should get multiple responses or combined response
        int response_count = 0;
        for (int i = 0; i < 3; ++i) {
            string resp = client.recv();
            if (!resp.empty()) response_count++;
            this_thread::sleep_for(200ms);
        }
        add_result("Multiple messages handling", response_count > 0, "Received " + to_string(response_count) + " responses");

        // Test very large message (near size limits)
        string large_device_name(1000, 'X');
        json large_msg = {
            {"transmittingDevice", large_device_name},
            {"messages", {{{"messageType", "StatusRequest"}, {"channelIndex", 0}}}}
        };
        client.send(large_msg.dump());
        
        string resp = client.recv();
        bool handled = !resp.empty();
        add_result("Large message handling", handled, handled ? "Handled large message" : "No response to large message");

        // Test missing transmittingDevice
        json no_device = {{"messages", {{{"messageType", "StatusRequest"}, {"channelIndex", 0}}}}};
        client.send(no_device.dump());
        
        ExpectedResponse expected = {"ErrorResponse", [](const json& m){ return m.contains("errorValue"); }, "Error for missing transmittingDevice"};
        string details;
        bool ok = wait_for_response(expected, details);
        add_result("Missing transmittingDevice", ok, details);
    }

    void test_multicast_functionality() {
        // Test that StatusUpdate messages are sent via multicast
        // This is challenging to test fully without multicast listening setup
        
        // Send a command that should trigger StatusUpdate
        if (!identity.channelIndices.empty()) {
            json cmd = {
                {"messageType", "ParameterCommand"},
                {"channelIndex", identity.channelIndices[0]},
                {"gain", {{"value", 0}}}
            };
            json req = {{"transmittingDevice", "TestMaster"}, {"messages", {cmd}}};
            client.send(req.dump());
            
            // Should receive StatusUpdate (even though we're not listening on multicast)
            ExpectedResponse expected = {"StatusUpdate", [](const json& m){ return m.contains("channelIndex"); }, "StatusUpdate after command"};
            string details;
            bool ok = wait_for_response(expected, details);
            add_result("StatusUpdate multicast trigger", ok, details);
        }

        // Test continuous status updates timing
        auto start_time = chrono::steady_clock::now();
        vector<chrono::steady_clock::time_point> update_times;
        
        for (int i = 0; i < 10; ++i) {
            string resp = client.recv();
            if (!resp.empty()) {
                try {
                    auto j = json::parse(resp);
                    if (j.contains("messages")) {
                        for (const auto& msg : j["messages"]) {
                            if (msg.contains("messageType") && msg["messageType"] == "StatusUpdate") {
                                update_times.push_back(chrono::steady_clock::now());
                            }
                        }
                    }
                } catch (...) {}
            }
            this_thread::sleep_for(100ms);
        }
        
        // Check if updates come roughly every 500ms
        bool timing_ok = true;
        for (size_t i = 1; i < update_times.size(); ++i) {
            auto interval = chrono::duration_cast<chrono::milliseconds>(update_times[i] - update_times[i-1]).count();
            if (interval < 400 || interval > 600) {  // Allow 100ms tolerance
                timing_ok = false;
                break;
            }
        }
        
        add_result("Continuous update timing", timing_ok && update_times.size() >= 2, 
                  "Updates: " + to_string(update_times.size()) + ", timing " + (timing_ok ? "OK" : "incorrect"));
    }

    void test_gain_pad_independence() {
        // Test that gain value remains unchanged when pad is toggled
        if (identity.channelIndices.empty()) {
            add_result("Gain/Pad independence test", false, "No channels available");
            return;
        }
        
        int test_channel = identity.channelIndices[0];
        
        // Step 1: Get current gain and pad values
        json req = {{"transmittingDevice", "TestMaster"}, {"messages", {{{"messageType", "ParameterRequest"}, {"channelIndex", test_channel}}}}};
        client.send(req.dump());
        
        string resp = client.recv();
        if (resp.empty()) {
            add_result("Gain/Pad independence setup", false, "No response to parameter request");
            return;
        }
        
        double original_gain = 0;
        bool has_gain = false;
        bool has_pad = false;
        bool original_pad_state = false;
        bool pad_locked = true;
        
        try {
            auto j = json::parse(resp);
            if (!j.contains("messages")) {
                add_result("Gain/Pad independence parse", false, "No messages in response");
                return;
            }
            
            for (const auto& msg : j["messages"]) {
                if (msg.contains("messageType") && msg["messageType"] == "ParameterResponse") {
                    // Extract gain value
                    if (msg.contains("gain") && msg["gain"].contains("value")) {
                        original_gain = msg["gain"]["value"].get<double>();
                        has_gain = true;
                    }
                    
                    // Extract pad state and check if it's controllable
                    if (msg.contains("pad")) {
                        has_pad = true;
                        if (msg["pad"].contains("value") && msg["pad"]["value"].is_boolean()) {
                            original_pad_state = msg["pad"]["value"].get<bool>();
                        }
                        if (msg["pad"].contains("locked") && msg["pad"]["locked"].is_boolean()) {
                            pad_locked = msg["pad"]["locked"].get<bool>();
                        }
                    }
                    break;
                }
            }
        } catch (...) {
            add_result("Gain/Pad independence JSON parse", false, "Failed to parse parameter response");
            return;
        }
        
        if (!has_gain) {
            add_result("Gain/Pad independence gain check", false, "No gain parameter found");
            return;
        }
        
        if (!has_pad) {
            add_result("Gain/Pad independence pad check", false, "No pad parameter found - test not applicable");
            return;
        }
        
        if (pad_locked) {
            add_result("Gain/Pad independence pad locked", false, "Pad parameter is locked - cannot test toggle");
            return;
        }
        
        // Step 2: Toggle the pad state
        bool new_pad_state = !original_pad_state;
        json pad_cmd = {
            {"messageType", "ParameterCommand"},
            {"channelIndex", test_channel},
            {"pad", {{"value", new_pad_state}}}
        };
        json pad_req = {{"transmittingDevice", "TestMaster"}, {"messages", {pad_cmd}}};
        client.send(pad_req.dump());
        
        // Wait for response (should be StatusUpdate)
        ExpectedResponse expected = {"StatusUpdate", [](const json& m){ 
            return m.contains("channelIndex"); 
        }, "StatusUpdate after pad toggle"};
        string details;
        bool pad_toggle_ok = wait_for_response(expected, details);
        
        if (!pad_toggle_ok) {
            add_result("Gain/Pad independence pad toggle", false, "Failed to toggle pad: " + details);
            return;
        }
        
        // Step 3: Get the parameters again and verify gain hasn't changed
        client.send(req.dump());  // Same request as before
        resp = client.recv();
        
        if (resp.empty()) {
            add_result("Gain/Pad independence verification", false, "No response to verification request");
            return;
        }
        
        double final_gain = 0;
        bool final_pad_state = original_pad_state;  // Default to original in case we can't read it
        bool verification_success = false;
        
        try {
            auto j = json::parse(resp);
            if (j.contains("messages")) {
                for (const auto& msg : j["messages"]) {
                    if (msg.contains("messageType") && msg["messageType"] == "ParameterResponse") {
                        // Check final gain value
                        if (msg.contains("gain") && msg["gain"].contains("value")) {
                            final_gain = msg["gain"]["value"].get<double>();
                        }
                        
                        // Check final pad state
                        if (msg.contains("pad") && msg["pad"].contains("value") && msg["pad"]["value"].is_boolean()) {
                            final_pad_state = msg["pad"]["value"].get<bool>();
                        }
                        
                        verification_success = true;
                        break;
                    }
                }
            }
        } catch (...) {
            add_result("Gain/Pad independence verification parse", false, "Failed to parse verification response");
            return;
        }
        
        if (!verification_success) {
            add_result("Gain/Pad independence verification", false, "No valid parameter response in verification");
            return;
        }
        
        // Verify results
        bool gain_unchanged = (abs(original_gain - final_gain) < 0.001);  // Allow for floating point precision
        bool pad_changed = (final_pad_state == new_pad_state);
        
        string result_details = "Original gain: " + to_string(original_gain) + 
                                   ", Final gain: " + to_string(final_gain) + 
                                   ", Pad: " + (original_pad_state ? "on" : "off") + 
                                   " -> " + (final_pad_state ? "on" : "off");
        
        add_result("Gain value independence", gain_unchanged, 
                  gain_unchanged ? "Gain remained constant during pad toggle" : 
                  "FAIL: Gain changed from " + to_string(original_gain) + " to " + to_string(final_gain));
        
        add_result("Pad toggle verification", pad_changed,
                  pad_changed ? "Pad successfully toggled" : 
                  "FAIL: Pad state didn't change as expected");
        
        add_result("Gain/Pad independence overall", gain_unchanged && pad_changed, result_details);
        
        // Step 4: Restore original pad state for cleanliness
        json restore_cmd = {
            {"messageType", "ParameterCommand"},
            {"channelIndex", test_channel},
            {"pad", {{"value", original_pad_state}}}
        };
        json restore_req = {{"transmittingDevice", "TestMaster"}, {"messages", {restore_cmd}}};
        client.send(restore_req.dump());
        
        // Don't wait for response as this is just cleanup
    }
};

int main(int argc, char* argv[]) {
    // Initialize log file with timestamp
    string timestamp = get_timestamp();
    replace(timestamp.begin(), timestamp.end(), ':', '-');  // Windows filename compatibility
    replace(timestamp.begin(), timestamp.end(), ' ', '_');
    string log_filename = "virgil_test_" + timestamp + ".log";
    
    log_file.open(log_filename, ios::out | ios::app);
    if (!log_file.is_open()) {
        cerr << "Warning: Could not open log file " << log_filename << endl;
    } else {
        cout << "Logging detailed output to: " << log_filename << endl;
        log_detailed("INFO", "STARTUP", "Virgil Slave Tester started", "Log file: " + log_filename);
    }
    
    string ip;
    if (argc < 2) {
        cout << "Enter the slave's IP address: ";
        getline(cin, ip);
        if (ip.empty()) {
            cout << "No IP provided. Exiting." << endl;
            log_detailed("ERROR", "STARTUP", "No IP address provided", "Exiting application");
            return 1;
        }
    } else {
        ip = argv[1];
    }
    
    log_detailed("INFO", "STARTUP", "Target IP address set", "IP: " + ip);
    
    // mDNS scan before protocol tests
    DeviceIdentity identity;
    scan_mdns(ip, identity);
    try {
        VirgilTester tester(ip, identity);
        tester.run_all_tests();
        log_detailed("INFO", "SHUTDOWN", "All tests completed successfully", "Test run finished");
    } catch (const exception& ex) {
        cerr << "Fatal error: " << ex.what() << endl;
        log_detailed("FATAL", "SHUTDOWN", "Fatal error occurred", "Error: " + string(ex.what()));
        return 2;
    }
    
    log_detailed("INFO", "SHUTDOWN", "Virgil Slave Tester finished", "Application exit");
    if (log_file.is_open()) {
        log_file.close();
    }
    return 0;
}