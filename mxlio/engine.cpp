#include "engine.hpp"

#include "domain.hpp"
#include "picture.hpp"
#include "preview.hpp"
#include "uuid.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <set>
#include <thread>

namespace avsync {
namespace {

PlanConfig plan_from(const Config& config, const VideoFormat& format)
{
    PlanConfig plan;
    plan.video = format;
    plan.event_rate = config.event_rate;
    plan.sample_rate = 48000;
    plan.offset_ms = config.offset_ms;
    plan.live = true;
    plan.chirp_ms = config.chirp_ms;
    plan.chirp_low_hz = config.chirp_low_hz;
    plan.chirp_high_hz = config.chirp_high_hz;
    plan.chirp_level_dbfs = config.chirp_level_dbfs;
    return plan;
}

ChirpSpec chirp_from(const Config& config)
{
    ChirpSpec spec;
    spec.sample_rate = 48000;
    spec.duration_ms = config.chirp_ms;
    spec.low_hz = config.chirp_low_hz;
    spec.high_hz = config.chirp_high_hz;
    spec.level_dbfs = config.chirp_level_dbfs;
    return spec;
}

void run_generator(std::atomic<bool>& stop, LiveCallbacks& callbacks)
{
    Config config = callbacks.copy_config();
    const auto format = parse_video_format(config.video_format);
    if (!format) {
        MediaStatus status;
        status.state = "format_mismatch";
        status.detail = "video format";
        callbacks.set_status(status);
        return;
    }
    std::string error;
    bool created_domain = false;
    if (!ensure_output_domain(config.mxl_output_domain_dir, config.mxl_output_domain_id, config.mxl_history_ns, &error, &created_domain)) {
        MediaStatus status;
        status.state = "waiting";
        status.detail = error;
        callbacks.set_status(status);
        while (!stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return;
    }
    Domain domain;
    if (!domain.open(config.mxl_output_domain_dir, &error)) {
        MediaStatus status;
        status.state = "waiting";
        status.detail = error;
        callbacks.set_status(status);
        while (!stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return;
    }
    domain.garbage_collect();
    const std::string video_id = uuid_v5("mxl-avsync/" + config.seed() + "/flow/video/" + format->token);
    const std::string audio_id = uuid_v5("mxl-avsync/" + config.seed() + "/flow/audio/" + std::to_string(config.audio_channels));
    const std::string description = "TEST SIGNAL — AV sync, " + std::to_string(config.event_rate) + "/s";
    Rate pace = format->grain_rate();
    int grain_rows = format->grain_height();
    size_t max_write = 0;
    if (!domain.create_video_writer(video_id, video_flow_json(video_id, "AVSync Test Video", description, *format), &pace, &grain_rows, &error)
        || !domain.create_audio_writer(audio_id, audio_flow_json(audio_id, "AVSync Test Audio", config.audio_channels), &max_write, &error)) {
        MediaStatus status;
        status.state = "waiting";
        status.detail = error;
        callbacks.set_status(status);
        while (!stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return;
    }
    if (format->interlaced && pace.num == format->frame.num && pace.den == format->frame.den) {
        pace = format->grain_rate();
    }
    grain_rows = format->grain_height();
    MediaStatus status;
    status.ready = true;
    status.state = "running";
    status.video_flow = video_id;
    status.audio_flow = audio_id;
    status.domain = config.mxl_output_domain_dir;
    status.width = format->width;
    status.height = format->height;
    status.grain_rows = grain_rows;
    status.interlaced = format->interlaced;
    callbacks.set_status(status);

    const Rate audio_rate{48000, 1};
    uint64_t audio_written = 0;
    bool audio_started = false;
    Yuv422p10 picture;
    picture.resize(format->width, grain_rows);
    std::vector<uint8_t> packed(v210_line_bytes(format->width) * static_cast<size_t>(grain_rows));
    std::vector<float> samples;
    uint64_t qr_seq = ~0ULL;
    std::string qr_cached;

    while (!stop.load()) {
        config = callbacks.copy_config();
        const bool halted = callbacks.stopped();
        const uint64_t now = tai_now();
        uint64_t index = index_now(pace);
        const uint64_t when = time_of_index(pace, index);
        if (now + 1000000 < when) {
            sleep_until_tai(when);
        }
        if (tai_now() > time_of_index(pace, index + 1)) {
            callbacks.count_late(true);
            index = index_now(pace);
        }
        if (halted) {
            domain.write_silence_grain(index);
        } else {
            const Period period = plan_period(*format, config.event_rate);
            const uint64_t seq = index / static_cast<uint64_t>(std::max(1, period.grains));
            const int frame_in_period = static_cast<int>(index % static_cast<uint64_t>(std::max(1, period.grains)));
            const int64_t tai_ns = static_cast<int64_t>(time_of_index(pace, index));
            const double ms = static_cast<double>(tai_ns) / 1e6;
            PictureInfo info;
            info.title = config.title.empty() ? "TEST SIGNAL" : config.title;
            info.source = config.source;
            info.subtitle = config.subtitle;
            info.notes = config.notes;
            info.format_line = format->token;
            info.av_offset_ms = config.offset_ms;
            info.media_ms = ms;
            info.period_ms = period.period_ms;
            info.offset_line = "offset " + std::to_string(static_cast<int>(std::lround(config.offset_ms))) + " ms";
            char counter[80];
            std::snprintf(counter, sizeof(counter), "frame as written by the generator %07llu", static_cast<unsigned long long>(index));
            info.counter = counter;
            char timetext[64];
            std::snprintf(timetext, sizeof(timetext), "t %8.1f ms", ms);
            info.time_line = timetext;
            const int grains = std::max(1, period.grains);
            const uint64_t event_index = (index / static_cast<uint64_t>(grains)) * static_cast<uint64_t>(grains);
            const uint64_t named = index == event_index ? event_index : event_index + static_cast<uint64_t>(grains);
            const uint64_t named_seq = named / static_cast<uint64_t>(grains);
            if (named_seq != qr_seq) {
                QrPayload payload;
                payload.test_id = config.test_id.empty() ? config.seed() : config.test_id;
                payload.mode = "LIVE";
                payload.k = named_seq;
                payload.tk = static_cast<int64_t>(time_of_index(pace, named));
                payload.offset_ms = config.offset_ms;
                payload.rate = config.event_rate;
                payload.format = format->token;
                qr_cached = qr_payload_json(payload);
                qr_seq = named_seq;
            }
            info.qr_json = qr_cached;
            render_picture(picture, info, seq, frame_in_period, period.grains, (seq % 2) != 0);
            pack_v210(picture.y.data(), picture.cb.data(), picture.cr.data(), format->width, grain_rows, format->width, packed.data());
            const bool wrote = domain.write_grain(index, packed.size(), [&](uint8_t* dst, size_t size) {
                std::memcpy(dst, packed.data(), std::min(size, packed.size()));
            });
            if (wrote) {
                callbacks.count_video_grain();
                note_preview_v210("generated", packed.data(), format->width, grain_rows);
                if (frame_in_period == 0) {
                    callbacks.count_generator_event();
                }
            }
        }

        const uint64_t target_ns = time_of_index(pace, index + 1) + static_cast<uint64_t>(std::max(0, config.audio_lead_ms)) * 1000000ULL;
        uint64_t target = index_at(audio_rate, target_ns);
        if (!audio_started) {
            audio_written = target;
            audio_started = true;
        }
        if (target > audio_written && max_write > 0) {
            const int frames = static_cast<int>(std::min<uint64_t>(target - audio_written, 48000));
            samples.assign(static_cast<size_t>(frames * config.audio_channels), 0.f);
            if (!halted && config.audio_enable) {
                PlanConfig plan = plan_from(config, *format);
                const Period period = plan_period(*format, config.event_rate);
                const uint64_t centre_grain = index_at(pace, time_of_index(audio_rate, audio_written));
                uint64_t seq = centre_grain / static_cast<uint64_t>(std::max(1, period.grains));
                if (seq > 2) {
                    seq -= 2;
                } else {
                    seq = 0;
                }
                std::vector<PlannedEvent> events;
                for (int extra = 0; extra < 6; ++extra) {
                    events.push_back(live_event(plan, seq + static_cast<uint64_t>(extra)));
                }
                std::vector<float> mono(static_cast<size_t>(frames));
                synthesise_block(chirp_from(config), events, audio_written, mono.data(), mono.size());
                for (int frame = 0; frame < frames; ++frame) {
                    for (int channel = 0; channel < config.audio_channels; ++channel) {
                        samples[static_cast<size_t>(frame * config.audio_channels + channel)] = mono[static_cast<size_t>(frame)];
                    }
                }
            }
            if (domain.write_samples(audio_written + static_cast<uint64_t>(frames), samples.data(), frames, config.audio_channels, max_write)) {
                audio_written += static_cast<uint64_t>(frames);
                if (callbacks.count_audio_grain) {
                    callbacks.count_audio_grain();
                }
            } else {
                callbacks.count_late(false);
            }
        }
        sleep_until_tai(time_of_index(pace, index + 1));
    }
    domain.close();
    if (config.mxl_cleanup_on_exit && created_domain) {
        remove_own_domain(config.mxl_output_domain_dir, config.mxl_output_domain_id);
    }
}

struct ResultCaption {
    std::mutex mu;
    std::string line = "NO EVENTS";
    std::string grade = "invalid";
    std::string state = "WAITING";
};

bool result_format_of(const Config& config, VideoFormat* format)
{
    if (format == nullptr) {
        return false;
    }
    if (auto known = parse_video_format(config.result_format)) {
        *format = *known;
        format->interlaced = false;
        format->frame = Rate{std::max(1, config.result_rate), 1};
        format->token = config.result_format;
        return format->width >= 16 && format->height >= 16;
    }
    int width = 0;
    int height = 0;
    if (std::sscanf(config.result_format.c_str(), "%dx%d", &width, &height) == 2 && width >= 16 && height >= 16 && (width % 2) == 0) {
        *format = VideoFormat{config.result_format, width, height, {std::max(1, config.result_rate), 1}, false};
        return true;
    }
    return false;
}

void paint_result(Yuv422p10& frame, const std::string& line, const std::string& grade, const std::string& state)
{
    uint16_t y = 180;
    uint16_t cb = kYNeutralChroma;
    uint16_t cr = kYNeutralChroma;
    if (grade == "pass") {
        y = 520;
        cb = 360;
        cr = 400;
    } else if (grade == "warn") {
        y = 640;
        cb = 420;
        cr = 640;
    } else if (grade == "fail") {
        y = 420;
        cb = 480;
        cr = 720;
    }
    std::fill(frame.y.begin(), frame.y.end(), y);
    std::fill(frame.cb.begin(), frame.cb.end(), cb);
    std::fill(frame.cr.begin(), frame.cr.end(), cr);
    LumaView view{frame.y.data(), frame.width, frame.height, frame.width};
    draw_text(view, 24, 28, "AV SYNC", 2, kYWhite);
    const std::string shown = line.size() > 42 ? line.substr(0, 42) : line;
    draw_text(view, 24, 80, shown, 2, kYWhite);
    draw_text(view, 24, 140, grade, 2, kYWhite);
    draw_text(view, 24, 190, state, 2, kYWhite);
}

void run_result_writer(std::atomic<bool>& stop, LiveCallbacks& callbacks, ResultCaption& caption)
{
    const Config config = callbacks.copy_config();
    if (!config.result_enable) {
        return;
    }
    VideoFormat format;
    if (!result_format_of(config, &format)) {
        return;
    }
    std::string error;
    bool created = false;
    if (!ensure_output_domain(config.mxl_output_domain_dir, config.mxl_output_domain_id, config.mxl_history_ns, &error, &created)) {
        return;
    }
    Domain domain;
    if (!domain.open(config.mxl_output_domain_dir, &error)) {
        return;
    }
    domain.garbage_collect();
    const std::string flow_id = uuid_v5("mxl-avsync/" + config.seed() + "/flow/result/" + config.result_format);
    Rate pace = format.frame;
    if (!domain.create_result_writer(flow_id, video_flow_json(flow_id, "AVSync Result Video", "AV sync result", format, "AVSync Result:Video"), &pace, &error)) {
        return;
    }
    if (callbacks.set_result) {
        callbacks.set_result(flow_id, config.mxl_output_domain_dir);
    }
    Yuv422p10 picture;
    picture.resize(format.width, format.height);
    std::vector<uint8_t> packed(v210_line_bytes(format.width) * static_cast<size_t>(format.height));
    auto last_draw = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    while (!stop.load()) {
        if (callbacks.stopped && callbacks.stopped()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        const uint64_t index = index_now(pace);
        const uint64_t when = time_of_index(pace, index);
        if (tai_now() + 1000000 < when) {
            sleep_until_tai(when);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_draw >= std::chrono::milliseconds(200)) {
            std::string line;
            std::string grade;
            std::string state;
            {
                std::lock_guard lock(caption.mu);
                line = caption.line;
                grade = caption.grade;
                state = caption.state;
            }
            paint_result(picture, line, grade, state);
            pack_v210(picture.y.data(), picture.cb.data(), picture.cr.data(), format.width, format.height, format.width, packed.data());
            last_draw = now;
        }
        if (domain.write_result_grain(index, packed.size(), [&](uint8_t* dst, size_t size) {
                std::memcpy(dst, packed.data(), std::min(size, packed.size()));
            })) {
            note_preview_v210("generated", packed.data(), format.width, format.height);
        }
        sleep_until_tai(time_of_index(pace, index + 1));
    }
    domain.close();
    if (config.mxl_cleanup_on_exit && created) {
        remove_own_domain(config.mxl_output_domain_dir, config.mxl_output_domain_id);
    }
}

bool acceptable_video(const FlowDesc& flow)
{
    return flow.media_type == "video/v210" && flow.width >= 2 && flow.height >= 2 && !flow.bottom_field_first;
}

bool acceptable_audio(const FlowDesc& flow)
{
    return flow.media_type == "audio/float32" && flow.channels > 0;
}

void run_analyzer(std::atomic<bool>& stop, LiveCallbacks& callbacks)
{
    Domain video_domain;
    Domain audio_domain;
    FlowDesc video;
    FlowDesc audio;
    FlowFileId video_file{};
    FlowFileId audio_file{};
    Rate pace{};
    int grain_rows = 0;
    uint32_t grain_count = 0;
    int audio_channels = 0;
    size_t max_read = 0;
    bool have_cursor = false;
    uint64_t cursor = 0;
    uint64_t audio_cursor = 0;
    uint64_t stall_head = 0;
    uint64_t stall_since = 0;
    std::vector<uint8_t> packed;
    std::vector<uint16_t> luma;
    std::vector<float> mono;
    std::vector<float> audio_window;
    uint64_t window_origin = 0;
    std::set<int64_t> heard;
    std::optional<MarkerTracker> tracker;
    std::optional<Pairer> pairer;
    std::string tracker_flow;
    MediaStatus status;
    status.state = "waiting";
    status.detail = "no flow";
    callbacks.set_status(status);
    ResultCaption caption;
    std::thread result_thread(run_result_writer, std::ref(stop), std::ref(callbacks), std::ref(caption));

    auto close_readers = [&] {
        video_domain.close();
        audio_domain.close();
        have_cursor = false;
        stall_since = 0;
    };
    auto current_reference = [&](const std::string& domain) {
        ReferenceInfo reference;
        reference.type = "tai-ptp";
        reference.domain = domain;
        reference.sync_state = "unknown";
        reference.accuracy = "unknown";
        if (callbacks.clock_reading) {
            const ClockReading clock = callbacks.clock_reading();
            if (clock.sync_state == "locked" || clock.sync_state == "unlocked" || clock.sync_state == "unknown") {
                reference.sync_state = clock.sync_state;
            }
            if (!clock.accuracy_ns.empty()) {
                reference.accuracy = clock.accuracy_ns;
            }
        }
        return reference;
    };

    while (!stop.load()) {
        const Config config = callbacks.copy_config();
        if (!video_domain.is_open() || !audio_domain.is_open()) {
            video = {};
            audio = {};
            if (config.nmos_enable && callbacks.input_route) {
                const auto video_route = callbacks.input_route ? callbacks.input_route(true) : InputRoute{};
                const auto audio_route = callbacks.input_route ? callbacks.input_route(false) : InputRoute{};
                if (!video_route.enable || video_route.flow_id.empty() || !audio_route.enable || audio_route.flow_id.empty()) {
                    status.ready = false;
                    status.state = "not_routed";
                    status.detail = "waiting for IS-05";
                    callbacks.set_status(status);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                for (const auto& flow : scan_flows(config.mxl_domain_scan)) {
                    if (flow.domain_id == video_route.domain_id && flow.flow_id == video_route.flow_id) {
                        video = flow;
                    }
                    if (flow.domain_id == audio_route.domain_id && flow.flow_id == audio_route.flow_id) {
                        audio = flow;
                    }
                }
                if (video.flow_id.empty() || audio.flow_id.empty()) {
                    status.ready = false;
                    status.state = "waiting";
                    status.detail = "routed flow is not in the scan path";
                    callbacks.set_status(status);
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    continue;
                }
            } else {
                const auto own_domain = std::filesystem::absolute(config.mxl_output_domain_dir);
                for (const auto& flow : scan_flows(config.mxl_domain_scan)) {
                    if (std::filesystem::absolute(flow.domain_path) == own_domain) {
                        continue;
                    }
                    if (video.flow_id.empty() && acceptable_video(flow)) {
                        video = flow;
                    } else if (flow.media_type == "video/v210" && flow.bottom_field_first) {
                        status.ready = false;
                        status.state = "format_mismatch";
                        status.detail = "bottom field first";
                        callbacks.set_status(status);
                    }
                    if (audio.flow_id.empty() && acceptable_audio(flow)) {
                        audio = flow;
                    }
                }
            }
            std::string error;
            if (!video.flow_id.empty() && !audio.flow_id.empty() && flow_file_id(video.domain_path, video.flow_id, &video_file)
                && flow_file_id(audio.domain_path, audio.flow_id, &audio_file) && video_domain.open(video.domain_path, &error)
                && video_domain.create_video_reader(video.flow_id, &video, &pace, &grain_rows, &grain_count, &error)
                && audio_domain.open(audio.domain_path, &error)
                && audio_domain.create_audio_reader(audio.flow_id, &audio_channels, &max_read, &error)) {
                if (video.interlaced && video.height % 2 != 0) {
                    status.state = "format_mismatch";
                    status.detail = "odd interlaced height";
                    status.ready = false;
                    callbacks.set_status(status);
                    close_readers();
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    continue;
                }
                grain_rows = video.interlaced ? video.height / 2 : video.height;
                status.ready = true;
                status.state = "running";
                status.detail.clear();
                status.video_flow = video.flow_id;
                status.audio_flow = audio.flow_id;
                status.domain = video.domain_path;
                status.width = video.width;
                status.height = video.height;
                status.grain_rows = grain_rows;
                status.interlaced = video.interlaced;
                callbacks.set_status(status);
                luma.assign(static_cast<size_t>(video.width) * static_cast<size_t>(grain_rows), kYBlack);
            } else {
                close_readers();
                status.ready = false;
                if (status.state != "format_mismatch") {
                    status.state = "waiting";
                    status.detail = error.empty() ? "no flow" : error;
                    callbacks.set_status(status);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }
        }

        FlowFileId now_video{};
        FlowFileId now_audio{};
        const bool missing = !flow_file_id(video.domain_path, video.flow_id, &now_video) || !(now_video == video_file)
            || !flow_file_id(audio.domain_path, audio.flow_id, &now_audio) || !(now_audio == audio_file);
        const uint64_t head = video_domain.video_head();
        const uint64_t now_ns = tai_now();
        if (head == stall_head) {
            if (stall_since == 0) {
                stall_since = now_ns;
            } else if (now_ns > stall_since && now_ns - stall_since >= 1000000000ULL) {
                callbacks.count_resync();
                close_readers();
                status.state = "no_signal";
                status.detail = "head stood still";
                status.ready = false;
                callbacks.set_status(status);
                continue;
            }
        } else {
            stall_head = head;
            stall_since = now_ns;
        }
        if (missing) {
            callbacks.count_resync();
            close_readers();
            continue;
        }

        const uint64_t want = !have_cursor || cursor + 1 < head ? head : cursor + 1;
        bool flow_invalid = false;
        uint64_t grain_index = 0;
        if (!video_domain.copy_grain(want, 40000000, grain_rows, video.width, grain_count, &packed, &flow_invalid, &grain_index) || flow_invalid) {
            if (flow_invalid) {
                callbacks.count_resync();
                close_readers();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            continue;
        }
        cursor = want;
        have_cursor = true;
        const uint64_t video_arrived_ns = tai_now();
        note_preview_v210("input", packed.data(), video.width, grain_rows);
        unpack_v210_luma(packed.data(), video.width, grain_rows, luma.data(), video.width);
        VideoFormat tracked;
        tracked.token = video.interlaced ? "field" : "progressive";
        tracked.width = video.width;
        tracked.height = video.height;
        tracked.interlaced = video.interlaced;
        tracked.frame = video.interlaced ? Rate{std::max<int64_t>(1, pace.num), pace.den * 2} : pace;
        if (tracker_flow != video.flow_id || !tracker || !pairer) {
            tracker.emplace(tracked, config.event_rate);
            const ReferenceInfo reference = current_reference(video.domain_id);
            const double period_ms = plan_period(tracked, config.event_rate).period_ms;
            pairer.emplace(period_ms, GradeWindows{config.pass_min_ms, config.pass_max_ms, config.warn_min_ms, config.warn_max_ms}, reference, "LIVE",
                config.test_id.empty() ? config.seed() : config.test_id);
            tracker_flow = video.flow_id;
            heard.clear();
            audio_window.clear();
            window_origin = 0;
        }
        const int64_t tai_ns = static_cast<int64_t>(time_of_index(pace, want));
        VideoObservation observation = tracker->push(luma.data(), video.width, static_cast<int64_t>(want), static_cast<double>(tai_ns) / 1e6, tai_ns);
        if (observation.event) {
            observation.have_arrival = true;
            observation.arrival_ms = static_cast<double>(video_arrived_ns) / 1e6;
            observation.have_grain_time = true;
            observation.grain_tai_ns = static_cast<int64_t>(time_of_index(pace, grain_index));
            if (auto text = decode_qr_text(luma.data(), video.width, grain_rows, video.width)) {
                if (auto qr = parse_qr_payload(*text)) {
                    if (qr->k == observation.seq) {
                        observation.have_set_time = true;
                        observation.set_tai_ns = qr->tk;
                    }
                }
            }
            pairer->observe_video(observation, config.offset_ms);
        }

        const uint64_t audio_head = audio_domain.audio_head();
        // Stay inside the flow history (200 ms is 9600 samples). Older samples are already gone.
        const uint64_t keep_back = 8000;
        if (audio_cursor == 0 || audio_head > audio_cursor + keep_back) {
            audio_cursor = audio_head > keep_back ? audio_head - keep_back : 0;
            audio_window.clear();
            window_origin = audio_cursor;
        }
        if (audio_head > audio_cursor) {
            const int frames = static_cast<int>(std::min<uint64_t>(audio_head - audio_cursor, 2000));
            std::vector<float> interleaved;
            if (audio_domain.copy_samples(audio_cursor + static_cast<uint64_t>(frames), frames, audio_channels, 20000000, max_read, &interleaved, &flow_invalid)
                && !flow_invalid) {
                const double audio_arrived_ms = static_cast<double>(tai_now()) / 1e6;
                if (audio_window.empty()) {
                    window_origin = audio_cursor;
                }
                mono.resize(static_cast<size_t>(frames));
                for (int frame = 0; frame < frames; ++frame) {
                    mono[static_cast<size_t>(frame)] = interleaved[static_cast<size_t>(frame * audio_channels + std::min(config.audio_channel, audio_channels - 1))];
                }
                audio_window.insert(audio_window.end(), mono.begin(), mono.end());
                const size_t keep = 48000;
                if (audio_window.size() > keep) {
                    const size_t drop = audio_window.size() - keep;
                    audio_window.erase(audio_window.begin(), audio_window.begin() + static_cast<std::ptrdiff_t>(drop));
                    window_origin += drop;
                }
                AudioDetectConfig detect;
                detect.chirp = chirp_from(config);
                detect.threshold = config.audio_threshold;
                detect.period_ms = plan_period(tracked, config.event_rate).period_ms;
                const Rate audio_rate{48000, 1};
                detect.timeline_origin_ms = static_cast<double>(time_of_index(audio_rate, window_origin)) / 1e6;
                AudioDetector detector(detect);
                const auto hits = detector.detect(audio_window.data(), audio_window.size(), window_origin);
                for (const auto& hit : hits) {
                    const int64_t key = static_cast<int64_t>(std::llround(hit.sample_pos));
                    if (!heard.insert(key).second) {
                        continue;
                    }
                    AudioObservation heard_hit;
                    heard_hit.chirp_id = hit.chirp_id;
                    heard_hit.time_ms = hit.time_ms;
                    heard_hit.tai_ns = static_cast<int64_t>(std::llround(hit.time_ms * 1e6));
                    heard_hit.correlation = hit.correlation;
                    heard_hit.level_dbfs = hit.level_dbfs;
                    heard_hit.clipped = hit.clipped;
                    heard_hit.quality = hit.quality;
                    heard_hit.duplicate = hit.duplicate;
                    heard_hit.ambiguous = hit.ambiguous;
                    heard_hit.have_arrival = true;
                    heard_hit.arrival_ms = audio_arrived_ms;
                    const auto sample_index = static_cast<uint64_t>(std::llround(std::max(0.0, hit.sample_pos)));
                    heard_hit.have_grain_time = true;
                    heard_hit.grain_tai_ns = static_cast<int64_t>(time_of_index(Rate{48000, 1}, sample_index));
                    pairer->observe_audio(heard_hit);
                }
                audio_cursor += static_cast<uint64_t>(frames);
                if (heard.size() > 128) {
                    heard.erase(heard.begin());
                }
            } else if (flow_invalid) {
                callbacks.count_resync();
                close_readers();
                tracker.reset();
                pairer.reset();
                tracker_flow.clear();
                continue;
            }
        }
        if (pairer) {
            pairer->set_reference(current_reference(video.domain_id));
            for (const auto& event : pairer->poll(static_cast<double>(tai_now()) / 1e6)) {
                {
                    std::lock_guard lock(caption.mu);
                    caption.grade = event.grade;
                    caption.state = event.grade == "invalid" ? "NO EVENTS" : "RUNNING";
                    caption.line = event.av_offset_ms ? describe_offset(*event.av_offset_ms).line : event.reason;
                }
                callbacks.add_event(event);
            }
        }
    }
    close_readers();
    if (result_thread.joinable()) {
        result_thread.join();
    }
}

} // namespace

void run_live_media(std::atomic<bool>& stop, LiveCallbacks callbacks)
{
    struct PreviewLife {
        PreviewLife() { start_preview_worker(); }
        ~PreviewLife() { stop_preview_worker(); }
    } preview_life;
    const Config config = callbacks.copy_config();
    if (config.role == "generator") {
        run_generator(stop, callbacks);
    } else if (config.role == "analyzer") {
        run_analyzer(stop, callbacks);
    }
    (void)preview_life;
}

} // namespace avsync
