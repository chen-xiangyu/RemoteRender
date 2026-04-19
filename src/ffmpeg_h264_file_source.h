#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AVBSFContext;
struct AVFormatContext;
struct AVPacket;

namespace datatransfer {

class FfmpegH264FileSource {
public:
    explicit FfmpegH264FileSource(const std::string& filePath);
    ~FfmpegH264FileSource();

    FfmpegH264FileSource(const FfmpegH264FileSource&) = delete;
    FfmpegH264FileSource& operator=(const FfmpegH264FileSource&) = delete;

    bool ReadNextSample(std::vector<std::uint8_t>& annexBFrame, std::uint64_t& timestampUs);

private:
    bool ReadPacketFromSource(std::vector<std::uint8_t>& annexBFrame, std::uint64_t& timestampUs);
    bool SeekToStart();

    AVFormatContext* m_formatContext = nullptr;
    AVBSFContext* m_bsfContext = nullptr;
    AVPacket* m_packet = nullptr;
    int m_videoStreamIndex = -1;
    std::uint64_t m_fallbackTimestampUs = 0;
    std::uint64_t m_frameIntervalUs = 33333;
};

} // namespace datatransfer
