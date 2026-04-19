#pragma once

#include <QImage>

#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace datatransfer {

class FfmpegH264Decoder {
public:
    FfmpegH264Decoder();
    ~FfmpegH264Decoder();

    FfmpegH264Decoder(const FfmpegH264Decoder&) = delete;
    FfmpegH264Decoder& operator=(const FfmpegH264Decoder&) = delete;

    bool Decode(const std::vector<std::uint8_t>& annexBFrame, QImage& image);

private:
    AVCodecContext* m_codecContext = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_swsContext = nullptr;
};

} // namespace datatransfer
