#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "input_command.h"

namespace rtc {
class PeerConnection;
class DataChannel;
class Description;
class Candidate;
} // namespace rtc

namespace datatransfer {

class DataChannelSession {
public:
    explicit DataChannelSession(bool serverMode);
    ~DataChannelSession() = default;

    DataChannelSession(const DataChannelSession&) = delete;
    DataChannelSession& operator=(const DataChannelSession&) = delete;

    void SetSignalSender(std::function<void(const std::string&)> sender);
    void SetCommandReceiver(std::function<void(const InputCommand&)> receiver);
    void Start();
    void HandleSignalLine(const std::string& line);
    void SendCommand(const InputCommand& command);
    void WaitForOpen() const;
    bool IsOpen() const;

private:
    void CreatePeerConnection();
    void AttachDataChannel(const std::shared_ptr<rtc::DataChannel>& dataChannel);
    void OnLocalDescription(const rtc::Description& description);
    void OnLocalCandidate(const rtc::Candidate& candidate);
    void OnDataChannelOpen();
    std::function<void(const std::string&)> GetSignalSender() const;

    bool m_serverMode = false;
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::DataChannel> m_dataChannel;

    mutable std::mutex m_mutex;
    mutable std::condition_variable m_openCondition;
    std::function<void(const std::string&)> m_signalSender;
    std::function<void(const InputCommand&)> m_commandReceiver;
    bool m_open = false;
};

} // namespace datatransfer
