#pragma once

// Device-to-device calibration (spike, see docs/device-to-device.md). Pure maths and the onset
// extraction from a recording; no audio I/O. Times are in ms on one device's own clock.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace avsync {

inline constexpr double kSpeedOfSoundMs = 343.0; // m/s at 20 °C

// What one device's own recording shows: the onset of its own AVS2 packet (heard through its own
// speaker and microphone) and the onset of the other device's packet, both on this device's clock.
struct OwnRecording {
    double own_onset_ms = 0;
    double other_onset_ms = 0;
};

// Two-way acoustic ranging (BeepBeep). Both devices emit and both record; emission times and the
// speakers' output latencies cancel.
struct TwoWayAcoustic {
    double distance_m = 0; // speaker of one device to microphone of the other
    // Clock of B minus clock of A, plus (input latency of B - input latency of A). Acoustics alone
    // cannot separate the two: a trusted clock (SNTP/PTP) or a reference device is needed.
    double clock_offset_plus_input_skew_ms = 0;
};

// self_distance_*_m: the device's own speaker-to-microphone distance (a few cm on a phone).
TwoWayAcoustic two_way_acoustic(const OwnRecording& a, const OwnRecording& b, double self_distance_a_m, double self_distance_b_m,
    double speed_m_s = kSpeedOfSoundMs);

// Cross measurement: one device generates (AVS2, the packet carries its emission time on its own
// clock), the other measures arrival on its clock; then the roles swap.
//   m_ab = arrival on B - emission on A = out_A + d/c + in_B + theta      (theta = clock B - clock A)
//   m_ba = arrival on A - emission on B = out_B + d/c + in_A - theta
// loopback_*: own onset - own emission on the same device = out + in + self distance / c.
struct CrossInput {
    double m_ab_ms = 0;
    double m_ba_ms = 0;
    double loopback_a_ms = 0;
    double loopback_b_ms = 0;
    double distance_m = 0;
    double self_distance_a_m = 0;
    double self_distance_b_m = 0;
    double speed_m_s = kSpeedOfSoundMs;
};

struct DeviceLatencies {
    double out_a_ms = 0, in_a_ms = 0, out_b_ms = 0, in_b_ms = 0;
};

struct CrossResult {
    double round_trip_ms = 0;          // out_A + in_A + out_B + in_B (both directions, path removed)
    double theta_plus_asymmetry_ms = 0; // theta + ((out_A + in_B) - (out_B + in_A)) / 2
    double consistency_ms = 0;         // round trip from the cross runs minus the two loopbacks: ~0
    // Filled when the clock offset is known (trusted clock) and one latency is known (a reference
    // device or a calibrated input): the four latencies follow.
    std::optional<DeviceLatencies> split;
};

CrossResult device_cross(const CrossInput& in, std::optional<double> theta_ms = std::nullopt, std::optional<double> in_a_reference_ms = std::nullopt);

// Onsets of two AVS2 packets in one recording, told apart by source id. Empty if either is missing.
std::optional<OwnRecording> onsets_from_recording(const float* audio, size_t count, int sample_rate, uint32_t own_source_id,
    uint32_t other_source_id);

} // namespace avsync
