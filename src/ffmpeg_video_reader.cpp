#include "ffmpeg_video_reader.h"

#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace datatransfer {

namespace {

void ThrowIfNegative(int value, const char* message) {
    if (value < 0) {
        throw std::runtime_error(message);
    }
}

} // namespace

FfmpegVideoReader::FfmpegVideoReader(const std::string& filePath) {
    ThrowIfNegative(avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr), "Failed to open video file");
    ThrowIfNegative(avformat_find_stream_info(m_formatContext, nullptr), "Failed to find stream info");

    m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIndex < 0) {
        throw std::runtime_error("Failed to find video stream");
    }

    AVStream* stream = m_formatContext->streams[m_videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        throw std::runtime_error("Failed to find decoder");
    }

    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        throw std::runtime_error("Failed to allocate codec context");
    }

    ThrowIfNegative(avcodec_parameters_to_context(m_codecContext, stream->codecpar), "Failed to copy codec parameters");
    ThrowIfNegative(avcodec_open2(m_codecContext, codec, nullptr), "Failed to open decoder");

    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_frame || !m_packet) {
        throw std::runtime_error("Failed to allocate ffmpeg frame or packet");
    }

    m_swsContext = sws_getContext(
        m_codecContext->width,
        m_codecContext->height,
        m_codecContext->pix_fmt,
        m_codecContext->width,
        m_codecContext->height,
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!m_swsContext) {
        throw std::runtime_error("Failed to create swscale context");
    }

    if (stream->avg_frame_rate.den != 0 && stream->avg_frame_rate.num != 0) {
        const double fps = av_q2d(stream->avg_frame_rate);
        if (fps > 0.0) {
            m_frameIntervalMs = static_cast<int>(1000.0 / fps);
            if (m_frameIntervalMs < 1) {
                m_frameIntervalMs = 1;
            }
        }
    }
}

FfmpegVideoReader::~FfmpegVideoReader() {
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
    }
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
    }
}

bool FfmpegVideoReader::ReadNextFrame(QImage& image) {
    if (DecodeNextFrame(image)) {
        return true;
    }

    if (!SeekToStart()) {
        return false;
    }

    return DecodeNextFrame(image);
}

int FfmpegVideoReader::FrameIntervalMs() const {
    return m_frameIntervalMs;
}

int FfmpegVideoReader::Width() const {
    return m_codecContext ? m_codecContext->width : 0;
}

int FfmpegVideoReader::Height() const {
    return m_codecContext ? m_codecContext->height : 0;
}

bool FfmpegVideoReader::DecodeNextFrame(QImage& image) {
    while (av_read_frame(m_formatContext, m_packet) >= 0) {
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet);
            continue;
        }

        const int sendResult = avcodec_send_packet(m_codecContext, m_packet);
        av_packet_unref(m_packet);
        if (sendResult < 0) {
            continue;
        }

        while (true) {
            const int receiveResult = avcodec_receive_frame(m_codecContext, m_frame);
            if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
                break;
            }
            if (receiveResult < 0) {
                return false;
            }

            QImage rgbImage(m_codecContext->width, m_codecContext->height, QImage::Format_RGB888);
            std::uint8_t* destination[4] = {rgbImage.bits(), nullptr, nullptr, nullptr};
            int destinationStride[4] = {rgbImage.bytesPerLine(), 0, 0, 0};

            sws_scale(
                m_swsContext,
                m_frame->data,
                m_frame->linesize,
                0,
                m_codecContext->height,
                destination,
                destinationStride);

            image = rgbImage.copy();
            av_frame_unref(m_frame);
            return true;
        }
    }

    return false;
}

bool FfmpegVideoReader::SeekToStart() {
    if (!m_formatContext || m_videoStreamIndex < 0) {
        return false;
    }

    avcodec_flush_buffers(m_codecContext);
    return av_seek_frame(m_formatContext, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD) >= 0;
}

} // namespace datatransfer
