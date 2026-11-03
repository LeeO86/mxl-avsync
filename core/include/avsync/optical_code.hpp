#pragma once

// AVS3 optical code: two adjacent fields A and B, always opposite (one white,
// one black), so their sum stays constant. Around each event they swap in a
// fixed burst of 8 transitions. Every transition is the same physical change
// (one field rises, one falls), so each has the same display/camera delay.
// The detector times each A = B crossing and averages all of them.

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace avsync {

constexpr int kOpticalTransitions = 8;

// Grains per chip: the whole number of grains closest to 40 ms at 2 events/s
// and 80 ms at 1 event/s (for 30 fps cameras), at least 1.
int optical_chip_grains(double grain_rate, int event_rate);
// Chip offsets of the 8 transitions. Variant = event sequence parity.
const std::array<int, kOpticalTransitions>& optical_offsets(int variant);
// Burst length in chips (the last transition).
int optical_span_chips();
// AVS3 QR timing: the QR names event k from half a period before its burst to half after.
inline uint64_t optical_named_seq(uint64_t grain_index, int period_grains)
{
    return (2 * grain_index + uint64_t(period_grains)) / (2 * uint64_t(period_grains));
}
// True when field A is white at this grain of the period. Rest state: A black, B white.
bool optical_a_white(int64_t grain_in_period, int chip_grains, uint64_t seq);

class OpticalCodeDetector {
public:
    struct Sample {
        double start_ms = 0;    // exposure start of the marker rows (or grain start)
        double exposure_ms = 0; // integration time (a grain's full duration for digital input)
        double a = 0, b = 0;    // field levels, any monotonic scale
    };
    struct Burst {
        double time_ms = 0;  // time of transition 0
        int variant = 0;     // 0/1 = event sequence parity
        int used = 0;        // crossings in the average
        double rms_ms = 0;   // residual spread of the used crossings
        double contrast = 0; // smaller of the two fields' level ranges
    };
    struct Diagnostics {
        uint64_t samples = 0, crossings = 0, gap_crossings = 0, bursts = 0, rejected = 0, low_contrast = 0;
    };
    // chip_ms: chip duration at the source; period_ms: event period.
    OpticalCodeDetector(double chip_ms = 40, double period_ms = 500);
    void configure(double chip_ms, double period_ms);
    void reset();
    void push(const Sample& sample);
    std::vector<Burst> take();
    const Diagnostics& diagnostics() const { return diagnostics_; }

private:
    struct Crossing { double time_ms; int direction; };
    double chip_ms_ = 40, period_ms_ = 500;
    std::deque<Sample> samples_;
    std::deque<Crossing> crossings_;
    std::vector<Burst> ready_;
    int state_ = 0; // +1: A above B, -1: below, 0: unknown
    int pending_dir_ = 0; // a flip waiting for its edge to settle
    double pending_ms_ = 0;
    double gamma_ = 2.2;
    double interval_ms_ = 0; // running typical sample interval
    Diagnostics diagnostics_;
    bool range(double& amin, double& amax, double& bmin, double& bmax) const;
    double level(const Sample& s, double amin, double amax, double bmin, double bmax) const;
    void crossing(double amin, double amax, double bmin, double bmax);
    void close_burst(double now_ms);
};

} // namespace avsync
