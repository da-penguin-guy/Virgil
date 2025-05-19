#include <iostream>
#include <nlohmann/json.hpp>
#include <winsock2.h>
#include <string>
#include <cstdlib>
#include <ctime>
#include <ws2tcpip.h>

#define MDNS_ADDRESS "224.0.0.251"
#define MDNS_PORT 5353
#define SERVICE_PORT 7889
#define BASE_MULTICAST_ADDR "244.1.1"
#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;

WSADATA InitWSA()
{
    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        std::cerr << "WSAStartup failed: " << wsa_result << "\n";
    }
    return WSADATA
}

SOCKET createSocket(const std::string& ip, uint16_t port, int protocol) {
    // Initialize Winsock
    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        std::cerr << "WSAStartup failed: " << wsa_result << "\n";
        return INVALID_SOCKET;
    }

    SOCKET sockfd = INVALID_SOCKET;
    int socket_type = 0;
    
    if (protocol == IPPROTO_UDP) {
        socket_type = SOCK_DGRAM;
    } else if (protocol == IPPROTO_TCP) {
        socket_type = SOCK_STREAM;
    } else {
        std::cerr << "Unsupported protocol: " << protocol << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    // Create the socket based on the protocol
    sockfd = socket(AF_INET, socket_type, protocol_type);
    if (sockfd == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!" << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    // Set up the sockaddr_in structure to bind the socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);  // Convert port to network byte order
    addr.sin_addr.s_addr = inet_addr(ip.c_str());  // Convert IP to network byte order

    // Bind the socket to the specified IP and port
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed!" << std::endl;
        closesocket(sockfd);
        WSACleanup();
        return INVALID_SOCKET;
    }

    std::cout << "Socket created and bound to " << ip << ":" << port << " using " << protocol << " protocol." << std::endl;

    return sockfd;
}

void sendTcpJsonMessage(const char* server_ip, int server_port, const json& json_message) {
    // Set up the server address
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // Connect to the server
    if (connect(sock, (SOCKADDR*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connection failed.\n";
        closesocket(sock);
        WSACleanup();
        return;
    }

    // Convert the JSON message to a string and send it
    std::string message = json_message.dump();
    size_t message_len = message.size();
    
    int send_result = send(sock, message.c_str(), static_cast<int>(message_len), 0);
    if (send_result == SOCKET_ERROR) {
        std::cerr << "Send failed.\n";
    } else {
        std::cout << "Sent JSON message: " << message << "\n";
    }

    // Cleanup
    closesocket(sock);
    WSACleanup();
}

void sendMdnsQuery(SOCKET sockfd, const std::string& query) {
    struct sockaddr_in multicastAddr;
    memset(&multicastAddr, 0, sizeof(multicastAddr));
    multicastAddr.sin_family = AF_INET;
    multicastAddr.sin_addr.s_addr = inet_addr(MDNS_ADDRESS);  // mDNS Multicast address
    multicastAddr.sin_port = htons(MDNS_PORT);

    // Send the mDNS query
    int bytesSent = sendto(sockfd, query.c_str(), query.size(), 0, (struct sockaddr *)&multicastAddr, sizeof(multicastAddr));
    if (bytesSent == SOCKET_ERROR) {
        std::cerr << "Error sending mDNS query" << std::endl;
    } else {
        std::cout << "mDNS query sent!" << std::endl;
    }
}

void advertiseMdnsService(SOCKET sockfd, const std::string& multicastAddr, const std::string& danteName, const std::string& function) {
    // Set the socket options to allow multicast
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes)) == SOCKET_ERROR) {
        std::cerr << "Error setting socket options" << std::endl;
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    // Bind the socket to the mDNS port
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(MDNS_PORT);
    if (bind(sockfd, (struct sockaddr *)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        std::cerr << "Error binding socket" << std::endl;
        closesocket(sockfd);
        WSACleanup();
        return 1;
    }

    struct sockaddr_in multicastServiceAddr;
    memset(&multicastServiceAddr, 0, sizeof(multicastServiceAddr));
    multicastServiceAddr.sin_family = AF_INET;
    multicastServiceAddr.sin_addr.s_addr = inet_addr(multicastAddr.c_str());  // Custom multicast address for this slave
    multicastServiceAddr.sin_port = htons(MDNS_PORT);

    // mDNS service query for "_virgil._udp.local."
    std::string serviceName = danteName + "._virgil._udp.local.";
    std::string txtRecord = "multicastAddr=" + multicastAddr + "&function=" + function;

    // Create the mDNS service advertisement (simplified format)
    std::string serviceMessage =
        "\x00\x00"      // Transaction ID
        "\x00\x00"      // Flags
        "\x00\x01"      // Questions: 1
        "\x00\x00"      // Answers: 0
        "\x00\x00"      // Authority: 0
        "\x00\x00"      // Additional: 0
        + serviceName + "\x00"   // Service name
        "\x00\x11"      // Type: PTR (pointer)
        "\x00\x01"      // Class: IN (Internet)
        "\x00\x10"      // Length of TXT record
        + txtRecord;    // TXT record: multicastAddr and function

    // Send the mDNS service advertisement
    int bytesSent = sendto(sockfd, serviceMessage.c_str(), serviceMessage.size(), 0, (struct sockaddr *)&multicastServiceAddr, sizeof(multicastServiceAddr));
    if (bytesSent == SOCKET_ERROR) {
        std::cerr << "Error sending mDNS service advertisement" << std::endl;
    } else {
        std::cout << "mDNS service advertised with name: " << serviceName << std::endl;
    }
}

int main() {
    const char* ip = "127.0.0.1";  // IP address to send message to
    int virgilPort = 7889;
    cout << "What ip do you want to send to?"

    while true
    {
        cout << "What type of message do you want to send? \n
        c for command, and i for info request";
        input << cin;
        if(input == "c")
        {
            while true
            {

            }
        }
        else if(input == "i")
        {

        }
        else
        {
            cout << "That is not an accepted input. Please try again \n \n"
        }
    }

    // Create a simple JSON message using nlohmann/json
    json json_message = {
        {"name", "John"},
        {"age", 30},
        {"city", "New York"}
    };

    // Send the JSON message to the server
    sendTcpJsonMessage(ip, virgilPort, json_message);

    return 0;
    WSADATA wsaData;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsa_result != 0) {
        std::cerr << "WSAStartup failed: " << wsa_result << "\n";
        return 1;
    }

    // Create a UDP socket
    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!" << std::endl;
        WSACleanup();
        return 1;
    }
    

    // Service name (example: "Master1" as dante_name)
    std::string danteName = "Master1";
    std::string function = "master";  // The function for this device

    // Advertise the service over mDNS
    advertiseMdnsService(sockfd, multicastAddr, danteName, function);

    // Cleanup
    closesocket(sockfd);
    WSACleanup();
    return 0;
}
