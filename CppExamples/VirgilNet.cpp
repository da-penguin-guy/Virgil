#include "VirgilNet.hpp"
#include <iostream>

void CloseSocket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

socket_t CreateSocket(int port) {
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "Socket creation failed." << std::endl;
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
        std::cerr << "Bind failed." << std::endl;
        CloseSocket(sock);
        return -1;
    }
    return sock;
}


void SendUdp(const json& message, const std::string& ip, const int port) {
    socket_t sock = CreateSocket(port);
    if (sock < 0) return;
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &dest.sin_addr);
    std::string payload = message.dump();

    // Log the response being sent
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dest.sin_addr, ipstr, sizeof(ipstr));

    std::string formatted_payload = payload;
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

void SendUdp(const json& message, const sockaddr_in& dest) {
    int port = ntohs(dest.sin_port);
    socket_t sock = CreateSocket(port);
    if (sock < 0) return;
    std::string payload = message.dump();

    // Log the response being sent
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dest.sin_addr, ipstr, sizeof(ipstr));

    std::string formatted_payload = payload;
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

void SendMulticast(const json& message, const string& multicast_ip, const int port) {
    socket_t sock = CreateSocket(port);
    if (sock < 0) return;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, multicast_ip.c_str(), &addr.sin_addr);
    std::string payload = message.dump();

    std::string formatted_payload = payload;
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
