#pragma once

#include <QByteArray>

#include <cstdint>
#include <optional>

namespace datatransfer {

struct VideoFramePacketHeader {
    std::uint32_t payloadSize = 0;
    std::uint32_t frameIndex = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t timestampMs = 0;
};

constexpr std::uint32_t kVideoFrameMagic = 0x31444656U; // VFD1
constexpr int kVideoFrameHeaderSize = 28;

QByteArray SerializeVideoFramePacket(
    const VideoFramePacketHeader& header,
    const QByteArray& jpegPayload);

std::optional<VideoFramePacketHeader> TryParseVideoFrameHeader(const QByteArray& buffer);
std::uint32_t ReadVideoFramePayloadSize(const QByteArray& buffer);

} // namespace datatransfer
