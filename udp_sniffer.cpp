#include <iostream>


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

using namespace std;

constexpr int VIRGIL_PORT = 7889;

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

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        cerr << "WSAStartup failed." << endl;
        return 0;
    }
#endif
    socket_t sock = CreateUdpSocket(VIRGIL_PORT);
    if (sock < 0) return 1;
    cout << "[sniffer] Listening for UDP packets on port " << VIRGIL_PORT << "..." << endl;
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
            char ipstr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src_addr.sin_addr, ipstr, sizeof(ipstr));
            cout << "[sniffer] Received UDP packet from " << ipstr << ":" << ntohs(src_addr.sin_port) << ":\n";
            cout << buffer << endl << endl;
        }
    }
    CloseSocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
