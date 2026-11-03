#pragma once

// One live phone measurement, shared by the iOS and Android apps. The platform
// feeds camera luma, microphone samples and QR results (all on the capture
// clock) and gets a JSON status about once a second. Latencies are relative to
// the device UTC clock; the app adds its NTP/GNSS correction.

#include "avsync/avsync.hpp"

#include <array>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace avsync {

class PhoneAnalyzer {
public:
    // A clap/tap take for the manual camera-vs-microphone calibration.
    struct CalibrationFrame { double time_ms = 0; int width = 0, height = 0; std::vector<uint8_t> luma; };
    struct CalibrationTake {
        std::vector<CalibrationFrame> frames; // times relative to the take start
        std::vector<float> audio;              // mono, the review window only
        int sample_rate = 48000;
        double audio_start_ms = 0;             // relative to the take start
        double spike_ms = 0;                   // detected sound, relative to the take start
        double fps = 0;
    };

    PhoneAnalyzer();
    void reset();
    void set_tai_utc_seconds(double seconds);
    // Device UTC = capture time + offset (unix ms). Changing it by more than 100 ms drops the QR.
    void set_device_utc_offset(double unix_minus_capture_ms);

    // QR text and its corners (TL, TR, BR, BL, normalized to the camera image).
    void observe_qr(const std::string& text, const std::array<double, 4>& x, const std::array<double, 4>& y, double capture_ms);
    bool qr_scan_suggested(double capture_ms) const;
    // 8-bit luma plane. pts_ms: exposure start of the first row; exposure and readout 0 when unknown.
    void push_video(const uint8_t* luma, int width, int height, int stride, double pts_ms, double exposure_ms, double readout_ms);
    void push_audio(const float* mono, size_t count, double pts_ms, int sample_rate);

    // Live analysis: about once a second a report (JSON), else empty. Snapshot/merge lets the
    // platform run the slow audio scan on a copy away from the capture thread.
    bool report_due() const;
    std::string poll();
    PhoneAnalyzer snapshot_for_poll();
    void merge_audio(const PhoneAnalyzer& analyzed);

    // Calibration: buffer frames and audio until a sharp sound plus 700 ms.
    void begin_calibration();
    bool calibrating() const { return calibration_; }
    // After a take: the error text, or the take itself (once).
    std::optional<std::string> calibration_error() const { return calibration_error_; }
    std::optional<CalibrationTake> take_calibration();

    // The detected picture (for the preview outline), normalized corners.
    bool picture(std::array<double, 4>& x, std::array<double, 4>& y) const;

private:
    PhoneSession session_;
    std::optional<QrPayload> qr_;
    double qr_ms_ = -1e30;
    double origin_ = -1, last_video_ = -1, last_audio_ = -1, last_report_ = -1;
    int frames_ = 0, video_width_ = 0, video_height_ = 0;
    double tai_utc_ = 37, wall_offset_ = 0;
    bool have_wall_ = false;
    std::deque<double> frame_times_; // for the delivered rate
    // Calibration take.
    bool calibration_ = false, calibration_done_ = false;
    std::optional<std::string> calibration_error_;
    std::optional<CalibrationTake> take_;
    std::deque<CalibrationFrame> pictures_;
    std::vector<float> cal_audio_;
    double cal_audio_origin_ = -1, spike_ = -1, floor_ = .002;
    int rate_ = 48000;
    void finish_calibration(double now);
};

} // namespace avsync
