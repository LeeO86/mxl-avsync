#pragma once

#include "avsync/avsync.hpp"
#include "config.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace avsync {

struct SourceClip;

struct MediaStatus {
    uint64_t timecode_skipped = 0;
    bool ready = false;
    bool live = true;
    std::string state;
    std::string detail;
    std::string video_flow;
    std::string audio_flow;
    std::string result_flow;
    std::string result_domain;
    std::string domain;
    int width = 0;
    int height = 0;
    int grain_rows = 0;
    double rate_fps = 0;
    bool interlaced = false;
};

struct InputRoute {
    bool enable = false;
    std::string domain_id;
    std::string flow_id;
    std::string sender_id;
};

struct ClockReading {
    std::string sync_state = "unknown";
    std::string accuracy_ns = "unknown";
};

struct LiveCallbacks {
    std::function<Config()> copy_config;
    std::function<bool()> stopped;
    // Uploaded source picture, already decoded; null when none. Returned per frame, never decoded per frame.
    std::function<std::shared_ptr<const SourceClip>()> source_image;
    std::function<ClockReading()> clock_reading;
    std::function<void(const MediaStatus&)> set_status;
    std::function<void(const std::string& flow, const std::string& domain)> set_result;
    std::function<InputRoute(bool video)> input_route;
    std::function<void(const Measurement&)> add_event;
    std::function<void()> count_generator_event;
    std::function<void()> count_video_grain;
    std::function<void()> count_audio_grain;
    std::function<void(bool video)> count_late;
    std::function<void()> count_resync;
};

// Blocks until stop is set. Generator writes video and audio. Analyzer reads them.
// avsync-core is not linked to libmxl; this function is.
void run_live_media(std::atomic<bool>& stop, LiveCallbacks callbacks);

} // namespace avsync
