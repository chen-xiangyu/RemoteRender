#include "tcp_signaling_channel.h"

#include <array>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <WinSock2.h>
#include <Ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace datatransfer {

namespace {

std::once_flag g_socketInitFlag;
std::atomic_int g_socketUserCount{0};

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

NativeSocket ToNativeSocket(TcpSignalingChannel::SocketHandle handle) {
    return static_cast<NativeSocket>(handle);
}

TcpSignalingChannel::SocketHandle FromNativeSocket(NativeSocket socketHandle) {
    return static_cast<TcpSignalingChannel::SocketHandle>(socketHandle);
}

std::string DescribeLastSocketError() {
#ifdef _WIN32
    return "socket error code=" + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

} // namespace

TcpSignalingChannel::TcpSignalingChannel() {
    InitializeSockets();
}

TcpSignalingChannel::~TcpSignalingChannel() {
    Close();
    CleanupSockets();
}

void TcpSignalingChannel::StartServer(int port) {
    NativeSocket listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == kInvalidSocket) {
        throw std::runtime_error("Failed to create listen socket: " + DescribeLastSocketError());
    }

    const int reuse = 1;
#ifdef _WIN32
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(port));

    if (::bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        CloseSocket(FromNativeSocket(listenSocket));
        throw std::runtime_error("Failed to bind listen socket: " + DescribeLastSocketError());
    }

    if (::listen(listenSocket, 1) != 0) {
        CloseSocket(FromNativeSocket(listenSocket));
        throw std::runtime_error("Failed to listen on socket: " + DescribeLastSocketError());
    }

    std::cout << "[signaling] waiting for client on TCP port " << port << "..." << std::endl;

    sockaddr_in clientAddress{};
#ifdef _WIN32
    int clientLength = sizeof(clientAddress);
#else
    socklen_t clientLength = sizeof(clientAddress);
#endif
    NativeSocket clientSocket = ::accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
    CloseSocket(FromNativeSocket(listenSocket));

    if (clientSocket == kInvalidSocket) {
        throw std::runtime_error("Failed to accept client: " + DescribeLastSocketError());
    }

    m_socket = FromNativeSocket(clientSocket);
    std::cout << "[signaling] client connected" << std::endl;
}

void TcpSignalingChannel::ConnectTo(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const auto portText = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0) {
        throw std::runtime_error("Failed to resolve host: " + host);
    }

    NativeSocket connectedSocket = kInvalidSocket;
    for (addrinfo* current = result; current != nullptr; current = current->ai_next) {
        connectedSocket = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (connectedSocket == kInvalidSocket) {
            continue;
        }

        if (::connect(connectedSocket, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
            break;
        }

        CloseSocket(FromNativeSocket(connectedSocket));
        connectedSocket = kInvalidSocket;
    }

    ::freeaddrinfo(result);

    if (connectedSocket == kInvalidSocket) {
        throw std::runtime_error("Failed to connect to signaling server: " + DescribeLastSocketError());
    }

    m_socket = FromNativeSocket(connectedSocket);
    std::cout << "[signaling] connected to " << host << ":" << port << std::endl;
}

void TcpSignalingChannel::StartReceiveLoop(std::function<void(const std::string&)> onLine) {
    if (m_socket == -1) {
        throw std::runtime_error("Cannot start receive loop before socket is connected");
    }

    m_running = true;
    m_receiveThread = std::thread([this, onLine = std::move(onLine)]() {
        std::array<char, 2048> buffer{};
        std::string pending;

        while (m_running) {
            const int received = ::recv(ToNativeSocket(m_socket), buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received <= 0) {
                m_running = false;
                break;
            }

            pending.append(buffer.data(), static_cast<std::size_t>(received));

            std::size_t lineEnd = 0;
            while ((lineEnd = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, lineEnd);
                pending.erase(0, lineEnd + 1);
                if (!line.empty()) {
                    onLine(line);
                }
            }
        }
    });
}

void TcpSignalingChannel::SendLine(const std::string& line) {
    if (m_socket == -1) {
        throw std::runtime_error("Cannot send signaling line before socket is connected");
    }

    const std::string payload = line + "\n";
    std::lock_guard<std::mutex> lock(m_sendMutex);

    std::size_t sentTotal = 0;
    while (sentTotal < payload.size()) {
        const int sent = ::send(
            ToNativeSocket(m_socket),
            payload.data() + sentTotal,
            static_cast<int>(payload.size() - sentTotal),
            0);
        if (sent <= 0) {
            throw std::runtime_error("Failed to send signaling data: " + DescribeLastSocketError());
        }
        sentTotal += static_cast<std::size_t>(sent);
    }
}

void TcpSignalingChannel::Close() {
    m_running = false;

    if (m_socket != -1) {
#ifdef _WIN32
        ::shutdown(ToNativeSocket(m_socket), SD_BOTH);
#else
        ::shutdown(ToNativeSocket(m_socket), SHUT_RDWR);
#endif
        CloseSocket(m_socket);
        m_socket = -1;
    }

    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }
}

TcpSignalingChannel::SocketHandle TcpSignalingChannel::CreateSocket() {
    return FromNativeSocket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
}

void TcpSignalingChannel::CloseSocket(SocketHandle socketHandle) {
    if (socketHandle == -1) {
        return;
    }

#ifdef _WIN32
    ::closesocket(ToNativeSocket(socketHandle));
#else
    ::close(ToNativeSocket(socketHandle));
#endif
}

void TcpSignalingChannel::InitializeSockets() {
    std::call_once(g_socketInitFlag, []() {
#ifdef _WIN32
        WSADATA data{};
        const int startupResult = ::WSAStartup(MAKEWORD(2, 2), &data);
        if (startupResult != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    });

    ++g_socketUserCount;
}

void TcpSignalingChannel::CleanupSockets() {
    if (--g_socketUserCount == 0) {
#ifdef _WIN32
        ::WSACleanup();
#endif
    }
}

} // namespace datatransfer
