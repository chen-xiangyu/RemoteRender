#include "ffmpeg_h264_decoder.h"

#include <cstring>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
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

FfmpegH264Decoder::FfmpegH264Decoder() {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        throw std::runtime_error("Failed to find H.264 decoder");
    }

    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        throw std::runtime_error("Failed to allocate H.264 codec context");
    }

    ThrowIfNegative(avcodec_open2(m_codecContext, codec, nullptr), "Failed to open H.264 decoder");

    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_frame || !m_packet) {
        throw std::runtime_error("Failed to allocate decoder frame or packet");
    }
}

FfmpegH264Decoder::~FfmpegH264Decoder() {
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
}

bool FfmpegH264Decoder::Decode(const std::vector<std::uint8_t>& annexBFrame, QImage& image) {
    av_packet_unref(m_packet);
    ThrowIfNegative(av_new_packet(m_packet, static_cast<int>(annexBFrame.size())), "Failed to allocate decode packet");
    std::memcpy(m_packet->data, annexBFrame.data(), annexBFrame.size());

    const int sendResult = avcodec_send_packet(m_codecContext, m_packet);
    av_packet_unref(m_packet);
    if (sendResult < 0) {
        return false;
    }

    while (true) {
        const int receiveResult = avcodec_receive_frame(m_codecContext, m_frame);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
            return false;
        }
        if (receiveResult < 0) {
            return false;
        }

        if (!m_swsContext ||
            m_codecContext->width != image.width() ||
            m_codecContext->height != image.height()) {
            if (m_swsContext) {
                sws_freeContext(m_swsContext);
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

} // namespace datatransfer
