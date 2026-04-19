#include "ffmpeg_h264_file_source.h"

#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

namespace datatransfer {

namespace {

void ThrowIfNegative(int value, const char* message) {
    if (value < 0) {
        throw std::runtime_error(message);
    }
}

} // namespace

FfmpegH264FileSource::FfmpegH264FileSource(const std::string& filePath) {
    ThrowIfNegative(avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr), "Failed to open video input");
    ThrowIfNegative(avformat_find_stream_info(m_formatContext, nullptr), "Failed to read stream info");

    m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIndex < 0) {
        throw std::runtime_error("No video stream found");
    }

    AVStream* stream = m_formatContext->streams[m_videoStreamIndex];
    if (stream->codecpar->codec_id != AV_CODEC_ID_H264) {
        throw std::runtime_error("This prototype currently expects an H.264 input file");
    }

    const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
    if (!bsf) {
        throw std::runtime_error("Failed to find h264_mp4toannexb bitstream filter");
    }

    ThrowIfNegative(av_bsf_alloc(bsf, &m_bsfContext), "Failed to allocate H264 bitstream filter");
    ThrowIfNegative(
        avcodec_parameters_copy(m_bsfContext->par_in, stream->codecpar),
        "Failed to copy codec parameters into bitstream filter");
    m_bsfContext->time_base_in = stream->time_base;
    ThrowIfNegative(av_bsf_init(m_bsfContext), "Failed to initialize H264 bitstream filter");

    m_packet = av_packet_alloc();
    if (!m_packet) {
        throw std::runtime_error("Failed to allocate AVPacket");
    }

    if (stream->avg_frame_rate.den != 0 && stream->avg_frame_rate.num != 0) {
        const double fps = av_q2d(stream->avg_frame_rate);
        if (fps > 0.0) {
            m_frameIntervalUs = static_cast<std::uint64_t>(1000000.0 / fps);
        }
    }
}

FfmpegH264FileSource::~FfmpegH264FileSource() {
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_bsfContext) {
        av_bsf_free(&m_bsfContext);
    }
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
    }
}

bool FfmpegH264FileSource::ReadNextSample(
    std::vector<std::uint8_t>& annexBFrame,
    std::uint64_t& timestampUs) {
    if (ReadPacketFromSource(annexBFrame, timestampUs)) {
        return true;
    }

    if (!SeekToStart()) {
        return false;
    }

    return ReadPacketFromSource(annexBFrame, timestampUs);
}

bool FfmpegH264FileSource::ReadPacketFromSource(
    std::vector<std::uint8_t>& annexBFrame,
    std::uint64_t& timestampUs) {
    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet);
            continue;
        }

        ThrowIfNegative(av_bsf_send_packet(m_bsfContext, m_packet), "Failed to send packet to bitstream filter");
        av_packet_unref(m_packet);

        while (true) {
            const int receiveResult = av_bsf_receive_packet(m_bsfContext, m_packet);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            ThrowIfNegative(receiveResult, "Failed to receive H264 packet from bitstream filter");

            annexBFrame.assign(m_packet->data, m_packet->data + m_packet->size);
            if (m_packet->pts != AV_NOPTS_VALUE) {
                const AVRational targetBase{1, 1000000};
                timestampUs = static_cast<std::uint64_t>(
                    av_rescale_q(m_packet->pts, m_bsfContext->time_base_out, targetBase));
                m_fallbackTimestampUs = timestampUs + m_frameIntervalUs;
            } else {
                timestampUs = m_fallbackTimestampUs;
                m_fallbackTimestampUs += m_frameIntervalUs;
            }

            av_packet_unref(m_packet);
            return true;
        }
    }

    return false;
}

bool FfmpegH264FileSource::SeekToStart() {
    if (!m_formatContext) {
        return false;
    }

    av_bsf_flush(m_bsfContext);
    if (av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }

    m_fallbackTimestampUs = 0;
    return true;
}

} // namespace datatransfer
