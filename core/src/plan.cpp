#include "avsync/avsync.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace avsync {
namespace {

int64_t iround_div(int64_t num, int64_t den)
{
    return (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);
}

} // namespace

int64_t index_to_ns(Rate rate, uint64_t index)
{
    if (rate.num == 0 || rate.den == 0) {
        return 0;
    }
    const __int128 n = rate.num;
    const __int128 d = rate.den;
    return static_cast<int64_t>((static_cast<__int128>(index) * d * 1000000000 + n / 2) / n);
}

uint64_t ns_to_index(Rate rate, uint64_t timestamp_ns)
{
    if (rate.num == 0 || rate.den == 0) {
        return 0;
    }
    const __int128 n = rate.num;
    const __int128 d = rate.den;
    return static_cast<uint64_t>((static_cast<__int128>(timestamp_ns) * n + 500000000 * d) / (1000000000 * d));
}

Rate VideoFormat::grain_rate() const
{
    if (!interlaced) {
        return frame;
    }
    return Rate{frame.num * 2, frame.den};
}

std::optional<VideoFormat> parse_video_format(std::string_view token)
{
    VideoFormat fmt;
    fmt.token = std::string(token);
    if (token == "1080p50") {
        fmt = VideoFormat{"1080p50", 1920, 1080, {50, 1}, false};
    } else if (token == "1080p25") {
        fmt = VideoFormat{"1080p25", 1920, 1080, {25, 1}, false};
    } else if (token == "1080p59.94") {
        fmt = VideoFormat{"1080p59.94", 1920, 1080, {60000, 1001}, false};
    } else if (token == "1080i25") {
        fmt = VideoFormat{"1080i25", 1920, 1080, {25, 1}, true};
    } else if (token == "2160p50") {
        fmt = VideoFormat{"2160p50", 3840, 2160, {50, 1}, false};
    } else if (token == "720p50") {
        fmt = VideoFormat{"720p50", 1280, 720, {50, 1}, false};
    } else if (token == "540p25" || token == "960x540p25") {
        fmt = VideoFormat{"960x540p25", 960, 540, {25, 1}, false};
    } else {
        return std::nullopt;
    }
    return fmt;
}

std::string format_token(const VideoFormat& fmt)
{
    return fmt.token;
}

Period plan_period(const VideoFormat& video, int event_rate)
{
    if (event_rate != 1 && event_rate != 2 && event_rate != 4) {
        throw std::invalid_argument("event rate must be 1, 2 or 4");
    }
    const Rate grain = video.grain_rate();
    const double grain_hz = static_cast<double>(grain.num) / static_cast<double>(grain.den);
    const double exact = grain_hz / static_cast<double>(event_rate);
    int grains = static_cast<int>(std::llround(exact));
    if (grains < 1) {
        grains = 1;
    }
    // Interlaced events fall on a first field, so the period in grains is even.
    if (video.interlaced && (grains % 2) != 0) {
        const int down = std::max(2, grains - 1);
        const int up = grains + 1;
        grains = (std::abs(exact - down) < std::abs(exact - up)) ? down : up;
    }
    Period period;
    period.grains = grains;
    const double ns = static_cast<double>(index_to_ns(grain, static_cast<uint64_t>(grains)));
    period.period_ms = ns / 1e6;
    period.true_rate_hz = (period.period_ms > 0) ? 1000.0 / period.period_ms : 0;
    return period;
}

std::string event_id_for(uint64_t seq)
{
    std::ostringstream os;
    os << "EVT-";
    const std::string digits = std::to_string(seq);
    if (digits.size() < 6) {
        os << std::string(6 - digits.size(), '0');
    }
    os << digits;
    return os.str();
}

namespace {

double offset_for(const PlanConfig& config, uint64_t ordinal)
{
    if (config.sweep.empty()) {
        return config.offset_ms;
    }
    return config.sweep[ordinal % config.sweep.size()];
}

PlannedEvent make_event(const PlanConfig& config, const Period& period, uint64_t seq, uint64_t grain, double offset_ms, bool live)
{
    const Rate grain_rate = config.video.grain_rate();
    PlannedEvent event;
    event.seq = seq;
    event.event_id = event_id_for(seq);
    event.chirp_id = static_cast<int>(seq % 4);
    event.marker_phase = static_cast<int>(seq % 2);
    event.video_grain = grain;
    event.video_tai_ns = index_to_ns(grain_rate, grain);
    event.video_timeline_ms = live ? 0 : static_cast<double>(event.video_tai_ns) / 1e6;
    if (!live) {
        // FILE timestamps are media time. index_to_ns from grain 0 is the media time
        // because the file timeline starts at grain 0. The field above stored that.
        event.video_timeline_ms = static_cast<double>(event.video_tai_ns) / 1e6;
        event.video_tai_ns = 0;
    }
    event.expected_av_offset_ms = offset_ms;
    event.frame_in_period = static_cast<int>(grain % static_cast<uint64_t>(period.grains));
    const double audio_ms = (live ? static_cast<double>(index_to_ns(grain_rate, grain)) / 1e6 : event.video_timeline_ms) + offset_ms;
    event.audio_timeline_ms = live ? 0 : audio_ms;
    if (live) {
        event.audio_tai_ns = event.video_tai_ns + static_cast<int64_t>(std::llround(offset_ms * 1e6));
        event.audio_centre_sample = static_cast<double>(event.audio_tai_ns) * static_cast<double>(config.sample_rate) / 1e9;
        event.audio_sample = static_cast<int64_t>(std::llround(event.audio_centre_sample));
        event.rounding_residual_us = 0;
    } else {
        event.audio_centre_sample = audio_ms * 0.001 * static_cast<double>(config.sample_rate);
        event.audio_sample = static_cast<int64_t>(std::llround(event.audio_centre_sample));
        const double actual_ms = static_cast<double>(event.audio_sample) * 1000.0 / static_cast<double>(config.sample_rate);
        event.rounding_residual_us = (actual_ms - audio_ms) * 1000.0;
        event.audio_timeline_ms = actual_ms;
        // expected offset stays the requested value; the residual is reported separately.
        event.expected_av_offset_ms = offset_ms;
    }
    return event;
}

std::string canonical_of(const EventPlan& plan)
{
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(6);
    os << plan.config.video.token << '|' << plan.config.event_rate << '|' << plan.config.sample_rate << '|'
       << plan.config.duration_ms << '|' << plan.config.first_event_ms << '|' << (plan.config.live ? 1 : 0) << '\n';
    for (const auto& event : plan.events) {
        os << event.event_id << ' ' << event.seq << ' ' << event.chirp_id << ' ' << event.marker_phase << ' '
           << event.video_grain << ' ' << event.video_timeline_ms << ' ' << event.video_tai_ns << ' '
           << event.audio_sample << ' ' << event.audio_timeline_ms << ' ' << event.audio_tai_ns << ' '
           << event.expected_av_offset_ms << ' ' << event.rounding_residual_us << '\n';
    }
    return os.str();
}

} // namespace

EventPlan make_file_plan(const PlanConfig& config)
{
    EventPlan plan;
    plan.config = config;
    plan.period = plan_period(config.video, config.event_rate);
    const Rate grain_rate = config.video.grain_rate();
    const uint64_t first_ns = static_cast<uint64_t>(std::llround(config.first_event_ms * 1e6));
    uint64_t grain = ns_to_index(grain_rate, first_ns);
    const uint64_t step = static_cast<uint64_t>(plan.period.grains);
    if (step == 0) {
        throw std::invalid_argument("empty period");
    }
    if (grain % step != 0) {
        grain += step - (grain % step);
    }
    const int64_t period_ns = index_to_ns(grain_rate, step);
    const int64_t end_ns = static_cast<int64_t>(std::llround(config.duration_ms * 1e6));
    const int64_t last_ns = end_ns - period_ns;
    uint64_t ordinal = 0;
    uint64_t seq = 1;
    while (index_to_ns(grain_rate, grain) <= last_ns) {
        plan.events.push_back(make_event(config, plan.period, seq, grain, offset_for(config, ordinal), false));
        ++seq;
        ++ordinal;
        grain += step;
        if (plan.events.size() > 1000000) {
            break;
        }
    }
    plan.canonical = canonical_of(plan);
    (void)iround_div;
    return plan;
}

PlannedEvent live_event(const PlanConfig& config, uint64_t seq)
{
    const Period period = plan_period(config.video, config.event_rate);
    const uint64_t grain = seq * static_cast<uint64_t>(period.grains);
    PlanConfig live = config;
    live.live = true;
    return make_event(live, period, seq, grain, offset_for(config, seq), true);
}

} // namespace avsync
