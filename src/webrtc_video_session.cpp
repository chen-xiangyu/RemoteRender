#include "webrtc_video_session.h"

#include "base64.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <rtc/h264rtpdepacketizer.hpp>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/nalunit.hpp>
#include <rtc/rtc.hpp>
#include <rtc/rtcpreceivingsession.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtppacketizationconfig.hpp>

namespace datatransfer {

namespace {

std::vector<std::string> Split(const std::string& input, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::uint32_t GenerateRandomSsrc() {
    static std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<std::uint32_t> distribution(1U, 0x7fffffffU);
    return distribution(generator);
}

} // namespace

WebRtcVideoSession::WebRtcVideoSession(bool senderMode)
    : m_senderMode(senderMode) {
    rtc::InitLogger(rtc::LogLevel::Info);
    CreatePeerConnection();
}

void WebRtcVideoSession::SetSignalSender(std::function<void(const std::string&)> sender) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_signalSender = std::move(sender);
}

void WebRtcVideoSession::SetVideoFrameReceiver(
    std::function<void(const std::vector<std::uint8_t>&, std::uint64_t)> receiver) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_videoFrameReceiver = std::move(receiver);
}

void WebRtcVideoSession::Start() {
    if (m_senderMode) {
        CreateSenderTrack();
    } else {
        CreateReceiverTrack();
    }
    m_peerConnection->setLocalDescription();
}

void WebRtcVideoSession::Close() {
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::PeerConnection> peerConnection;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        track = m_track;
        peerConnection = m_peerConnection;
        m_trackOpen = false;
    }

    if (track) {
        track->close();
    }
    if (peerConnection) {
        peerConnection->close();
    }

    m_openCondition.notify_all();
}

void WebRtcVideoSession::HandleSignalLine(const std::string& line) {
    const auto parts = Split(line, '|');
    if (parts.empty()) {
        return;
    }

    if (parts[0] == "SDP" && parts.size() == 3U) {
        const auto type = Base64Decode(parts[1]);
        const auto sdp = Base64Decode(parts[2]);
        rtc::Description description(sdp, type);
        const bool remoteOffer = description.type() == rtc::Description::Type::Offer;
        m_peerConnection->setRemoteDescription(description);
        if (!m_senderMode && remoteOffer) {
            m_peerConnection->setLocalDescription();
        }
        return;
    }

    if (parts[0] == "CAND" && parts.size() == 3U) {
        const auto mid = Base64Decode(parts[1]);
        const auto candidate = Base64Decode(parts[2]);
        m_peerConnection->addRemoteCandidate(rtc::Candidate(candidate, mid));
        return;
    }

    throw std::runtime_error("Unsupported signaling line: " + line);
}

void WebRtcVideoSession::WaitForTrackOpen() const {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_openCondition.wait(lock, [this]() { return m_trackOpen; });
}

bool WebRtcVideoSession::IsTrackOpen() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_trackOpen;
}

void WebRtcVideoSession::SendVideoSample(
    const std::vector<std::uint8_t>& annexBFrame,
    std::uint64_t timestampUs) {
    std::shared_ptr<rtc::Track> track;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_trackOpen || !m_track) {
            throw std::runtime_error("Video track is not open");
        }
        track = m_track;
    }

    rtc::binary binary;
    binary.reserve(annexBFrame.size());
    for (const auto byteValue : annexBFrame) {
        binary.push_back(static_cast<std::byte>(byteValue));
    }

    track->sendFrame(binary, std::chrono::duration<double, std::micro>(timestampUs));
}

void WebRtcVideoSession::CreatePeerConnection() {
    rtc::Configuration config;
    config.iceServers.clear();

    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[webrtc-video] peer state " << static_cast<int>(state) << std::endl;
    });

    m_peerConnection->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        std::cout << "[webrtc-video] gathering state " << static_cast<int>(state) << std::endl;
    });

    m_peerConnection->onLocalDescription([this](rtc::Description description) {
        OnLocalDescription(description);
    });

    m_peerConnection->onLocalCandidate([this](rtc::Candidate candidate) {
        OnLocalCandidate(candidate);
    });

    m_peerConnection->onTrack([this](const std::shared_ptr<rtc::Track>& track) {
        std::cout << "[webrtc-video] remote track received: " << track->mid() << std::endl;
        if (m_senderMode) {
            return;
        }
        bool shouldAttach = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            shouldAttach = !m_track;
        }
        if (shouldAttach) {
            ConfigureReceiverTrack(track);
            AttachTrack(track);
        }
    });
}

void WebRtcVideoSession::CreateSenderTrack() {
    const std::uint32_t ssrc = GenerateRandomSsrc();
    const std::string cname = "remote-render-video";
    const std::string streamId = "remote-render-stream";
    constexpr std::uint8_t payloadType = 96;

    rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
    video.addH264Codec(payloadType);
    video.addSSRC(ssrc, cname, streamId, cname);
    video.setBitrate(4000);

    const auto track = m_peerConnection->addTrack(video);

    m_rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        ssrc,
        cname,
        payloadType,
        rtc::H264RtpPacketizer::ClockRate);

    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::StartSequence,
        m_rtpConfig);
    m_srReporter = std::make_shared<rtc::RtcpSrReporter>(m_rtpConfig);
    packetizer->addToChain(m_srReporter);
    packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    track->setMediaHandler(packetizer);

    AttachTrack(track);
}

void WebRtcVideoSession::CreateReceiverTrack() {
    rtc::Description::Video video("video", rtc::Description::Direction::RecvOnly);
    video.addH264Codec(96);
    video.setBitrate(4000);
    const auto track = m_peerConnection->addTrack(video);
    ConfigureReceiverTrack(track);
    AttachTrack(track);
}

void WebRtcVideoSession::ConfigureReceiverTrack(const std::shared_ptr<rtc::Track>& track) {
    auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
    auto receivingSession = std::make_shared<rtc::RtcpReceivingSession>();
    depacketizer->addToChain(receivingSession);
    track->setMediaHandler(depacketizer);

    track->onFrame([this](rtc::binary data, rtc::FrameInfo info) {
        std::vector<std::uint8_t> frameBytes;
        frameBytes.reserve(data.size());
        for (const auto value : data) {
            frameBytes.push_back(std::to_integer<std::uint8_t>(value));
        }

        std::function<void(const std::vector<std::uint8_t>&, std::uint64_t)> receiver;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            receiver = m_videoFrameReceiver;
        }

        std::uint64_t timestampUs = 0;
        if (info.timestampSeconds.has_value()) {
            timestampUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    info.timestampSeconds.value())
                    .count());
        } else {
            timestampUs = info.timestamp;
        }

        if (receiver) {
            receiver(frameBytes, timestampUs);
        }
    });
}

void WebRtcVideoSession::AttachTrack(const std::shared_ptr<rtc::Track>& track) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_track = track;
    }

    track->onOpen([this]() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_trackOpen = true;
        }
        m_openCondition.notify_all();
        std::cout << "[webrtc-video] track open" << std::endl;
    });

    track->onClosed([this]() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_trackOpen = false;
        }
        std::cout << "[webrtc-video] track closed" << std::endl;
    });
}

void WebRtcVideoSession::OnLocalDescription(const rtc::Description& description) {
    const auto sender = GetSignalSender();
    if (!sender) {
        throw std::runtime_error("Signal sender must be configured before negotiation starts");
    }

    const std::string payload =
        "SDP|" +
        Base64Encode(description.typeString()) +
        "|" +
        Base64Encode(std::string(description));

    sender(payload);
}

void WebRtcVideoSession::OnLocalCandidate(const rtc::Candidate& candidate) {
    const auto sender = GetSignalSender();
    if (!sender) {
        throw std::runtime_error("Signal sender must be configured before negotiation starts");
    }

    const std::string payload =
        "CAND|" +
        Base64Encode(candidate.mid()) +
        "|" +
        Base64Encode(std::string(candidate));

    sender(payload);
}

std::function<void(const std::string&)> WebRtcVideoSession::GetSignalSender() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_signalSender;
}

} // namespace datatransfer
