#include "avsync/avsync.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace avsync {
namespace {

double median_of(std::vector<double> values)
{
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    if (n % 2 == 1) {
        return values[n / 2];
    }
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

std::optional<double> percentile(std::vector<double> values, double p)
{
    if (values.empty()) {
        return std::nullopt;
    }
    std::sort(values.begin(), values.end());
    if (values.size() == 1) {
        return values[0];
    }
    const double rank = (p / 100.0) * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(rank));
    const size_t hi = static_cast<size_t>(std::ceil(rank));
    const double frac = rank - static_cast<double>(lo);
    return values[lo] * (1.0 - frac) + values[hi] * frac;
}

SeriesStats stats_of(const std::vector<double>& values, const std::vector<uint64_t>& seqs, const std::vector<uint64_t>& dropouts)
{
    SeriesStats stats;
    stats.valid = static_cast<int>(values.size());
    stats.dropout_seqs = dropouts;
    stats.series = values;
    if (values.empty()) {
        return stats;
    }
    const double med = median_of(values);
    std::vector<double> absdev;
    absdev.reserve(values.size());
    for (double v : values) {
        absdev.push_back(std::abs(v - med));
    }
    const double mad = median_of(absdev);
    std::vector<double> kept;
    std::vector<char> outlier(values.size(), 0);
    for (size_t i = 0; i < values.size(); ++i) {
        const bool out = std::abs(values[i] - med) > (3.0 * mad);
        if (out) {
            outlier[i] = 1;
            if (i < seqs.size()) {
                stats.outlier_seqs.push_back(seqs[i]);
            }
        } else {
            kept.push_back(values[i]);
        }
    }
    auto fill = [](SeriesStats& dst, const std::vector<double>& src, bool with_percentiles, bool primary) {
        if (src.empty()) {
            return;
        }
        const auto [mn, mx] = std::minmax_element(src.begin(), src.end());
        const double sum = std::accumulate(src.begin(), src.end(), 0.0);
        const double mean = sum / static_cast<double>(src.size());
        double var = 0;
        for (double v : src) {
            const double d = v - mean;
            var += d * d;
        }
        const double stddev = std::sqrt(var / static_cast<double>(src.size()));
        if (primary) {
            dst.min = *mn;
            dst.max = *mx;
            dst.mean = mean;
            dst.median = median_of(src);
            dst.stddev = stddev;
            if (with_percentiles) {
                if (src.size() >= 20) {
                    dst.p5 = percentile(src, 5);
                    dst.p95 = percentile(src, 95);
                    dst.p5_status = "ok";
                    dst.p95_status = "ok";
                }
                if (src.size() >= 100) {
                    dst.p99 = percentile(src, 99);
                    dst.p99_status = "ok";
                }
            }
        } else {
            dst.min_wo = *mn;
            dst.max_wo = *mx;
            dst.mean_wo = mean;
            dst.median_wo = median_of(src);
            dst.stddev_wo = stddev;
        }
    };
    fill(stats, values, true, true);
    fill(stats, kept, false, false);
    return stats;
}

bool has_flag(const Measurement& m, const char* name)
{
    return std::find(m.flags.begin(), m.flags.end(), name) != m.flags.end();
}

} // namespace

bool windows_valid(const GradeWindows& w)
{
    return w.warn_min_ms <= w.pass_min_ms && w.pass_min_ms <= 0 && 0 <= w.pass_max_ms && w.pass_max_ms <= w.warn_max_ms;
}

std::string grade_value(double value_ms, const GradeWindows& windows)
{
    if (value_ms >= windows.pass_min_ms && value_ms <= windows.pass_max_ms) {
        return "pass";
    }
    if (value_ms >= windows.warn_min_ms && value_ms <= windows.warn_max_ms) {
        return "warn";
    }
    return "fail";
}

OffsetWords describe_offset(double ms)
{
    OffsetWords words;
    std::ostringstream number;
    number.setf(std::ios::fixed);
    number.precision(1);
    if (ms > 0) {
        number << '+';
    }
    number << ms << " ms";
    words.number = number.str();
    const long mag = std::lround(std::abs(ms));
    if (std::abs(ms) < 0.05) {
        words.sentence = "audio and video in sync";
    } else if (ms > 0) {
        words.sentence = "audio " + std::to_string(mag) + " ms after video";
    } else {
        words.sentence = "audio " + std::to_string(mag) + " ms before video";
    }
    words.line = words.number + " — " + words.sentence;
    return words;
}

double audio_uncertainty_ms(double correlation)
{
    // Half-widths covering the clean-path and degraded-path p99 measured in M0.
    // A correlation below the detection threshold does not produce a number.
    if (correlation >= 0.95) {
        return 0.05;
    }
    if (correlation >= 0.85) {
        return 0.10;
    }
    if (correlation >= 0.70) {
        return 0.25;
    }
    if (correlation >= 0.50) {
        return 0.50;
    }
    return 1.0;
}

Uncertainty estimate_uncertainty(const UncertaintyInput& in)
{
    Uncertainty out;
    if (!in.have_correlation) {
        out.offset_status = "unknown";
    } else {
        double audio = audio_uncertainty_ms(in.correlation);
        double video = 0;
        if (in.frame_rate_converted || in.drop_or_dup_near) {
            video = in.frame_ms;
        }
        out.offset_ms = audio + video;
        out.offset_status = "known";
    }
    if (!in.live) {
        out.latency_status = "unknown";
        return out;
    }
    if (!in.have_reference || in.sync_state == "unknown") {
        out.latency_status = "unknown";
    } else {
        out.latency_status = "known";
    }
    return out;
}

StatsBundle compute_stats(const std::vector<Measurement>& events)
{
    StatsBundle bundle;
    std::vector<double> offset, deviation, vlat, alat;
    std::vector<uint64_t> oseq, dseq, vseq, aseq, dropouts;
    std::map<std::string, int> reasons;
    int discarded = 0;
    for (const auto& event : events) {
        if (event.grade == "invalid" || !event.av_offset_ms) {
            ++discarded;
            if (!event.reason.empty()) {
                ++reasons[event.reason];
            }
            dropouts.push_back(event.seq);
            continue;
        }
        offset.push_back(*event.av_offset_ms);
        oseq.push_back(event.seq);
        if (event.deviation_ms) {
            deviation.push_back(*event.deviation_ms);
            dseq.push_back(event.seq);
        }
        if (event.video_latency_ms) {
            vlat.push_back(*event.video_latency_ms);
            vseq.push_back(event.seq);
        }
        if (event.audio_latency_ms) {
            alat.push_back(*event.audio_latency_ms);
            aseq.push_back(event.seq);
        }
    }
    bundle.offset = stats_of(offset, oseq, dropouts);
    bundle.offset.discarded = discarded;
    bundle.offset.reasons = reasons;
    bundle.deviation = stats_of(deviation, dseq, {});
    bundle.video_latency = stats_of(vlat, vseq, {});
    bundle.audio_latency = stats_of(alat, aseq, {});
    return bundle;
}

Pairer::Pairer(double period_ms, GradeWindows windows, ReferenceInfo reference, std::string mode, std::string test_id)
    : period_ms_(period_ms)
    , windows_(windows)
    , reference_(std::move(reference))
    , mode_(std::move(mode))
    , test_id_(std::move(test_id))
{
}

void Pairer::observe_video(const VideoObservation& video, double expected_offset_ms)
{
    if (!video.event) {
        return;
    }
    PendingVideo pending;
    pending.video = video;
    pending.expected_offset_ms = expected_offset_ms;
    videos_.push_back(pending);
}

AudioObservation audio_observation(const AudioTimecode& p) {
    AudioObservation a;
    a.estimated_source = p.estimated_source;
    a.timecode = true; a.source_id = p.source_id; a.event_time_ns = p.video_send_ns;
    a.audio_send_ns = p.audio_send_ns; a.time_ms = p.time_ms;
    a.arrival_ms = p.arrival_ms; a.have_arrival = true;
    a.tai_ns = int64_t(std::llround(p.time_ms * 1e6));
    a.correlation = p.correlation; a.level_dbfs = p.level_dbfs; a.quality = chirp_quality(p.correlation, false);
    return a;
}

void Pairer::observe_audio(const AudioObservation& audio)
{
    audios_.push_back(audio);
    audio_used_.push_back(false);
}

Measurement Pairer::make_missing_video(const AudioObservation& audio)
{
    Measurement m;
    m.test_id = test_id_;
    const double event_ms = audio.timecode ? static_cast<double>(audio.event_time_ns) / 1e6 : audio.time_ms;
    m.seq = period_ms_ > 0 ? static_cast<uint64_t>(std::llround(std::max(0.0, event_ms) / period_ms_)) : 0;
    m.event_id = event_id_for(m.seq);
    m.mode = mode_;
    m.have_audio = true;
    m.audio_timecode = audio.timecode;
    m.source_id = audio.source_id;
    m.audio_send_ns = audio.audio_send_ns;
    m.audio_time_ms = audio.time_ms;
    m.audio_tai_ns = audio.tai_ns;
    m.video_time_ms = event_ms; // no video was found; the event's nominal time keeps the log in order
    m.correlation = audio.correlation;
    m.audio_level_dbfs = audio.level_dbfs;
    m.clipped = audio.clipped;
    m.grade = "invalid";
    m.video_quality = "invalid";
    m.audio_quality = audio.quality;
    m.reason = audio.duplicate ? "duplicate" : "missing_video";
    m.flags = {m.reason};
    m.reference = reference_;
    m.uncertainty_status = "unknown";
    return m;
}

Measurement Pairer::make_invalid(const PendingVideo& pending, const std::string& reason, const std::vector<std::string>& flags)
{
    Measurement m;
    m.test_id = test_id_;
    m.seq = pending.video.seq;
    m.event_id = event_id_for(m.seq);
    m.mode = mode_;
    m.have_video = true;
    m.video_time_ms = pending.video.time_ms;
    m.video_tai_ns = pending.video.tai_ns;
    m.expected_av_offset_ms = pending.expected_offset_ms;
    m.grade = "invalid";
    m.video_quality = pending.video.quality;
    m.audio_quality = "invalid";
    m.reason = reason;
    m.flags = flags;
    m.flags.insert(m.flags.end(), pending.video.flags.begin(), pending.video.flags.end());
    m.reference = reference_;
    m.uncertainty_status = "unknown";
    return m;
}

Measurement Pairer::pair_one(const PendingVideo& pending, const AudioObservation* audio, const std::string& reason)
{
    if (audio == nullptr) {
        return make_invalid(pending, reason, {reason});
    }
    Measurement m;
    m.test_id = test_id_;
    m.seq = pending.video.seq;
    m.event_id = event_id_for(m.seq);
    m.mode = mode_;
    m.have_video = true;
    m.have_audio = true;
    m.audio_timecode = audio->timecode; m.source_id = audio->source_id; m.audio_send_ns = audio->audio_send_ns;
    m.video_time_ms = pending.video.time_ms;
    m.audio_time_ms = audio->time_ms;
    m.video_tai_ns = pending.video.tai_ns;
    m.audio_tai_ns = audio->tai_ns;
    m.expected_av_offset_ms = audio->timecode ? (audio->audio_send_ns - audio->event_time_ns) / 1e6 : pending.expected_offset_ms;
    m.av_offset_ms = audio->time_ms - pending.video.time_ms;
    m.deviation_ms = *m.av_offset_ms - m.expected_av_offset_ms;
    m.correlation = audio->correlation;
    m.audio_level_dbfs = audio->level_dbfs;
    m.clipped = audio->clipped;
    m.video_quality = pending.video.quality;
    m.audio_quality = audio->quality;
    m.reference = reference_;
    m.flags = pending.video.flags;
    if (audio->duplicate) {
        m.flags.push_back("duplicate");
    }
    if (audio->ambiguous) {
        m.flags.push_back("ambiguous");
    }
    const bool use_deviation = std::abs(m.expected_av_offset_ms) > 1e-9;
    m.graded_on = use_deviation ? "deviation" : "offset";
    const double graded = use_deviation ? *m.deviation_ms : *m.av_offset_ms;
    if (audio->ambiguous || audio->quality == "invalid" || pending.video.quality == "invalid") {
        m.grade = "invalid";
        m.reason = audio->ambiguous ? "ambiguous" : "quality";
        m.uncertainty_status = "unknown";
    } else {
        m.grade = grade_value(graded, windows_);
        UncertaintyInput input;
        input.correlation = audio->correlation;
        input.have_correlation = true;
        input.frame_rate_converted = std::find(m.flags.begin(), m.flags.end(), "frame_rate_converted") != m.flags.end();
        input.drop_or_dup_near = std::find(m.flags.begin(), m.flags.end(), "video_drop_near") != m.flags.end()
            || std::find(m.flags.begin(), m.flags.end(), "video_dup_near") != m.flags.end();
        input.live = mode_ == "LIVE";
        input.have_reference = reference_.type == "tai-ptp";
        input.sync_state = reference_.sync_state;
        const auto uncertainty = estimate_uncertainty(input);
        m.uncertainty_ms = uncertainty.offset_ms;
        m.uncertainty_status = uncertainty.offset_status;
        m.latency_uncertainty_status = uncertainty.latency_status;
        if (has_flag(m, "optical_capture")) {
            m.uncertainty_ms.reset(); m.uncertainty_status = "unknown";
        }
    }
    // Latency and index shift need the generator's T_k and a locked PTP reading.
    // Audio's set time is T_k plus the configured offset, so a clean path reports a shift near 0.
    m.latency_estimated = pending.video.estimated_source || audio->estimated_source;
    if (m.latency_estimated) {
        m.latency_uncertainty_status = "unknown";
        m.flags.push_back("estimated_source_clock_and_output");
    }
    // Phone source timestamps describe scheduled output on a UTC-derived clock.
    // Keep the requested estimate, and retain the receiver clock status verbatim.
    const bool locked = (reference_.sync_state == "locked" || m.latency_estimated)
        && pending.video.have_set_time && mode_ == "LIVE";
    if (locked) {
        m.have_set_time = true;
        m.set_video_ns = pending.video.set_tai_ns;
        const double tk_ms = static_cast<double>(pending.video.set_tai_ns) / 1e6;
        const double audio_set_ms = audio->timecode ? double(audio->audio_send_ns) / 1e6 : tk_ms + pending.expected_offset_ms;
        m.video_index_shift_ms = pending.video.time_ms - tk_ms;
        m.audio_index_shift_ms = audio->time_ms - audio_set_ms;
        if (pending.video.have_arrival) {
            m.video_latency_ms = pending.video.arrival_ms - tk_ms;
        }
        if (audio->have_arrival) {
            m.audio_latency_ms = audio->arrival_ms - audio_set_ms;
        }
        if (pending.video.have_arrival && audio->have_arrival) {
            m.have_arrival = true;
            m.video_arrival_ms = pending.video.arrival_ms;
            m.audio_arrival_ms = audio->arrival_ms;
            m.av_offset_arrival_ms = audio->arrival_ms - pending.video.arrival_ms;
        }
    }
    if (pending.video.have_set_time && pending.video.have_grain_time) {
        const double content_ms = static_cast<double>(pending.video.set_tai_ns) / 1e6;
        const double grain_ms = static_cast<double>(pending.video.grain_tai_ns) / 1e6;
        m.video_content_vs_grain_ms = content_ms - grain_ms;
    }
    if (audio->have_grain_time) {
        const double grain_ms = static_cast<double>(audio->grain_tai_ns) / 1e6;
        m.audio_content_vs_grain_ms = audio->time_ms - grain_ms;
    }
    if (m.video_content_vs_grain_ms && m.audio_content_vs_grain_ms) {
        m.content_vs_grain_av_ms = *m.video_content_vs_grain_ms - *m.audio_content_vs_grain_ms;
    }
    if (reference_.sync_state == "locked") {
        if (m.video_content_vs_grain_ms && m.video_latency_ms) {
            m.video_content_vs_grain_vs_latency_ms = *m.video_content_vs_grain_ms - *m.video_latency_ms;
        }
        if (m.audio_content_vs_grain_ms && m.audio_latency_ms) {
            m.audio_content_vs_grain_vs_latency_ms = *m.audio_content_vs_grain_ms - *m.audio_latency_ms;
        }
        if (m.video_content_vs_grain_ms && m.video_index_shift_ms) {
            m.video_content_vs_grain_vs_index_shift_ms = *m.video_content_vs_grain_ms - *m.video_index_shift_ms;
        }
        if (m.audio_content_vs_grain_ms && m.audio_index_shift_ms) {
            m.audio_content_vs_grain_vs_index_shift_ms = *m.audio_content_vs_grain_ms - *m.audio_index_shift_ms;
        }
        if (m.content_vs_grain_av_ms && m.av_offset_arrival_ms) {
            m.content_vs_grain_av_vs_arrival_ms = *m.content_vs_grain_av_ms - *m.av_offset_arrival_ms;
        }
    }
    (void)has_flag;
    return m;
}

std::vector<Measurement> Pairer::poll(double now_ms)
{
    std::vector<Measurement> fresh;
    for (auto& pending : videos_) {
        if (pending.resolved) {
            continue;
        }
        const int want = static_cast<int>(pending.video.seq % 4);
        const double target = pending.video.time_ms + pending.expected_offset_ms;
        const AudioObservation* best = nullptr;
        double best_dist = 1e300;
        bool out_of_range = false;
        for (const auto& audio : audios_) {
            if (pending.video.timecode || audio.timecode) {
                if (!pending.video.timecode || !audio.timecode || pending.video.source_id != audio.source_id ||
                    pending.video.event_time_ns != audio.event_time_ns) continue;
                best = &audio; best_dist = 0; break;
            }
            if (audio.chirp_id != want || audio.ambiguous) {
                continue;
            }
            const double dist = std::abs(audio.time_ms - target);
            if (dist <= 2.0 * period_ms_ + 1e-6) {
                if (dist < best_dist) {
                    best = &audio;
                    best_dist = dist;
                }
            } else if (dist <= 4.0 * period_ms_) {
                out_of_range = true;
            }
        }
        const bool window_closed = now_ms > target + (pending.video.timecode ? 10000.0 : 2.0 * period_ms_);
        if (best != nullptr) {
            audio_used_[static_cast<size_t>(best - audios_.data())] = true;
            auto measured = pair_one(pending, best, {});
            pending.resolved = true;
            done_.push_back(measured);
            fresh.push_back(measured);
        } else if (window_closed) {
            auto measured = make_invalid(pending, out_of_range ? "out_of_range" : "missing_audio",
                {out_of_range ? "out_of_range" : "missing"});
            pending.resolved = true;
            done_.push_back(measured);
            fresh.push_back(measured);
        }
    }
    if (live_stream_) {
        if (first_poll_ms_ < 0) first_poll_ms_ = now_ms;
        // An audio event no video claimed within its window is reported, never dropped silently
        // (after a warm-up of two periods: a reader that starts mid-event cannot see that flash).
        for (size_t i = 0; i < audios_.size(); ++i) {
            const auto& audio = audios_[i];
            const double horizon = audio.time_ms + (audio.timecode ? 10000.0 : 4.0 * period_ms_);
            if (!audio_used_[i] && now_ms > horizon && audio.time_ms >= first_poll_ms_ + 2.0 * period_ms_) {
                audio_used_[i] = true;
                auto measured = make_missing_video(audio);
                fresh.push_back(measured);
            }
        }
        // Keep only what can still pair: unresolved videos and audio inside the longest window.
        const double keep_from = now_ms - 10000.0 - 4.0 * period_ms_;
        videos_.erase(std::remove_if(videos_.begin(), videos_.end(), [](const PendingVideo& v) { return v.resolved; }), videos_.end());
        size_t w = 0;
        for (size_t i = 0; i < audios_.size(); ++i) {
            if (!(audio_used_[i] && audios_[i].time_ms < keep_from) && audios_[i].time_ms >= keep_from - 10000.0) {
                audios_[w] = audios_[i];
                audio_used_[w] = audio_used_[i];
                ++w;
            }
        }
        audios_.resize(w);
        audio_used_.resize(w);
        done_.clear();
    }
    return fresh;
}

std::vector<Measurement> Pairer::finish()
{
    double horizon = 0;
    for (const auto& video : videos_) {
        horizon = std::max(horizon, video.video.time_ms + video.expected_offset_ms);
    }
    for (const auto& audio : audios_) {
        horizon = std::max(horizon, audio.time_ms);
    }
    return poll(horizon + std::max(10001.0, 10.0 * period_ms_));
}

std::string current_grade(const std::vector<Measurement>& events, int last_n, const GradeWindows& windows, double now_ms, double stale_s)
{
    std::vector<double> values;
    double last_time = -1e300;
    for (auto it = events.rbegin(); it != events.rend() && static_cast<int>(values.size()) < last_n; ++it) {
        if (it->grade == "invalid" || !it->av_offset_ms) {
            continue;
        }
        last_time = std::max(last_time, it->video_time_ms);
        const double graded = (it->graded_on == "deviation" && it->deviation_ms) ? *it->deviation_ms : *it->av_offset_ms;
        values.push_back(graded);
    }
    if (values.empty()) {
        return "invalid";
    }
    if (now_ms - last_time > stale_s * 1000.0) {
        return "invalid";
    }
    return grade_value(median_of(values), windows);
}

} // namespace avsync
