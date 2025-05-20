#include <iostream>
#include <cstring>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

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

atomic<bool> net_listener_running{false};
thread net_listener_thread;

// This function will be called with received data and sender info
using PacketHandler = std::function<void(const std::string&, const sockaddr_in&)>;

int virgilPort = 7889;


socket_t CreateSocket(int type, int port, sockaddr_in& addr) 
{
    socket_t sock = socket(AF_INET, type, 0);
    if (sock < 0) 
    {
        cerr << "Socket creation failed.\n";
    }
    sockaddr_in addr;
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

    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_DGRAM, mdns_port, addr);

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

vector<pair<string, sockaddr_in>> ReadMDNS()
{
    const char* mdns_ip = "224.0.0.251";
    int mdns_port = 5353;

    vector<pair<string, sockaddr_in>> results;

    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_DGRAM, mdns_port, addr);
    if (sock < 0) 
    {
        cerr << "mDNS read socket creation failed.\n";
        return results;
    }

    if (::bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        cerr << "mDNS bind failed.\n";
        CloseSocket(sock);
        return results;
    }

    // Join mDNS multicast group
    ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(mdns_ip);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));

    char buffer[4096];
    auto start = chrono::steady_clock::now();
    while (chrono::steady_clock::now() - start < chrono::seconds(5)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000; // 0.5 second

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
                results.emplace_back(std::string(buffer, len), src_addr);
            }
        }
    }

    CloseSocket(sock);
    return results;
}

void StartMDNS(const string& danteName, const string& function, const string& multicastBase = "") 
{
    mdns_running = true;
    mdns_thread = std::thread(AdvertiseMDNS, danteName, function, virgilPort, multicastBase);
}

void StopMDNS() 
{
    mdns_running = false;
    if (mdns_thread.joinable()) {
        mdns_thread.join();
    }
}

bool SendTCP(const string& ip, int port, json message)
{
    sockaddr_in addr;
    socket_t sock = CreateSocket(SOCK_STREAM, port, addr);

    // Set the destination IP address
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) 
    {
        cerr << "TCP connect failed.\n";
        CloseSocket(sock);
        return false;
    }
    std::string messageDump = message.dump();
    int send_result = send(sock, messageDump.c_str(), messageDump.size(), 0);
    if (send_result < 0) 
    {
        cerr << "TCP send failed.\n";
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
    addr.sin_addr.s_addr = inet_addr(multicast_ip.c_str());

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
        mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
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
    net_listener_thread = std::thread(NetListener, port, multicast_ips, handler);
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
    cout << "Received from " << inet_ntoa(src.sin_addr) << ":" << ntohs(src.sin_port)
         << " - Data: " << data << endl;
    json j = json::parse(data);
}

int main() 
{
    //Windows Boilerplate
    #ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
        {
            cerr << "WSAStartup failed.\n";
            return 1;
        }
    #endif
    //starts advertising mDNS (Not needed for master, but is highly recommended)
    StartMDNS("ExampleDevice", "master");
    
    //Here, we have a preexisting list of Dante devices we are subscribed to
    //You would get this from your preexisting Dante code
    //I'm just making up a format to make my life easier
    //This has names, a bool for virgil, a multicast address, and a list of preamps we care about
    vector<json> danteLookup;
    //Read mDNS packets for 5 seconds
    auto mdns_packets = ReadMDNS();
    //Loop over all packets
    for (const auto& [data, src] : mdns_packets) 
    {
        //Filter for virgil slaves only
        json j = json::parse(data);
        if (j["serviceType"] == "_virgil._udp.local." && j["txt"]["function"] == "slave") 
        {
            //Search for the device in our dante lookup
            bool found = false;
            for (auto& device : danteLookup) 
            {
                string serviceName = j["serviceName"];
                string serviceType = j["serviceType"];
                string danteName = serviceName.erase(serviceName.find(serviceType),serviceName.length());
                if(device["name"] == danteName)
                {
                    found = true;
                    device["multicast"] = j["txt"]["multicast"];
                    device["virgil"] = true;
                    break;
                }
            }
        }
    }
    //Find all the multicast addresses we care about
    vector<string> multicast_addresses;
    for(auto& device : danteLookup) 
    {
        if(device["virgil"] == false)
        {
            continue;
        }
        for(int preamp : device["preamps"]) 
        {
            multicast_addresses.push_back(device["multicast"] + "." + to_string(preamp));
        }
    }
    //Start the listener
    //It's important to start the net listener before sending info requests, otherwise you may miss packets
    StartNetListener(virgilPort, multicast_addresses, ProcessPacket);
    for(auto& device : danteLookup) 
    {
        if(device["virgil"] == false)
        {
            continue;
        }
        //Send a request to the device
        json request;
        request["transmittingDevice"] = "ExampleDevice";
        request["receivingDevice"] = device["name"];
        request["messages"] = json::array();

        for (int preamp : device["preamps"]) 
        {
            request["messages"].push_back({{"messageType", "statusRequest"},{"preampIndex", preamp}});
        }
        SendTCP(device["ip"], virgilPort, request);
    }

    //Cleanup
    #ifdef _WIN32
    WSACleanup();
    #endif
    StopNetListener();
    StopMDNS();
    return 0;
}