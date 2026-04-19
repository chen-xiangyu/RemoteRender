#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace datatransfer {

class TcpSignalingChannel {
public:
    using SocketHandle = long long;

    TcpSignalingChannel();
    ~TcpSignalingChannel();

    TcpSignalingChannel(const TcpSignalingChannel&) = delete;
    TcpSignalingChannel& operator=(const TcpSignalingChannel&) = delete;

    void StartServer(int port);
    void ConnectTo(const std::string& host, int port);
    void StartReceiveLoop(std::function<void(const std::string&)> onLine);
    void SendLine(const std::string& line);
    void Close();

private:
    static SocketHandle CreateSocket();
    static void CloseSocket(SocketHandle socketHandle);
    static void InitializeSockets();
    static void CleanupSockets();

    SocketHandle m_socket = -1;
    std::thread m_receiveThread;
    std::mutex m_sendMutex;
    std::atomic_bool m_running{false};
};

} // namespace datatransfer
