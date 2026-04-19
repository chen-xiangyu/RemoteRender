#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace rtc {
class PeerConnection;
class Track;
class Description;
class Candidate;
class RtcpSrReporter;
class RtpPacketizationConfig;
} // namespace rtc

namespace datatransfer {

class WebRtcVideoSession {
public:
    explicit WebRtcVideoSession(bool senderMode);
    ~WebRtcVideoSession() = default;

    WebRtcVideoSession(const WebRtcVideoSession&) = delete;
    WebRtcVideoSession& operator=(const WebRtcVideoSession&) = delete;

    void SetSignalSender(std::function<void(const std::string&)> sender);
    void SetVideoFrameReceiver(std::function<void(const std::vector<std::uint8_t>&, std::uint64_t)> receiver);

    void Start();
    void HandleSignalLine(const std::string& line);
    void WaitForTrackOpen() const;
    bool IsTrackOpen() const;
    void SendVideoSample(const std::vector<std::uint8_t>& annexBFrame, std::uint64_t timestampUs);

private:
    void CreatePeerConnection();
    void CreateSenderTrack();
    void CreateReceiverTrack();
    void ConfigureReceiverTrack(const std::shared_ptr<rtc::Track>& track);
    void AttachTrack(const std::shared_ptr<rtc::Track>& track);
    void OnLocalDescription(const rtc::Description& description);
    void OnLocalCandidate(const rtc::Candidate& candidate);
    std::function<void(const std::string&)> GetSignalSender() const;

    bool m_senderMode = false;
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::Track> m_track;
    std::shared_ptr<rtc::RtpPacketizationConfig> m_rtpConfig;
    std::shared_ptr<rtc::RtcpSrReporter> m_srReporter;

    mutable std::mutex m_mutex;
    mutable std::condition_variable m_openCondition;
    std::function<void(const std::string&)> m_signalSender;
    std::function<void(const std::vector<std::uint8_t>&, std::uint64_t)> m_videoFrameReceiver;
    bool m_trackOpen = false;
};

} // namespace datatransfer
