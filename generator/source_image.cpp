#include "picture.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace avsync {
namespace {

struct DecodeSession {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    ~DecodeSession()
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&ctx);
        if (fmt != nullptr) {
            avformat_close_input(&fmt);
        }
    }
};

// Copies one decoded frame as RGBA, scaled down (never up) to fit kSourceStillMaxSide.
bool copy_frame(const AVFrame* frame, SourceStill& still, std::string& error, bool& over_limit)
{
    const int src_w = frame->width;
    const int src_h = frame->height;
    if (src_w <= 0 || src_h <= 0 || src_w > 4096 || src_h > 4096) {
        error = "source image is too large";
        over_limit = src_w > 4096 || src_h > 4096;
        return false;
    }
    const double scale = std::min(1.0, static_cast<double>(kSourceStillMaxSide) / std::max(src_w, src_h));
    still.width = std::max(1, static_cast<int>(std::lround(src_w * scale)));
    still.height = std::max(1, static_cast<int>(std::lround(src_h * scale)));
    still.rgba.assign(static_cast<size_t>(still.width * still.height * 4), 0);
    if (frame->format == AV_PIX_FMT_PAL8) {
        const uint8_t* palette = frame->data[1];
        if (palette == nullptr) {
            error = "source image palette is missing";
            return false;
        }
        for (int y = 0; y < still.height; ++y) {
            const int sy = std::min(src_h - 1, static_cast<int>(y * static_cast<double>(src_h) / still.height));
            const uint8_t* row = frame->data[0] + static_cast<size_t>(sy) * static_cast<size_t>(frame->linesize[0]);
            uint8_t* dst = still.rgba.data() + static_cast<size_t>(y * still.width * 4);
            for (int x = 0; x < still.width; ++x) {
                const int sx = std::min(src_w - 1, static_cast<int>(x * static_cast<double>(src_w) / still.width));
                const uint8_t* entry = palette + static_cast<size_t>(row[sx]) * 4;
                dst[x * 4 + 0] = entry[0];
                dst[x * 4 + 1] = entry[1];
                dst[x * 4 + 2] = entry[2];
                dst[x * 4 + 3] = entry[3];
            }
        }
        return true;
    }
    SwsContext* sws = sws_getContext(src_w, src_h, static_cast<AVPixelFormat>(frame->format), still.width, still.height,
        AV_PIX_FMT_RGBA, scale < 1.0 ? SWS_AREA : SWS_POINT, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        error = "cannot convert source image";
        return false;
    }
    uint8_t* dst_data[1] = {still.rgba.data()};
    int dst_stride[1] = {still.width * 4};
    sws_scale(sws, frame->data, frame->linesize, 0, src_h, dst_data, dst_stride);
    sws_freeContext(sws);
    return true;
}

} // namespace

bool SourceClip::load(const std::string& path, std::string& error)
{
    frames.clear();
    start_ms.clear();
    loop_ms = 0;
    over_limit = false;
    decoded_bytes = 0;
    DecodeSession session;
    if (avformat_open_input(&session.fmt, path.c_str(), nullptr, nullptr) < 0) {
        error = "cannot open source image";
        return false;
    }
    if (avformat_find_stream_info(session.fmt, nullptr) < 0) {
        error = "cannot read source image";
        return false;
    }
    const int stream_index = av_find_best_stream(session.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        error = "source image has no picture";
        return false;
    }
    AVStream* stream = session.fmt->streams[stream_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        error = "cannot decode source image";
        return false;
    }
    session.ctx = avcodec_alloc_context3(codec);
    if (session.ctx == nullptr || avcodec_parameters_to_context(session.ctx, stream->codecpar) < 0 || avcodec_open2(session.ctx, codec, nullptr) < 0) {
        error = "cannot decode source image";
        return false;
    }
    session.packet = av_packet_alloc();
    session.frame = av_frame_alloc();
    double cursor = 0;
    auto take = [&](const AVFrame* decoded, const AVPacket* packet) {
        if (frames.size() >= static_cast<size_t>(kSourceClipMaxFrames)) {
            error = "source image has too many frames (at most " + std::to_string(kSourceClipMaxFrames) + ")";
            over_limit = true;
            return false;
        }
        SourceStill still;
        if (!copy_frame(decoded, still, error, over_limit)) {
            return false;
        }
        decoded_bytes += still.rgba.size();
        if (decoded_bytes > kSourceClipMaxBytes) {
            error = "source image decodes to more than " + std::to_string(kSourceClipMaxBytes >> 20) + " MiB";
            over_limit = true;
            return false;
        }
        double duration_ms = 0;
        if (decoded->duration > 0) {
            duration_ms = decoded->duration * av_q2d(stream->time_base) * 1000.0;
        } else if (packet != nullptr && packet->duration > 0) {
            duration_ms = packet->duration * av_q2d(stream->time_base) * 1000.0;
        }
        if (duration_ms < 1.0) {
            if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
                duration_ms = 1000.0 * stream->avg_frame_rate.den / static_cast<double>(stream->avg_frame_rate.num);
            } else {
                duration_ms = 100;
            }
        }
        frames.push_back(std::move(still));
        start_ms.push_back(cursor);
        cursor += duration_ms;
        return true;
    };
    while (av_read_frame(session.fmt, session.packet) >= 0) {
        if (session.packet->stream_index != stream_index) {
            av_packet_unref(session.packet);
            continue;
        }
        if (avcodec_send_packet(session.ctx, session.packet) < 0) {
            av_packet_unref(session.packet);
            error = "cannot decode source image";
            return false;
        }
        while (avcodec_receive_frame(session.ctx, session.frame) == 0) {
            if (!take(session.frame, session.packet)) {
                av_packet_unref(session.packet);
                return false;
            }
        }
        av_packet_unref(session.packet);
    }
    avcodec_send_packet(session.ctx, nullptr);
    while (avcodec_receive_frame(session.ctx, session.frame) == 0) {
        if (!take(session.frame, nullptr)) {
            return false;
        }
    }
    if (frames.empty()) {
        error = "source image has no frames";
        return false;
    }
    loop_ms = std::max(1.0, cursor);
    return true;
}

const SourceStill* SourceClip::at(double media_ms) const
{
    if (frames.empty()) {
        return nullptr;
    }
    if (frames.size() == 1 || loop_ms <= 0 || start_ms.size() != frames.size()) {
        return &frames.front();
    }
    double t = std::fmod(media_ms, loop_ms);
    if (t < 0) {
        t += loop_ms;
    }
    size_t index = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (start_ms[i] <= t) {
            index = i;
        } else {
            break;
        }
    }
    return &frames[index];
}

} // namespace avsync
