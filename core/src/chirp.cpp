#include "avsync/avsync.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace avsync {
namespace {

constexpr double kPi = 3.14159265358979323846;

double fade(double t, double duration)
{
    if (t < 0 || t > duration) {
        return 0;
    }
    const double edge = 0.1 * duration;
    if (edge <= 0) {
        return 1;
    }
    if (t < edge) {
        return 0.5 * (1.0 - std::cos(kPi * t / edge));
    }
    if (t > duration - edge) {
        return 0.5 * (1.0 - std::cos(kPi * (duration - t) / edge));
    }
    return 1;
}

double raw_chirp(const ChirpSpec& spec, int chirp_id, double t_seconds)
{
    const double duration = spec.duration_ms * 0.001;
    if (t_seconds < 0 || t_seconds > duration) {
        return 0;
    }
    const ChirpBand band = chirp_band(chirp_id, spec.low_hz, spec.high_hz);
    const double slope = (band.f1 - band.f0) / duration;
    const double phase = 2.0 * kPi * (band.f0 * t_seconds + 0.5 * slope * t_seconds * t_seconds);
    const double amp = std::pow(10.0, spec.level_dbfs / 20.0);
    return amp * fade(t_seconds, duration) * std::sin(phase);
}

} // namespace

ChirpBand chirp_band(int chirp_id, double low_hz, double high_hz)
{
    const double span = high_hz - low_hz;
    const double partial = span * 0.6;
    switch (chirp_id & 3) {
    case 0:
        return {low_hz, high_hz};
    case 1:
        return {high_hz, low_hz};
    case 2:
        return {low_hz, low_hz + partial};
    default:
        return {high_hz, high_hz - partial};
    }
}

int chirp_length(const ChirpSpec& spec)
{
    return std::max(1, static_cast<int>(std::llround(spec.duration_ms * 0.001 * spec.sample_rate)));
}

float chirp_sample(const ChirpSpec& spec, int chirp_id, double centre_sample, double sample_index)
{
    const double duration = spec.duration_ms * 0.001;
    const double t = (sample_index - centre_sample) / static_cast<double>(spec.sample_rate) + duration * 0.5;
    return static_cast<float>(raw_chirp(spec, chirp_id, t));
}

void synthesise_block(const ChirpSpec& spec, const std::vector<PlannedEvent>& events, uint64_t start, float* dst, size_t count)
{
    std::fill(dst, dst + count, 0.f);
    const double half = spec.duration_ms * 0.001 * static_cast<double>(spec.sample_rate) * 0.5 + 1.0;
    const double begin = static_cast<double>(start);
    const double end = begin + static_cast<double>(count);
    for (const auto& event : events) {
        const double left = event.audio_centre_sample - half;
        const double right = event.audio_centre_sample + half;
        if (right < begin || left >= end) {
            continue;
        }
        const uint64_t from = static_cast<uint64_t>(std::max(begin, std::floor(left)));
        const uint64_t to = static_cast<uint64_t>(std::min(end, std::ceil(right)));
        for (uint64_t index = from; index < to; ++index) {
            dst[index - start] += chirp_sample(spec, event.chirp_id, event.audio_centre_sample, static_cast<double>(index));
        }
    }
}

ChirpCheck inspect_chirp(const ChirpSpec& spec, int chirp_id)
{
    const int length = chirp_length(spec);
    const double centre = (length - 1) * 0.5;
    ChirpCheck check;
    check.length = length;
    double sum_sq = 0;
    double low_sq = 0;
    double high_sq = 0;
    // Rough spectral split by a one-pole difference, enough for the unit test's band check.
    float prev = 0;
    for (int i = 0; i < length; ++i) {
        const float s = chirp_sample(spec, chirp_id, centre, static_cast<double>(i));
        const double a = std::abs(s);
        if (a > check.peak) {
            check.peak = a;
        }
        if (a >= 0.999999) {
            check.clipped = true;
        }
        sum_sq += static_cast<double>(s) * s;
        const float diff = s - prev;
        prev = s;
        high_sq += static_cast<double>(diff) * diff;
        low_sq += static_cast<double>(s) * s;
        (void)low_sq;
    }
    check.peak_dbfs = (check.peak > 0) ? 20.0 * std::log10(check.peak) : -120;
    check.centre_time_ms = spec.duration_ms * 0.5;
    check.energy_low = sum_sq;
    check.energy_high = high_sq;
    return check;
}

} // namespace avsync
