#pragma once

#include <QImage>

#include <string>

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace datatransfer {

class FfmpegVideoReader {
public:
    explicit FfmpegVideoReader(const std::string& filePath);
    ~FfmpegVideoReader();

    FfmpegVideoReader(const FfmpegVideoReader&) = delete;
    FfmpegVideoReader& operator=(const FfmpegVideoReader&) = delete;

    bool ReadNextFrame(QImage& image);
    int FrameIntervalMs() const;
    int Width() const;
    int Height() const;

private:
    bool DecodeNextFrame(QImage& image);
    bool SeekToStart();

    AVFormatContext* m_formatContext = nullptr;
    AVCodecContext* m_codecContext = nullptr;
    AVFrame* m_frame = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_swsContext = nullptr;
    int m_videoStreamIndex = -1;
    int m_frameIntervalMs = 33;
};

} // namespace datatransfer
