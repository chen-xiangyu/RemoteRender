#include "video_frame_protocol.h"

#include <stdexcept>

namespace datatransfer {

namespace {

void AppendU32(QByteArray& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

void AppendU64(QByteArray& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

std::uint32_t ReadU32(const QByteArray& input, int offset) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset + index])) << (index * 8);
    }
    return value;
}

std::uint64_t ReadU64(const QByteArray& input, int offset) {
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(input[offset + index])) << (index * 8);
    }
    return value;
}

} // namespace

QByteArray SerializeVideoFramePacket(
    const VideoFramePacketHeader& header,
    const QByteArray& jpegPayload) {
    if (header.payloadSize != static_cast<std::uint32_t>(jpegPayload.size())) {
        throw std::runtime_error("Video frame payload size mismatch");
    }

    QByteArray packet;
    packet.reserve(kVideoFrameHeaderSize + jpegPayload.size());

    AppendU32(packet, kVideoFrameMagic);
    AppendU32(packet, header.payloadSize);
    AppendU32(packet, header.frameIndex);
    AppendU32(packet, header.width);
    AppendU32(packet, header.height);
    AppendU64(packet, header.timestampMs);
    packet.append(jpegPayload);

    return packet;
}

std::optional<VideoFramePacketHeader> TryParseVideoFrameHeader(const QByteArray& buffer) {
    if (buffer.size() < kVideoFrameHeaderSize) {
        return std::nullopt;
    }

    if (ReadU32(buffer, 0) != kVideoFrameMagic) {
        throw std::runtime_error("Invalid video frame magic");
    }

    VideoFramePacketHeader header;
    header.payloadSize = ReadU32(buffer, 4);
    header.frameIndex = ReadU32(buffer, 8);
    header.width = ReadU32(buffer, 12);
    header.height = ReadU32(buffer, 16);
    header.timestampMs = ReadU64(buffer, 20);
    return header;
}

std::uint32_t ReadVideoFramePayloadSize(const QByteArray& buffer) {
    const auto header = TryParseVideoFrameHeader(buffer);
    return header ? header->payloadSize : 0U;
}

} // namespace datatransfer
