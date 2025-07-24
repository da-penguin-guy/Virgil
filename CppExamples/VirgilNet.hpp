#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include "nlohmann/json.hpp"
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

socket_t CreateSocket(int port);

void SendUdp(const json& message, const string& ip, const int port = VIRGIL_PORT);

void SendUdp(const json& message, const sockaddr_in& dest);

void SendMulticast(const json& message, const string& multicast_ip, const int port = VIRGIL_PORT);

void CloseSocket(socket_t sock);
