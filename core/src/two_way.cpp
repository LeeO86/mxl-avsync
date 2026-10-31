#include "avsync/two_way.hpp"

#include "avsync/timecode.hpp"

namespace avsync {

TwoWayAcoustic two_way_acoustic(const OwnRecording& a, const OwnRecording& b, double self_distance_a_m, double self_distance_b_m, double speed_m_s)
{
    // a.other - a.own = (T_B - T_A) + out_B - out_A + (d - d_AA) / c
    // b.other - b.own = (T_A - T_B) + out_A - out_B + (d - d_BB) / c
    // The sum leaves 2 d / c - (d_AA + d_BB) / c.
    const double sum_ms = (a.other_onset_ms - a.own_onset_ms) + (b.other_onset_ms - b.own_onset_ms);
    TwoWayAcoustic result;
    result.distance_m = speed_m_s * sum_ms / 2000.0 + (self_distance_a_m + self_distance_b_m) / 2.0;
    // b.other - a.own = theta + in_B - in_A + (d - d_AA) / c
    // b.own - a.other = theta + in_B - in_A + (d_BB - d) / c
    const double mean_ms = ((b.other_onset_ms - a.own_onset_ms) + (b.own_onset_ms - a.other_onset_ms)) / 2.0;
    result.clock_offset_plus_input_skew_ms = mean_ms - (self_distance_b_m - self_distance_a_m) / (2.0 * speed_m_s) * 1000.0;
    return result;
}

CrossResult device_cross(const CrossInput& in, std::optional<double> theta_ms, std::optional<double> in_a_reference_ms)
{
    const double path_ms = in.distance_m / in.speed_m_s * 1000.0;
    const double self_a_ms = in.self_distance_a_m / in.speed_m_s * 1000.0;
    const double self_b_ms = in.self_distance_b_m / in.speed_m_s * 1000.0;
    CrossResult result;
    result.round_trip_ms = in.m_ab_ms + in.m_ba_ms - 2.0 * path_ms;
    result.theta_plus_asymmetry_ms = (in.m_ab_ms - in.m_ba_ms) / 2.0;
    const double loop_a = in.loopback_a_ms - self_a_ms; // out_A + in_A
    const double loop_b = in.loopback_b_ms - self_b_ms; // out_B + in_B
    result.consistency_ms = result.round_trip_ms - (loop_a + loop_b);
    if (theta_ms && in_a_reference_ms) {
        DeviceLatencies split;
        split.in_a_ms = *in_a_reference_ms;
        split.out_a_ms = loop_a - split.in_a_ms;
        split.in_b_ms = in.m_ab_ms - path_ms - *theta_ms - split.out_a_ms; // m_ab = out_A + d/c + in_B + theta
        split.out_b_ms = loop_b - split.in_b_ms;
        result.split = split;
    }
    return result;
}

std::optional<OwnRecording> onsets_from_recording(const float* audio, size_t count, int sample_rate, uint32_t own_source_id, uint32_t other_source_id)
{
    std::optional<double> own, other;
    for (const auto& packet : decode_timecodes(audio, count, sample_rate, 0)) {
        if (packet.source_id == own_source_id && !own) own = packet.time_ms;
        if (packet.source_id == other_source_id && !other) other = packet.time_ms;
    }
    if (!own || !other) return std::nullopt;
    return OwnRecording{*own, *other};
}

} // namespace avsync
