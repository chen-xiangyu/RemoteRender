#include "data_channel_session.h"

#include "base64.h"

#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include <rtc/rtc.hpp>

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

} // namespace

DataChannelSession::DataChannelSession(bool serverMode)
    : m_serverMode(serverMode) {
    rtc::InitLogger(rtc::LogLevel::Info);
    CreatePeerConnection();
}

void DataChannelSession::SetSignalSender(std::function<void(const std::string&)> sender) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_signalSender = std::move(sender);
}

void DataChannelSession::SetCommandReceiver(std::function<void(const InputCommand&)> receiver) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_commandReceiver = std::move(receiver);
}

void DataChannelSession::Start() {
    if (!m_serverMode) {
        return;
    }

    auto dataChannel = m_peerConnection->createDataChannel("lan-chat");
    AttachDataChannel(dataChannel);
}

void DataChannelSession::HandleSignalLine(const std::string& line) {
    const auto parts = Split(line, '|');
    if (parts.empty()) {
        return;
    }

    if (parts[0] == "SDP" && parts.size() == 3U) {
        const auto type = Base64Decode(parts[1]);
        const auto sdp = Base64Decode(parts[2]);
        std::cout << "[signal] remote " << type << " received" << std::endl;
        m_peerConnection->setRemoteDescription(rtc::Description(sdp, type));
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

void DataChannelSession::SendCommand(const InputCommand& command) {
    std::shared_ptr<rtc::DataChannel> dataChannel;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        dataChannel = m_dataChannel;
        if (!dataChannel || !m_open) {
            throw std::runtime_error("Data channel is not open");
        }
    }

    const auto bytes = SerializeCommand(command);
    rtc::binary binary;
    binary.reserve(bytes.size());
    for (const auto value : bytes) {
        binary.push_back(static_cast<std::byte>(value));
    }
    dataChannel->send(binary);
}

void DataChannelSession::WaitForOpen() const {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_openCondition.wait(lock, [this]() { return m_open; });
}

bool DataChannelSession::IsOpen() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_open;
}

void DataChannelSession::CreatePeerConnection() {
    rtc::Configuration config;
    config.iceServers.clear();

    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[peer] state changed to " << static_cast<int>(state) << std::endl;
    });

    m_peerConnection->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
        std::cout << "[peer] gathering state " << static_cast<int>(state) << std::endl;
    });

    m_peerConnection->onLocalDescription([this](rtc::Description description) {
        OnLocalDescription(description);
    });

    m_peerConnection->onLocalCandidate([this](rtc::Candidate candidate) {
        OnLocalCandidate(candidate);
    });

    m_peerConnection->onDataChannel([this](const std::shared_ptr<rtc::DataChannel>& dataChannel) {
        std::cout << "[peer] remote data channel created: " << dataChannel->label() << std::endl;
        AttachDataChannel(dataChannel);
    });
}

void DataChannelSession::AttachDataChannel(const std::shared_ptr<rtc::DataChannel>& dataChannel) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dataChannel = dataChannel;
    }

    dataChannel->onOpen([this]() {
        OnDataChannelOpen();
    });

    dataChannel->onClosed([this]() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_open = false;
        std::cout << "[datachannel] closed" << std::endl;
    });

    dataChannel->onMessage([this](rtc::message_variant message) {
        if (const auto* text = std::get_if<std::string>(&message)) {
            std::cout << "[peer-text] " << *text << std::endl;
            return;
        }

        if (const auto* binary = std::get_if<rtc::binary>(&message)) {
            std::vector<std::uint8_t> bytes;
            bytes.reserve(binary->size());
            for (const auto value : *binary) {
                bytes.push_back(std::to_integer<std::uint8_t>(value));
            }
            const auto command = DeserializeCommand(bytes);

            std::function<void(const InputCommand&)> receiver;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                receiver = m_commandReceiver;
            }

            if (receiver) {
                receiver(command);
            } else {
                std::cout << "[peer-command] " << ToDisplayString(command) << std::endl;
            }
        }
    });
}

void DataChannelSession::OnLocalDescription(const rtc::Description& description) {
    const auto sender = GetSignalSender();
    if (!sender) {
        throw std::runtime_error("Signal sender must be configured before negotiation starts");
    }

    const std::string payload =
        "SDP|" +
        Base64Encode(description.typeString()) +
        "|" +
        Base64Encode(std::string(description));

    std::cout << "[signal] local " << description.typeString() << " generated" << std::endl;
    sender(payload);
}

void DataChannelSession::OnLocalCandidate(const rtc::Candidate& candidate) {
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

void DataChannelSession::OnDataChannelOpen() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_open = true;
    }
    m_openCondition.notify_all();
    std::cout << "[datachannel] open" << std::endl;
}

std::function<void(const std::string&)> DataChannelSession::GetSignalSender() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_signalSender;
}

} // namespace datatransfer
