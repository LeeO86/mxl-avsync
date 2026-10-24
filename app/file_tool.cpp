#include "file_tool.hpp"

#include "picture.hpp"
#include "report.hpp"

#include <nlohmann/json.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace avsync {
namespace {

std::string av_error(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

void check(int err, const char* what)
{
    if (err < 0) {
        throw std::runtime_error(std::string(what) + ": " + av_error(err));
    }
}

const char* video_encoder_name(const std::string& codec)
{
    if (codec == "prores") {
        return "prores_ks";
    }
    if (codec == "v210") {
        return "v210";
    }
    if (codec == "h264") {
        return "libx264";
    }
    throw std::runtime_error("unknown video codec " + codec);
}

const char* audio_encoder_name(const std::string& codec)
{
    if (codec == "pcm24") {
        return "pcm_s24le";
    }
    if (codec == "pcm16") {
        return "pcm_s16le";
    }
    if (codec == "aac") {
        return "aac";
    }
    throw std::runtime_error("unknown audio codec " + codec);
}

std::string fourcc_name(const std::string& codec)
{
    if (codec == "prores") {
        return "prores_hq";
    }
    if (codec == "pcm24") {
        return "pcm_s24le";
    }
    if (codec == "pcm16") {
        return "pcm_s16le";
    }
    return codec;
}

void append_channel(std::vector<float>& dst, const AVFrame* frame, int channel)
{
    const int channels = frame->ch_layout.nb_channels > 0 ? frame->ch_layout.nb_channels : 1;
    const int ch = std::clamp(channel, 0, channels - 1);
    const auto fmt = static_cast<AVSampleFormat>(frame->format);
    const bool planar = av_sample_fmt_is_planar(fmt) != 0;
    for (int i = 0; i < frame->nb_samples; ++i) {
        float sample = 0;
        if (fmt == AV_SAMPLE_FMT_FLTP || fmt == AV_SAMPLE_FMT_FLT) {
            const float* src = planar ? reinterpret_cast<const float*>(frame->extended_data[ch])
                                      : reinterpret_cast<const float*>(frame->extended_data[0]);
            sample = planar ? src[i] : src[i * channels + ch];
        } else if (fmt == AV_SAMPLE_FMT_S32P || fmt == AV_SAMPLE_FMT_S32) {
            const int32_t* src = planar ? reinterpret_cast<const int32_t*>(frame->extended_data[ch])
                                        : reinterpret_cast<const int32_t*>(frame->extended_data[0]);
            const int32_t v = planar ? src[i] : src[i * channels + ch];
            sample = static_cast<float>(v / 2147483648.0);
        } else if (fmt == AV_SAMPLE_FMT_S16P || fmt == AV_SAMPLE_FMT_S16) {
            const int16_t* src = planar ? reinterpret_cast<const int16_t*>(frame->extended_data[ch])
                                        : reinterpret_cast<const int16_t*>(frame->extended_data[0]);
            const int16_t v = planar ? src[i] : src[i * channels + ch];
            sample = static_cast<float>(v / 32768.0);
        } else {
            throw std::runtime_error("unsupported decoded audio format");
        }
        dst.push_back(sample);
    }
}

nlohmann::json check_container(const std::string& path, const EventPlan& plan, int audio_rate)
{
    AVFormatContext* fmt = nullptr;
    check(avformat_open_input(&fmt, path.c_str(), nullptr, nullptr), "reopen");
    check(avformat_find_stream_info(fmt, nullptr), "stream info");
    const int video = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audio = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    nlohmann::json checks = {{"ok", false}, {"notes", nlohmann::json::array()}};
    if (video < 0 || audio < 0) {
        checks["notes"].push_back("missing stream");
        avformat_close_input(&fmt);
        return checks;
    }
    auto* vs = fmt->streams[video];
    auto* as = fmt->streams[audio];
    auto tb = [](AVRational r) { return std::to_string(r.num) + "/" + std::to_string(r.den); };
    checks["video_time_base"] = tb(vs->time_base);
    checks["audio_time_base"] = tb(as->time_base);
    checks["video_start_time"] = vs->start_time == AV_NOPTS_VALUE ? 0 : vs->start_time;
    checks["audio_start_time"] = as->start_time == AV_NOPTS_VALUE ? 0 : as->start_time;
    const Rate grain = plan.config.video.grain_rate();
    const AVRational want_v{static_cast<int>(grain.den), static_cast<int>(grain.num)};
    const AVRational want_a{1, audio_rate};
    const bool video_tb_ok = vs->time_base.num == want_v.num && vs->time_base.den == want_v.den;
    const bool audio_tb_ok = as->time_base.num == want_a.num && as->time_base.den == want_a.den;
    if (!video_tb_ok) {
        checks["notes"].push_back("video time base differs from the plan");
    }
    if (!audio_tb_ok) {
        checks["notes"].push_back("audio time base differs from the plan");
    }
    std::vector<int64_t> video_pts;
    int64_t first_audio = AV_NOPTS_VALUE;
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == video && packet->pts != AV_NOPTS_VALUE) {
            video_pts.push_back(packet->pts);
        }
        if (packet->stream_index == audio && first_audio == AV_NOPTS_VALUE) {
            first_audio = packet->pts;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    double max_video = 0;
    for (const auto& event : plan.events) {
        const double want_ms = event.video_timeline_ms;
        if (video_pts.empty()) {
            max_video = 1e9;
            break;
        }
        auto it = std::min_element(video_pts.begin(), video_pts.end(), [&](int64_t a, int64_t b) {
            const double da = std::abs(a * av_q2d(vs->time_base) * 1000.0 - want_ms);
            const double db = std::abs(b * av_q2d(vs->time_base) * 1000.0 - want_ms);
            return da < db;
        });
        const double err = std::abs(*it * av_q2d(vs->time_base) * 1000.0 - want_ms);
        max_video = std::max(max_video, err);
    }
    checks["max_video_pts_error_ms"] = max_video;
    checks["audio_first_pts"] = first_audio == AV_NOPTS_VALUE ? 0 : first_audio;
    const double audio_start_ms = (first_audio == AV_NOPTS_VALUE ? 0 : first_audio) * av_q2d(as->time_base) * 1000.0;
    checks["audio_start_ms"] = audio_start_ms;
    int priming = 0;
    if (as->codecpar && as->codecpar->initial_padding > 0) {
        priming = as->codecpar->initial_padding;
    }
    checks["audio_priming_samples"] = priming;
    if (priming > 0) {
        checks["notes"].push_back("audio encoder priming is reported, not hidden");
    }
    if (std::abs(audio_start_ms) > 0.05) {
        checks["notes"].push_back("audio start is not 0");
    }
    if (max_video > 0.05) {
        checks["notes"].push_back("video PTS does not match the event grid");
    }
    checks["ok"] = checks["notes"].empty();
    avformat_close_input(&fmt);
    return checks;
}

} // namespace

int run_generate(const GenerateRequest& request, std::string& error)
{
    try {
        const auto format = parse_video_format(request.format);
        if (!format) {
            error = "unknown format " + request.format;
            return 78;
        }
        PlanConfig plan_config;
        plan_config.video = *format;
        plan_config.event_rate = request.event_rate;
        plan_config.sample_rate = 48000;
        plan_config.offset_ms = request.offset_ms;
        plan_config.sweep = request.sweep;
        plan_config.duration_ms = request.duration_s * 1000.0;
        plan_config.first_event_ms = 1000;
        plan_config.chirp_ms = request.chirp_ms;
        plan_config.chirp_low_hz = request.chirp_low_hz;
        plan_config.chirp_high_hz = request.chirp_high_hz;
        plan_config.chirp_level_dbfs = request.chirp_level_dbfs;
        const EventPlan plan = make_file_plan(plan_config);
        SourceClip source_clip;
        if (!request.source_image.empty() && !source_clip.load(request.source_image, error)) {
            return 78;
        }
        const Rate grain = format->grain_rate();
        const int width = format->width;
        const int height = format->grain_height();
        const int64_t frames = std::max<int64_t>(1, static_cast<int64_t>(std::llround(request.duration_s * grain.num / static_cast<double>(grain.den))));
        const int out_rate = request.audio_rate;
        const int64_t out_samples = std::max<int64_t>(1, static_cast<int64_t>(std::llround(request.duration_s * out_rate)));

        const AVCodec* vcodec = avcodec_find_encoder_by_name(video_encoder_name(request.video_codec));
        const AVCodec* acodec = avcodec_find_encoder_by_name(audio_encoder_name(request.audio_codec));
        if (vcodec == nullptr || acodec == nullptr) {
            error = "encoder not found";
            return 1;
        }
        AVFormatContext* fmt = nullptr;
        check(avformat_alloc_output_context2(&fmt, nullptr, request.container.c_str(), request.out_path.c_str()), "alloc output");
        AVStream* vstream = avformat_new_stream(fmt, nullptr);
        AVStream* astream = avformat_new_stream(fmt, nullptr);
        AVCodecContext* venc = avcodec_alloc_context3(vcodec);
        AVCodecContext* aenc = avcodec_alloc_context3(acodec);
        venc->width = width;
        venc->height = height;
        venc->time_base = AVRational{static_cast<int>(grain.den), static_cast<int>(grain.num)};
        venc->framerate = AVRational{static_cast<int>(grain.num), static_cast<int>(grain.den)};
        venc->pix_fmt = AV_PIX_FMT_YUV422P10LE;
        if (vcodec->pix_fmts) {
            bool ok = false;
            for (const AVPixelFormat* p = vcodec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
                if (*p == AV_PIX_FMT_YUV422P10LE) {
                    ok = true;
                }
            }
            if (!ok) {
                venc->pix_fmt = vcodec->pix_fmts[0];
            }
        }
        venc->color_range = AVCOL_RANGE_MPEG;
        venc->colorspace = AVCOL_SPC_BT709;
        venc->color_primaries = AVCOL_PRI_BT709;
        venc->color_trc = AVCOL_TRC_BT709;
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER) {
            venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        if (request.video_codec == "prores") {
            av_opt_set_int(venc->priv_data, "profile", 3, 0);
        }
        if (request.video_codec == "h264") {
            av_opt_set(venc->priv_data, "preset", "veryfast", 0);
        }
        check(avcodec_open2(venc, vcodec, nullptr), "open video");
        aenc->sample_rate = out_rate;
        av_channel_layout_default(&aenc->ch_layout, request.channels);
        aenc->time_base = AVRational{1, out_rate};
        aenc->sample_fmt = acodec->sample_fmts ? acodec->sample_fmts[0] : AV_SAMPLE_FMT_S32;
        if (request.audio_codec == "aac") {
            aenc->bit_rate = 128000;
        }
        check(avcodec_open2(aenc, acodec, nullptr), "open audio");
        check(avcodec_parameters_from_context(vstream->codecpar, venc), "vpar");
        check(avcodec_parameters_from_context(astream->codecpar, aenc), "apar");
        vstream->time_base = venc->time_base;
        astream->time_base = aenc->time_base;
        vstream->avg_frame_rate = venc->framerate;
        const std::string comment = "TEST SIGNAL — mxl-avsync";
        av_dict_set(&fmt->metadata, "title", request.title.empty() ? "TEST SIGNAL — mxl-avsync" : request.title.c_str(), 0);
        av_dict_set(&fmt->metadata, "comment", comment.c_str(), 0);
        if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
            check(avio_open(&fmt->pb, request.out_path.c_str(), AVIO_FLAG_WRITE), "open out");
        }
        AVDictionary* muxopt = nullptr;
        av_dict_set(&muxopt, "use_editlist", "0", 0);
        av_dict_set(&muxopt, "video_track_timescale", std::to_string(grain.num).c_str(), 0);
        check(avformat_write_header(fmt, &muxopt), "header");
        av_dict_free(&muxopt);

        SwsContext* sws = nullptr;
        AVFrame* vframe = av_frame_alloc();
        vframe->format = venc->pix_fmt;
        vframe->width = width;
        vframe->height = height;
        check(av_frame_get_buffer(vframe, 0), "vbuf");
        Yuv422p10 picture;
        picture.resize(width, height);
        const Period period = plan.period;
        std::string qr_cached;
        int qr_second = -1;
        for (int64_t index = 0; index < frames; ++index) {
            const double ms = index_to_ns(grain, static_cast<uint64_t>(index)) / 1e6;
            const int second = static_cast<int>(ms / 1000.0);
            bool marker_white = false;
            uint64_t shown_seq = 0;
            int frame_in_period = static_cast<int>(static_cast<uint64_t>(index) % static_cast<uint64_t>(std::max(1, period.grains)));
            double offset = request.offset_ms;
            uint64_t qr_seq = plan.events.empty() ? 1 : plan.events.front().seq;
            int64_t tk = static_cast<int64_t>(std::llround(ms));
            for (const auto& event : plan.events) {
                if (static_cast<uint64_t>(index) >= event.video_grain) {
                    marker_white = event.marker_phase != 0;
                    shown_seq = event.seq;
                    frame_in_period = static_cast<int>(static_cast<uint64_t>(index) - event.video_grain);
                    offset = event.expected_av_offset_ms;
                } else {
                    qr_seq = event.seq;
                    offset = event.expected_av_offset_ms;
                    tk = static_cast<int64_t>(std::llround(event.video_timeline_ms));
                    break;
                }
            }
            PictureInfo info;
            info.title = request.title;
            info.source = request.source;
            info.subtitle = request.subtitle;
            info.notes = request.notes;
            info.format_line = request.format;
            info.av_offset_ms = offset;
            info.media_ms = ms;
            info.period_ms = period.period_ms;
            info.source_image = source_clip.at(ms);
            info.offset_line = "offset " + std::to_string(static_cast<int>(std::lround(offset))) + " ms";
            char counter[64];
            std::snprintf(counter, sizeof(counter), "frame as written by the generator %07lld", static_cast<long long>(index));
            info.counter = counter;
            char timetext[64];
            std::snprintf(timetext, sizeof(timetext), "t %8.1f ms", ms);
            info.time_line = timetext;
            if (second != qr_second) {
                QrPayload payload;
                payload.test_id = request.test_id;
                payload.mode = "FILE";
                payload.k = qr_seq;
                payload.tk = tk;
                payload.sweep = !request.sweep.empty();
                payload.offset_ms = offset;
                payload.rate = request.event_rate;
                payload.format = request.format;
                qr_cached = qr_payload_json(payload);
                qr_second = second;
            }
            info.qr_json = qr_cached;
            render_picture(picture, info, shown_seq, frame_in_period, period.grains, marker_white);
            check(av_frame_make_writable(vframe), "writable");
            if (venc->pix_fmt == AV_PIX_FMT_YUV422P10LE) {
                for (int y = 0; y < height; ++y) {
                    std::memcpy(vframe->data[0] + static_cast<size_t>(y) * vframe->linesize[0],
                        picture.y.data() + static_cast<size_t>(y) * width, static_cast<size_t>(width) * sizeof(uint16_t));
                    std::memcpy(vframe->data[1] + static_cast<size_t>(y) * vframe->linesize[1],
                        picture.cb.data() + static_cast<size_t>(y) * (width / 2), static_cast<size_t>(width / 2) * sizeof(uint16_t));
                    std::memcpy(vframe->data[2] + static_cast<size_t>(y) * vframe->linesize[2],
                        picture.cr.data() + static_cast<size_t>(y) * (width / 2), static_cast<size_t>(width / 2) * sizeof(uint16_t));
                }
            } else {
                if (sws == nullptr) {
                    sws = sws_getContext(width, height, AV_PIX_FMT_YUV422P10LE, width, height, venc->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
                }
                AVFrame* src = av_frame_alloc();
                src->format = AV_PIX_FMT_YUV422P10LE;
                src->width = width;
                src->height = height;
                check(av_frame_get_buffer(src, 0), "srcbuf");
                for (int y = 0; y < height; ++y) {
                    std::memcpy(src->data[0] + y * src->linesize[0], picture.y.data() + static_cast<size_t>(y) * width, width * sizeof(uint16_t));
                    std::memcpy(src->data[1] + y * src->linesize[1], picture.cb.data() + static_cast<size_t>(y) * (width / 2), (width / 2) * sizeof(uint16_t));
                    std::memcpy(src->data[2] + y * src->linesize[2], picture.cr.data() + static_cast<size_t>(y) * (width / 2), (width / 2) * sizeof(uint16_t));
                }
                sws_scale(sws, src->data, src->linesize, 0, height, vframe->data, vframe->linesize);
                av_frame_free(&src);
            }
            vframe->pts = index;
            check(avcodec_send_frame(venc, vframe), "send video");
            AVPacket* packet = av_packet_alloc();
            while (true) {
                const int rec = avcodec_receive_packet(venc, packet);
                if (rec == AVERROR(EAGAIN) || rec == AVERROR_EOF) {
                    break;
                }
                check(rec, "video packet");
                av_packet_rescale_ts(packet, venc->time_base, vstream->time_base);
                packet->stream_index = vstream->index;
                check(av_interleaved_write_frame(fmt, packet), "write video");
                av_packet_unref(packet);
            }
            av_packet_free(&packet);
        }
        check(avcodec_send_frame(venc, nullptr), "flush video");
        {
            AVPacket* packet = av_packet_alloc();
            while (true) {
                const int rec = avcodec_receive_packet(venc, packet);
                if (rec == AVERROR(EAGAIN) || rec == AVERROR_EOF) {
                    break;
                }
                check(rec, "flush video packet");
                av_packet_rescale_ts(packet, venc->time_base, vstream->time_base);
                packet->stream_index = vstream->index;
                check(av_interleaved_write_frame(fmt, packet), "write flush video");
                av_packet_unref(packet);
            }
            av_packet_free(&packet);
        }

        ChirpSpec spec;
        spec.level_dbfs = request.chirp_level_dbfs;
        spec.duration_ms = request.chirp_ms;
        spec.low_hz = request.chirp_low_hz;
        spec.high_hz = request.chirp_high_hz;
        std::vector<float> mono(static_cast<size_t>(frames * 48000 * grain.den / grain.num + 8));
        if (mono.size() < static_cast<size_t>(request.duration_s * 48000)) {
            mono.resize(static_cast<size_t>(request.duration_s * 48000) + 8);
        }
        synthesise_block(spec, plan.events, 0, mono.data(), mono.size());
        std::vector<float> played = mono;
        if (out_rate != 48000) {
            played = resample_linear(mono.data(), static_cast<size_t>(request.duration_s * 48000), 48000, out_rate);
        }
        if (played.size() < static_cast<size_t>(out_samples)) {
            played.resize(static_cast<size_t>(out_samples));
        }
        AVFrame* aframe = av_frame_alloc();
        int64_t cursor = 0;
        const int block = aenc->frame_size > 0 ? aenc->frame_size : 1024;
        while (cursor < out_samples) {
            const int count = static_cast<int>(std::min<int64_t>(block, out_samples - cursor));
            aframe->format = aenc->sample_fmt;
            aframe->sample_rate = out_rate;
            aframe->nb_samples = count;
            check(av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout), "layout");
            check(av_frame_get_buffer(aframe, 0), "abuf");
            const bool planar = av_sample_fmt_is_planar(aenc->sample_fmt) != 0;
            for (int i = 0; i < count; ++i) {
                const float sample = played[static_cast<size_t>(cursor + i)];
                for (int ch = 0; ch < request.channels; ++ch) {
                    if (aenc->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                        reinterpret_cast<float*>(aframe->extended_data[ch])[i] = sample;
                    } else if (aenc->sample_fmt == AV_SAMPLE_FMT_FLT) {
                        reinterpret_cast<float*>(aframe->extended_data[0])[i * request.channels + ch] = sample;
                    } else if (aenc->sample_fmt == AV_SAMPLE_FMT_S32 || aenc->sample_fmt == AV_SAMPLE_FMT_S32P) {
                        const int32_t v = static_cast<int32_t>(std::lrint(std::clamp(static_cast<double>(sample), -1.0, 1.0) * 8388607.0)) << 8;
                        if (planar) {
                            reinterpret_cast<int32_t*>(aframe->extended_data[ch])[i] = v;
                        } else {
                            reinterpret_cast<int32_t*>(aframe->extended_data[0])[i * request.channels + ch] = v;
                        }
                    } else if (aenc->sample_fmt == AV_SAMPLE_FMT_S16 || aenc->sample_fmt == AV_SAMPLE_FMT_S16P) {
                        const int16_t v = static_cast<int16_t>(std::lrint(std::clamp(static_cast<double>(sample), -1.0, 1.0) * 32767.0));
                        if (planar) {
                            reinterpret_cast<int16_t*>(aframe->extended_data[ch])[i] = v;
                        } else {
                            reinterpret_cast<int16_t*>(aframe->extended_data[0])[i * request.channels + ch] = v;
                        }
                    }
                }
            }
            aframe->pts = cursor;
            check(avcodec_send_frame(aenc, aframe), "send audio");
            av_frame_unref(aframe);
            AVPacket* packet = av_packet_alloc();
            while (true) {
                const int rec = avcodec_receive_packet(aenc, packet);
                if (rec == AVERROR(EAGAIN) || rec == AVERROR_EOF) {
                    break;
                }
                check(rec, "audio packet");
                av_packet_rescale_ts(packet, aenc->time_base, astream->time_base);
                packet->stream_index = astream->index;
                check(av_interleaved_write_frame(fmt, packet), "write audio");
                av_packet_unref(packet);
            }
            av_packet_free(&packet);
            cursor += count;
        }
        check(avcodec_send_frame(aenc, nullptr), "flush audio");
        {
            AVPacket* packet = av_packet_alloc();
            while (true) {
                const int rec = avcodec_receive_packet(aenc, packet);
                if (rec == AVERROR_EOF || rec == AVERROR(EAGAIN)) {
                    break;
                }
                check(rec, "flush audio packet");
                av_packet_rescale_ts(packet, aenc->time_base, astream->time_base);
                packet->stream_index = astream->index;
                check(av_interleaved_write_frame(fmt, packet), "write flush audio");
                av_packet_unref(packet);
            }
            av_packet_free(&packet);
        }
        check(av_write_trailer(fmt), "trailer");
        if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmt->pb);
        }
        av_frame_free(&vframe);
        av_frame_free(&aframe);
        avcodec_free_context(&venc);
        avcodec_free_context(&aenc);
        avformat_free_context(fmt);
        if (sws) {
            sws_freeContext(sws);
        }

        const auto checks = check_container(request.out_path, plan, out_rate);
        nlohmann::json events = nlohmann::json::array();
        for (const auto& event : plan.events) {
            events.push_back({{"event_id", event.event_id}, {"seq", event.seq}, {"chirp_id", event.chirp_id},
                {"marker_phase", event.marker_phase}, {"video_frame", event.video_grain}, {"video_timeline_ms", event.video_timeline_ms},
                {"audio_sample", event.audio_sample}, {"audio_timeline_ms", event.audio_timeline_ms},
                {"expected_av_offset_ms", event.expected_av_offset_ms}, {"rounding_residual_us", event.rounding_residual_us}});
        }
        nlohmann::json manifest = {
            {"schema", kManifestSchema},
            {"generator", {{"version", kAppVersion}, {"core", kCoreVersion}}},
            {"test_id", request.test_id},
            {"mode", "FILE"},
            {"timeline", "file"},
            {"duration_ms", request.duration_s * 1000.0},
            {"video", {{"width", format->width}, {"height", format->height}, {"fps_num", format->frame.num}, {"fps_den", format->frame.den},
                          {"scan", format->interlaced ? "interlaced_tff" : "progressive"}, {"codec", fourcc_name(request.video_codec)}}},
            {"audio", {{"codec", fourcc_name(request.audio_codec)}, {"sample_rate_hz", out_rate}, {"channels", request.channels},
                          {"chirp", {{"ms", request.chirp_ms}, {"low_hz", request.chirp_low_hz}, {"high_hz", request.chirp_high_hz},
                                        {"level_dbfs", request.chirp_level_dbfs}}}}},
            {"container", {{"format", request.container}, {"video_time_base", checks.value("video_time_base", "")},
                              {"audio_time_base", checks.value("audio_time_base", "")},
                              {"video_start_pts", checks.value("video_start_time", 0)}, {"audio_start_pts", checks.value("audio_first_pts", 0)},
                              {"checks", checks}}},
            {"events", events},
        };
        const std::string manifest_path = request.out_path + ".manifest.json";
        std::ofstream out(manifest_path);
        out << manifest.dump(2) << "\n";
        if (!checks.value("ok", false) && !request.allow_container_shift) {
            error = "container timestamps differ from the plan";
            return 1;
        }
        return 0;
    } catch (const std::exception& ex) {
        error = ex.what();
        return 1;
    }
}

int run_analyze(const AnalyzeRequest& request, std::string& error)
{
    try {
        nlohmann::json manifest;
        bool have_manifest = false;
        if (!request.manifest.empty()) {
            std::ifstream in(request.manifest);
            if (!in) {
                error = "cannot read manifest";
                return 78;
            }
            in >> manifest;
            have_manifest = true;
        }
        AVFormatContext* fmt = nullptr;
        check(avformat_open_input(&fmt, request.path.c_str(), nullptr, nullptr), "open");
        check(avformat_find_stream_info(fmt, nullptr), "info");
        const int video_index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const int audio_index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (video_index < 0 || audio_index < 0) {
            error = "file has no video and audio";
            return 78;
        }
        auto open_dec = [&](int index) {
            const AVCodec* codec = avcodec_find_decoder(fmt->streams[index]->codecpar->codec_id);
            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            check(avcodec_parameters_to_context(ctx, fmt->streams[index]->codecpar), "par");
            check(avcodec_open2(ctx, codec, nullptr), "open dec");
            return ctx;
        };
        AVCodecContext* vdec = open_dec(video_index);
        AVCodecContext* adec = open_dec(audio_index);
        const int width = vdec->width;
        const int height = vdec->height;
        SwsContext* sws = sws_getContext(width, height, vdec->pix_fmt, width, height, AV_PIX_FMT_YUV422P10LE, SWS_BILINEAR, nullptr, nullptr, nullptr);
        std::vector<uint16_t> luma(static_cast<size_t>(width * height));
        AVFrame* yuv = av_frame_alloc();
        yuv->format = AV_PIX_FMT_YUV422P10LE;
        yuv->width = width;
        yuv->height = height;
        check(av_frame_get_buffer(yuv, 0), "yuv");
        VideoFormat synthetic;
        synthetic.token = std::to_string(width) + "x" + std::to_string(height);
        synthetic.width = width;
        synthetic.height = height;
        synthetic.frame = Rate{fmt->streams[video_index]->avg_frame_rate.num > 0 ? fmt->streams[video_index]->avg_frame_rate.num : 50,
            fmt->streams[video_index]->avg_frame_rate.den > 0 ? fmt->streams[video_index]->avg_frame_rate.den : 1};
        synthetic.interlaced = false;
        int event_rate = 2;
        if (have_manifest && manifest.contains("events") && manifest["events"].size() >= 2) {
            const double dt = manifest["events"][1]["video_timeline_ms"].get<double>() - manifest["events"][0]["video_timeline_ms"].get<double>();
            if (dt > 0) {
                const double hz = 1000.0 / dt;
                event_rate = std::abs(hz - 1) < std::abs(hz - 4) && std::abs(hz - 1) < std::abs(hz - 2) ? 1 : (std::abs(hz - 4) < std::abs(hz - 2) ? 4 : 2);
            }
        }
        MarkerTracker tracker(synthetic, event_rate);
        std::optional<MarkerTracker> locate_tracker;
        std::vector<float> audio;
        double audio_origin_ms = 0;
        bool audio_origin_set = false;
        int native_rate = adec->sample_rate > 0 ? adec->sample_rate : 48000;
        std::optional<QrPayload> qr;
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        std::vector<VideoObservation> videos;
        while (av_read_frame(fmt, packet) >= 0) {
            AVCodecContext* dec = packet->stream_index == video_index ? vdec : packet->stream_index == audio_index ? adec : nullptr;
            if (dec == nullptr) {
                av_packet_unref(packet);
                continue;
            }
            check(avcodec_send_packet(dec, packet), "send");
            av_packet_unref(packet);
            while (true) {
                const int rec = avcodec_receive_frame(dec, frame);
                if (rec == AVERROR(EAGAIN) || rec == AVERROR_EOF) {
                    break;
                }
                check(rec, "frame");
                const AVRational tb = fmt->streams[dec == vdec ? video_index : audio_index]->time_base;
                const double ms = (frame->pts == AV_NOPTS_VALUE ? 0 : frame->pts) * av_q2d(tb) * 1000.0;
                if (request.from_s >= 0 && ms < request.from_s * 1000.0) {
                    av_frame_unref(frame);
                    continue;
                }
                if (request.to_s >= 0 && ms > request.to_s * 1000.0) {
                    av_frame_unref(frame);
                    continue;
                }
                if (dec == vdec) {
                    sws_scale(sws, frame->data, frame->linesize, 0, height, yuv->data, yuv->linesize);
                    for (int y = 0; y < height; ++y) {
                        std::memcpy(luma.data() + static_cast<size_t>(y) * width, yuv->data[0] + static_cast<size_t>(y) * yuv->linesize[0],
                            static_cast<size_t>(width) * sizeof(uint16_t));
                    }
                    if (!qr) {
                        if (auto text = decode_qr_text(luma.data(), width, height, width)) {
                            qr = parse_qr_payload(*text);
                        }
                    }
                    const int64_t grain = frame->pts == AV_NOPTS_VALUE ? 0 : frame->pts;
                    VideoObservation obs;
                    if (request.locate) {
                        const LocatedMarker located = read_located_marker(luma.data(), width, height, width);
                        if (located.located) {
                            if (!locate_tracker) {
                                VideoFormat located_format = synthetic;
                                located_format.width = located.width;
                                located_format.height = located.height;
                                locate_tracker = MarkerTracker(located_format, event_rate);
                            }
                            obs = locate_tracker->push(located.canonical.data(), located.width, grain, ms);
                            if (obs.event) {
                                obs.flags.push_back("fiducial_lock");
                            }
                        }
                    } else {
                        obs = tracker.push(luma.data(), width, grain, ms);
                    }
                    if (obs.event) {
                        videos.push_back(obs);
                    }
                } else if (!audio_origin_set) {
                    audio_origin_ms = ms;
                    audio_origin_set = true;
                    append_channel(audio, frame, request.channel);
                } else {
                    append_channel(audio, frame, request.channel);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&yuv);
        sws_freeContext(sws);
        avcodec_free_context(&vdec);
        avcodec_free_context(&adec);
        avformat_close_input(&fmt);

        if (native_rate != 48000 && !audio.empty()) {
            audio = resample_linear(audio.data(), audio.size(), native_rate, 48000);
        }
        AudioDetectConfig detect;
        detect.period_ms = 1000.0 / std::max(1, event_rate);
        detect.timeline_origin_ms = audio_origin_ms;
        if (have_manifest && manifest.contains("audio") && manifest["audio"].contains("chirp")) {
            detect.chirp.duration_ms = manifest["audio"]["chirp"].value("ms", 30);
            detect.chirp.low_hz = manifest["audio"]["chirp"].value("low_hz", 700.0);
            detect.chirp.high_hz = manifest["audio"]["chirp"].value("high_hz", 2500.0);
            detect.chirp.level_dbfs = manifest["audio"]["chirp"].value("level_dbfs", -12.0);
        }
        AudioDetector detector(detect);
        const auto hits = detector.detect(audio.data(), audio.size(), 0);
        ReferenceInfo reference;
        reference.type = "file";
        reference.sync_state = "unknown";
        const std::string test_id = have_manifest ? manifest.value("test_id", qr ? qr->test_id : "TEST") : (qr ? qr->test_id : "TEST");
        Pairer pairer(detect.period_ms, GradeWindows{}, reference, "FILE", test_id);
        for (const auto& video : videos) {
            double expected = 0;
            if (have_manifest) {
                for (const auto& event : manifest["events"]) {
                    if (event["seq"].get<uint64_t>() == video.seq) {
                        expected = event["expected_av_offset_ms"].get<double>();
                    }
                }
            } else if (qr && !qr->sweep) {
                expected = qr->offset_ms;
            }
            pairer.observe_video(video, expected);
        }
        for (const auto& hit : hits) {
            AudioObservation obs;
            obs.chirp_id = hit.chirp_id;
            obs.time_ms = hit.time_ms;
            obs.correlation = hit.correlation;
            obs.level_dbfs = hit.level_dbfs;
            obs.clipped = hit.clipped;
            obs.quality = hit.quality;
            obs.duplicate = hit.duplicate;
            obs.ambiguous = hit.ambiguous;
            pairer.observe_audio(obs);
        }
        pairer.finish();
        if (pairer.results().empty()) {
            error = "no event was found";
            return 2;
        }
        nlohmann::json stream = {{"format", std::to_string(width) + "x" + std::to_string(height)},
            {"audio", "pcm " + std::to_string(native_rate) + " Hz ch " + std::to_string(request.channel)},
            {"container", request.path}};
        const bool deviation = std::any_of(pairer.results().begin(), pairer.results().end(), [](const Measurement& m) {
            return std::abs(m.expected_av_offset_ms) > 1e-6;
        });
        auto document = result_document(test_id, "FILE", deviation ? "deviation" : "offset", reference, stream, pairer.results());
        std::cout << "mxl-avsync analyze " << request.path << "\n" << human_report(document);
        if (!request.json_out.empty()) {
            std::ofstream out(request.json_out);
            out << document.dump(2) << "\n";
        } else {
            std::cout << document.dump(2) << "\n";
        }
        if (!request.fail_on.empty()) {
            GradeWindows windows;
            const auto& stats = document["statistics"][deviation ? "deviation" : "window"];
            if (!stats["median"].is_number()) {
                return 2;
            }
            const std::string grade = grade_value(stats["median"].get<double>(), windows);
            if (request.fail_on == "fail" && grade == "fail") {
                return 1;
            }
            if (request.fail_on == "warn" && (grade == "warn" || grade == "fail")) {
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& ex) {
        error = ex.what();
        return 1;
    }
}

int run_validate(const std::string& path, std::string& error)
{
    try {
        std::ifstream in(path);
        if (!in) {
            error = "cannot read " + path;
            return 78;
        }
        nlohmann::json doc;
        in >> doc;
        const std::string schema = doc.value("schema", doc.value("s", ""));
        if (schema != kManifestSchema && schema != kResultSchema && schema != kEventLogSchema && schema != kQrSchema) {
            error = "unknown schema";
            return 78;
        }
        if (schema == kManifestSchema && (!doc.contains("events") || !doc.contains("test_id"))) {
            error = "manifest is missing events";
            return 78;
        }
        if (schema == kResultSchema && (!doc.contains("events") || !doc.contains("reference"))) {
            error = "result is missing events";
            return 78;
        }
        return 0;
    } catch (const std::exception& ex) {
        error = ex.what();
        return 78;
    }
}

int run_merge(const std::string& a_path, const std::string& b_path, const std::string& out_path, std::string& error)
{
    try {
        std::ifstream ain(a_path);
        std::ifstream bin(b_path);
        if (!ain || !bin) {
            error = "cannot read result files";
            return 78;
        }
        nlohmann::json a;
        nlohmann::json b;
        ain >> a;
        bin >> b;
        nlohmann::json doc = merge_result_documents(a, b);
        if (!out_path.empty()) {
            std::ofstream out(out_path);
            out << doc.dump(2) << "\n";
        } else {
            std::cout << doc.dump(2) << "\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        error = ex.what();
        return 1;
    }
}

} // namespace avsync
