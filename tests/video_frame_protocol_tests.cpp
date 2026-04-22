#include "video_frame_protocol.h"

#include <gtest/gtest.h>

namespace datatransfer {
namespace {

TEST(VideoFrameProtocolTests, SerializeAndParseHeaderRoundTripPreservesFields) {
    VideoFramePacketHeader header;
    header.payloadSize = 5;
    header.frameIndex = 42;
    header.width = 1920;
    header.height = 1080;
    header.timestampMs = 123456789ULL;

    const QByteArray payload("abcde", 5);
    const QByteArray packet = SerializeVideoFramePacket(header, payload);
    const auto parsed = TryParseVideoFrameHeader(packet);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->payloadSize, header.payloadSize);
    EXPECT_EQ(parsed->frameIndex, header.frameIndex);
    EXPECT_EQ(parsed->width, header.width);
    EXPECT_EQ(parsed->height, header.height);
    EXPECT_EQ(parsed->timestampMs, header.timestampMs);
    EXPECT_EQ(packet.mid(kVideoFrameHeaderSize), payload);
}

TEST(VideoFrameProtocolTests, SerializeRejectsPayloadSizeMismatch) {
    VideoFramePacketHeader header;
    header.payloadSize = 4;

    EXPECT_THROW(SerializeVideoFramePacket(header, QByteArray("abc", 3)), std::runtime_error);
}

TEST(VideoFrameProtocolTests, TryParseHeaderReturnsNulloptWhenBufferTooShort) {
    const QByteArray packet(kVideoFrameHeaderSize - 1, '\0');
    EXPECT_FALSE(TryParseVideoFrameHeader(packet).has_value());
}

TEST(VideoFrameProtocolTests, TryParseHeaderRejectsInvalidMagic) {
    QByteArray packet(kVideoFrameHeaderSize, '\0');
    packet[0] = '\x00';
    packet[1] = '\x00';
    packet[2] = '\x00';
    packet[3] = '\x00';

    EXPECT_THROW(TryParseVideoFrameHeader(packet), std::runtime_error);
}

TEST(VideoFrameProtocolTests, ReadPayloadSizeReturnsZeroWhenHeaderIncomplete) {
    const QByteArray packet(kVideoFrameHeaderSize - 2, '\0');
    EXPECT_EQ(ReadVideoFramePayloadSize(packet), 0U);
}

TEST(VideoFrameProtocolTests, ReadPayloadSizeExtractsPayloadSizeFromValidPacket) {
    VideoFramePacketHeader header;
    header.payloadSize = 7;
    header.frameIndex = 3;
    header.width = 640;
    header.height = 360;
    header.timestampMs = 88;

    const QByteArray packet = SerializeVideoFramePacket(header, QByteArray("1234567", 7));
    EXPECT_EQ(ReadVideoFramePayloadSize(packet), 7U);
}

} // namespace
} // namespace datatransfer
