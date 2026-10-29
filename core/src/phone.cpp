#include "avsync/avsync.hpp"

#include <algorithm>
#include <cmath>

namespace avsync {
namespace {

constexpr int64_t kNinetyDaysMs = 90LL * 24 * 60 * 60 * 1000;

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

void PhoneSession::observe_signal(const QrPayload& qr) {
    if ((qr.rate != 1 && qr.rate != 2 && qr.rate != 4) || !parse_video_format(qr.format)) return;
    const bool changed = signal_ && (signal_->test_id != qr.test_id || signal_->source_id != qr.source_id ||
        signal_->mode != qr.mode || signal_->timecode != qr.timecode || signal_->rate != qr.rate ||
        signal_->format != qr.format || signal_->estimated_source != qr.estimated_source);
    if (changed || (!signal_ && qr.timecode)) {
        // Do not retain a speculative legacy anchor when compact identity first arrives.
        videos_.clear(); locked_seq_.reset(); pending_frames_ = 0; have_transition_ = false;
        marker_phase_ = MarkerReading::State::Invalid; white_onset_ms_.reset();
        if(changed){timecode_stream_.reset(); timecodes_.clear(); audio_.clear(); audio_set_ = false;}
    }
    signal_ = qr; event_rate_ = qr.rate;
}

void PhoneSession::set_calibration(const CalibrationRecord& record, std::string_view os_version, int64_t now_unix_ms)
{
    calibration_ = record;
    calibration_state_ = calibration_state(&record, os_version, now_unix_ms);
}

void PhoneSession::push_video(const uint16_t* luma, int width, int height, int stride, double frame_start_ms, double readout_ms)
{
    if (!cached_pose_.found || frame_start_ms - pose_time_ms_ > 150) {
        cached_pose_ = locate_picture(luma, width, height, stride);
        pose_time_ms_ = frame_start_ms;
    }
    LocatedMarker located = read_located_marker_at_pose(luma, width, height, stride, cached_pose_, 640);
    if (located.located && !located.reading.strip_ok) {
        cached_pose_ = locate_picture(luma, width, height, stride);
        pose_time_ms_ = frame_start_ms;
        located = read_located_marker_at_pose(luma, width, height, stride, cached_pose_, 640);
    }
    current_picture_ = located.pose;
    current_picture_.found = located.located;
    picture_code_confirmed_ = false;
    if (!located.located) {
        pending_frames_ = 0; marker_phase_ = MarkerReading::State::Invalid; white_onset_ms_.reset(); return;
    }
    ++located_frames_;
    const auto& reading = located.reading;
    const double time_ms = marker_row_time_ms(frame_start_ms, readout_ms, height / 2, height);
    bool continuous = marker_phase_pose_.found && time_ms >= marker_phase_ms_ && time_ms-marker_phase_ms_ <= 50;
    for(int i=0;i<4&&continuous;++i) {
        if(std::hypot(current_picture_.corner_x[i]-marker_phase_pose_.corner_x[i],
                      current_picture_.corner_y[i]-marker_phase_pose_.corner_y[i]) >
           std::max(4.0,current_picture_.fiducial_size)) continuous=false;
    }
    if(!continuous){marker_phase_=MarkerReading::State::Invalid;white_onset_ms_.reset();}
    // Optical phase remains usable while strip parity is briefly unreadable.
    // Retain the first clear white frame only after a continuous black phase.
    if(reading.state!=MarkerReading::State::Invalid) {
        if(reading.state==MarkerReading::State::Black) white_onset_ms_.reset();
        else if(marker_phase_==MarkerReading::State::Black) white_onset_ms_=time_ms;
        marker_phase_=reading.state;marker_phase_ms_=time_ms;marker_phase_pose_=current_picture_;
    }
    if (!reading.strip_ok || reading.quality == "invalid" || reading.state == MarkerReading::State::Invalid) {
        pending_frames_ = 0; return;
    }
    ++valid_frames_;
    decoded_seq_ = reading.seq_low24;
    last_valid_frame_ms_ = frame_start_ms;
    // A decoded event number and the marker phase must agree. Monitor refresh,
    // rolling shutter and moire can toggle the block without a new event.
    const bool white = reading.state == MarkerReading::State::White;
    if (signal_ && signal_->timecode) {
        VideoObservation identity;
        identity.seq = reading.seq_low24;
        if (!identify_video(identity, *signal_)) {
            ++sequence_rejected_frames_; pending_frames_ = 0; return;
        }
    }
    if (!(signal_ && signal_->timecode) && white != bool(reading.seq_low24 & 1u)) { ++phase_rejected_frames_; pending_frames_ = 0; return; }
    if (signal_ && signal_->timecode && (!locked_seq_ || reading.seq_low24 != *locked_seq_) && !white) {
        pending_frames_ = 0; return;
    }
    if (!pending_frames_ || pending_seq_ != reading.seq_low24) {
        pending_seq_ = reading.seq_low24; pending_time_ms_ = time_ms; pending_frames_ = 1;
    } else ++pending_frames_;
    // Confirm across frames but retain the FIRST frame's timestamp.
    if (pending_frames_ < 3) return;
    picture_code_confirmed_ = true;
    confirmed_picture_seq_ = pending_seq_;
    confirmed_picture_time_ms_ = frame_start_ms;
    auto anchor = [&] {
        locked_seq_ = pending_seq_; unwrapped_seq_ = pending_seq_;
        last_seen_ms_ = time_ms;
        have_transition_ = signal_ && signal_->timecode && white_onset_ms_ &&
            time_ms >= *white_onset_ms_ && time_ms-*white_onset_ms_ <= 120;
        if(have_transition_)last_transition_ms_=*white_onset_ms_;
    };
    if (!locked_seq_) { anchor(); return; }
    int32_t delta = static_cast<int32_t>((pending_seq_ - *locked_seq_) & 0xFFFFFFu);
    if (delta >= 0x800000) delta -= 0x1000000;
    if (delta == 0) { last_seen_ms_ = time_ms; return; }
    const bool coded_gap = signal_ && signal_->timecode && have_transition_ && delta > 1 && delta <= 4;
    if (delta != 1 && !coded_gap) {
        ++sequence_rejected_frames_;
        if (time_ms - last_seen_ms_ > 1500) anchor();
        return;
    }
    double period = 1000.0 / std::max(1, event_rate_);
    if (signal_) if (auto f = parse_video_format(signal_->format)) period = plan_period(*f, event_rate_).period_ms;
    double event_time_ms=pending_time_ms_;
    if(signal_ && signal_->timecode) {
        if(!white_onset_ms_ || time_ms-*white_onset_ms_>120 || time_ms<*white_onset_ms_) {
            anchor(); return; // Reacquire; a late barcode alone cannot date a flash.
        }
        event_time_ms=*white_onset_ms_;
    }
    if (have_transition_ && std::abs(event_time_ms - last_transition_ms_ - delta * period) > period * 0.25) {
        ++timing_resets_; anchor(); return;
    }
    unwrapped_seq_ += delta; locked_seq_ = pending_seq_; last_seen_ms_ = time_ms;
    last_transition_ms_ = event_time_ms; have_transition_ = true;
    VideoObservation obs;
    obs.event = true; obs.seq = unwrapped_seq_; obs.time_ms = event_time_ms;
    obs.marker_white = white; obs.strip_ok = true; obs.quality = reading.quality;
    obs.contrast = reading.contrast;
    if (signal_) identify_video(obs, *signal_);
    videos_.push_back(obs);
}

void PhoneSession::push_audio(const float* samples, size_t count, double start_ms, int sample_rate)
{
    if (count == 0 || samples == nullptr || sample_rate <= 0) {
        return;
    }
    for (size_t at = 0; at < count; at += size_t(sample_rate / 10)) {
        const size_t n = std::min(count - at, size_t(sample_rate / 10));
        const double time = start_ms + at * 1000.0 / sample_rate;
        timecode_stream_.push(samples + at, n, sample_rate, time, time);
        for (const auto& packet : timecode_stream_.scan()) {
            const auto same = [&](const AudioTimecode& p) { return p.source_id == packet.source_id && p.video_send_ns == packet.video_send_ns && p.audio_send_ns == packet.audio_send_ns; };
            if (std::none_of(timecodes_.begin(), timecodes_.end(), same)) timecodes_.push_back(packet);
        }
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

void PhoneSession::trim_before(double time_ms)
{
    timecodes_.erase(std::remove_if(timecodes_.begin(), timecodes_.end(), [&](const AudioTimecode& p) { return p.time_ms < time_ms; }), timecodes_.end());
    videos_.erase(std::remove_if(videos_.begin(), videos_.end(), [&](const VideoObservation& v) { return v.time_ms < time_ms; }), videos_.end());
    if (!audio_set_ || time_ms <= audio_origin_ms_) return;
    const size_t drop = std::min(audio_.size(), static_cast<size_t>((time_ms - audio_origin_ms_) * audio_rate_ / 1000.0));
    audio_.erase(audio_.begin(), audio_.begin() + drop);
    audio_origin_ms_ += drop * 1000.0 / audio_rate_;
}

PhoneReport PhoneSession::finish(const std::string& test_id)
{
    PhoneReport report;
    report.video_events = videos_.size();
    report.audio_samples = audio_.size();
    report.phase_rejected_frames = phase_rejected_frames_;
    report.sequence_rejected_frames = sequence_rejected_frames_;
    report.timing_resets = timing_resets_;
    report.pending_frames = pending_frames_;
    report.decoded_seq = decoded_seq_;
    report.locked_seq = locked_seq_;
    report.last_valid_frame_ms = last_valid_frame_ms_;
    if (!videos_.empty()) report.last_video_event_ms = videos_.back().time_ms;
    double peak = 0;
    for (float sample : audio_) peak = std::max(peak, std::abs(static_cast<double>(sample)));
    if (peak > 1e-6) report.audio_peak_dbfs = 20 * std::log10(peak);
    report.calibration = calibration_state_name(calibration_state_);
    report.show_grade_colour = calibration_state_ == CalibrationState::Valid;
    report.uncertainty_ms = report.show_grade_colour ? calibration_.spread_ms : 0;
    if (audio_.empty()) {
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
    const bool have_timecode = (signal_ && signal_->timecode) || !timecodes_.empty();
    const size_t diagnostic_window = have_timecode ? 48000 * 2 : 48000 * 6;
    const size_t diagnostic_start = audio.size() > diagnostic_window ? audio.size() - diagnostic_window : 0;
    const auto hits = detector.detect(audio.data() + diagnostic_start, audio.size() - diagnostic_start, diagnostic_start);
    report.audio_hits = hits;
    for (const auto& packet : timecode_stream_.scan(true)) {
        const auto same = [&](const AudioTimecode& p) { return p.source_id == packet.source_id && p.video_send_ns == packet.video_send_ns && p.audio_send_ns == packet.audio_send_ns; };
        if (std::none_of(timecodes_.begin(), timecodes_.end(), same)) timecodes_.push_back(packet);
    }
    std::sort(timecodes_.begin(), timecodes_.end(), [](const auto& a, const auto& b) { return a.time_ms < b.time_ms; });
    report.audio_timecodes = timecodes_;
    if (videos_.empty()) return report;
    ReferenceInfo reference;
    reference.type = "none";
    reference.sync_state = "unknown";
    Pairer pairer(detect.period_ms, windows_, reference, "LIVE", test_id);
    const bool coded = (signal_ && signal_->timecode) || !report.audio_timecodes.empty();
    for (auto video : videos_) {
        if (signal_) identify_video(video, *signal_);
        if (coded && !video.timecode) continue;
        pairer.observe_video(video, 0);
    }
    for (const auto& hit : hits) {
        if (coded) break;
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
    for (const auto& packet : report.audio_timecodes) pairer.observe_audio(audio_observation(packet));
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
