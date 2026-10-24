#include "avsync/avsync.hpp"

#include <algorithm>
#include <cmath>

namespace avsync {
namespace {

constexpr int64_t kNinetyDaysMs = 90LL * 24 * 60 * 60 * 1000;

VideoFormat canonical_format(int width, int height, int event_rate)
{
    VideoFormat format;
    format.token = "phone";
    format.width = width;
    format.height = height;
    // The phone tracker only needs a stable period. 50p keeps the file-plan grid
    // when the test picture was drawn at 50 frames per second.
    format.frame = Rate{50, 1};
    format.interlaced = false;
    (void)event_rate;
    return format;
}

} // namespace

CalibrationState calibration_state(const CalibrationRecord* record, std::string_view os_version, int64_t now_unix_ms)
{
    if (record == nullptr || record->events <= 0 || record->model.empty()) {
        return CalibrationState::Missing;
    }
    if (record->os_version != os_version) {
        return CalibrationState::Expired;
    }
    if (now_unix_ms - record->calibrated_unix_ms > kNinetyDaysMs) {
        return CalibrationState::Expired;
    }
    return CalibrationState::Valid;
}

const char* calibration_state_name(CalibrationState state)
{
    switch (state) {
    case CalibrationState::Valid:
        return "valid";
    case CalibrationState::Expired:
        return "expired";
    case CalibrationState::Missing:
        return "missing";
    }
    return "missing";
}

PhoneSession::PhoneSession(int event_rate, GradeWindows windows)
    : event_rate_(event_rate)
    , windows_(windows)
{
}

void PhoneSession::set_calibration(const CalibrationRecord& record, std::string_view os_version, int64_t now_unix_ms)
{
    calibration_ = record;
    calibration_state_ = calibration_state(&record, os_version, now_unix_ms);
}

void PhoneSession::push_video(const uint16_t* luma, int width, int height, int stride, double frame_start_ms, double readout_ms)
{
    if (!tracker_ready_) {
        const LocatedMarker located = read_located_marker(luma, width, height, stride);
        if (!located.located) {
            return;
        }
        canonical_w_ = located.width;
        canonical_h_ = located.height;
        tracker_ = MarkerTracker(canonical_format(canonical_w_, canonical_h_, event_rate_), event_rate_);
        tracker_ready_ = true;
    }
    const LocatedMarker located = read_located_marker(luma, width, height, stride);
    if (!located.located) {
        return;
    }
    std::vector<uint16_t> canonical = located.canonical;
    if (located.width != canonical_w_ || located.height != canonical_h_) {
        canonical.assign(static_cast<size_t>(canonical_w_ * canonical_h_), kYBackground);
        for (int y = 0; y < canonical_h_; ++y) {
            const int sy = std::min(located.height - 1, y * located.height / canonical_h_);
            for (int x = 0; x < canonical_w_; ++x) {
                const int sx = std::min(located.width - 1, x * located.width / canonical_w_);
                canonical[static_cast<size_t>(y) * canonical_w_ + x] = located.canonical[static_cast<size_t>(sy) * located.width + sx];
            }
        }
    }
    const FrameGeometry geo = frame_geometry(canonical_w_, canonical_h_);
    const int row = geo.marker_y + geo.marker_side / 2;
    const double time_ms = marker_row_time_ms(frame_start_ms, readout_ms, row, canonical_h_);
    const int64_t frame_index = static_cast<int64_t>(std::llround(frame_start_ms / 20.0));
    const auto obs = tracker_->push(canonical.data(), canonical_w_, frame_index, time_ms);
    if (obs.event) {
        videos_.push_back(obs);
    }
}

void PhoneSession::push_audio(const float* samples, size_t count, double start_ms, int sample_rate)
{
    if (count == 0 || samples == nullptr || sample_rate <= 0) {
        return;
    }
    if (!audio_set_) {
        audio_origin_ms_ = start_ms;
        audio_rate_ = sample_rate;
        audio_set_ = true;
    } else if (sample_rate == audio_rate_) {
        const double expected = audio_origin_ms_ + static_cast<double>(audio_.size()) * 1000.0 / audio_rate_;
        const double gap_ms = start_ms - expected;
        if (gap_ms > 0.5) {
            const int zeros = static_cast<int>(gap_ms * audio_rate_ / 1000.0);
            audio_.insert(audio_.end(), static_cast<size_t>(std::max(0, zeros)), 0.f);
        }
    }
    audio_.insert(audio_.end(), samples, samples + count);
}

PhoneReport PhoneSession::finish(const std::string& test_id)
{
    PhoneReport report;
    report.calibration = calibration_state_name(calibration_state_);
    report.show_grade_colour = calibration_state_ == CalibrationState::Valid;
    report.uncertainty_ms = report.show_grade_colour ? calibration_.spread_ms : 0;
    if (videos_.empty() || audio_.empty()) {
        return report;
    }
    AudioDetectConfig detect;
    detect.period_ms = 1000.0 / std::max(1, event_rate_);
    detect.timeline_origin_ms = audio_origin_ms_;
    detect.chirp.sample_rate = audio_rate_;
    AudioDetector detector(detect);
    std::vector<float> audio = audio_;
    if (audio_rate_ != 48000) {
        audio = resample_linear(audio_.data(), audio_.size(), audio_rate_, 48000);
        detect.chirp.sample_rate = 48000;
        detect.timeline_origin_ms = audio_origin_ms_;
        detector = AudioDetector(detect);
    }
    const auto hits = detector.detect(audio.data(), audio.size(), 0);
    ReferenceInfo reference;
    reference.type = "none";
    reference.sync_state = "unknown";
    Pairer pairer(detect.period_ms, windows_, reference, "LIVE", test_id);
    for (const auto& video : videos_) {
        pairer.observe_video(video, 0);
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
    report.events = pairer.results();
    if (calibration_state_ == CalibrationState::Valid) {
        for (auto& event : report.events) {
            if (!event.av_offset_ms) {
                continue;
            }
            event.av_offset_ms = *event.av_offset_ms - calibration_.offset_ms;
            event.deviation_ms = *event.av_offset_ms - event.expected_av_offset_ms;
            event.grade = grade_value(*event.av_offset_ms, windows_);
            event.uncertainty_ms = calibration_.spread_ms;
            event.uncertainty_status = "known";
            event.reference = reference;
        }
    } else {
        for (auto& event : report.events) {
            event.flags.push_back("uncalibrated");
            event.uncertainty_status = "unknown";
            event.uncertainty_ms.reset();
            event.reference = reference;
        }
    }
    return report;
}

} // namespace avsync
