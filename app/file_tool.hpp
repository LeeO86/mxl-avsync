#pragma once

#include <string>
#include <vector>

namespace avsync {

struct GenerateRequest {
    std::string format = "1080p50";
    double duration_s = 60;
    double offset_ms = 0;
    std::vector<double> sweep;
    int event_rate = 2;
    int audio_rate = 48000;
    int channels = 2;
    std::string container = "mov";
    std::string video_codec = "prores";
    std::string audio_codec = "pcm24";
    std::string title;
    std::string source;
    std::string subtitle;
    std::string source_image;
    std::string notes;
    std::string test_id = "TEST-2026-0001";
    std::string out_path;
    bool allow_container_shift = false;
    int chirp_ms = 30;
    double chirp_low_hz = 700;
    double chirp_high_hz = 2500;
    double chirp_level_dbfs = -12;
};

struct AnalyzeRequest {
    std::string path;
    std::string manifest;
    std::string json_out;
    double from_s = -1;
    double to_s = -1;
    int channel = 0;
    std::string fail_on; // "", "warn", "fail"
    bool locate = false; // stage 2: fiducial search in a scaled or cropped picture
};

// 0 success, 1 container/analysis failure, 2 no events, 78 bad arguments.
int run_generate(const GenerateRequest& request, std::string& error);
int run_analyze(const AnalyzeRequest& request, std::string& error);
int run_validate(const std::string& path, std::string& error);
int run_merge(const std::string& a_path, const std::string& b_path, const std::string& out_path, std::string& error);

} // namespace avsync
