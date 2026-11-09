#pragma once
#include "avsync/avsync.hpp"
#include "avsync/optical_code.hpp"
#include <mutex>
#include <utility>
#include <vector>

namespace avsync {
// A scheduled source, not an independent measurement of display/DAC emission.
// Host seconds are shared by CADisplayLink and AVAudioTime on iOS.
class PhoneGeneratorPlan {
public:
    PhoneGeneratorPlan(std::string test_id, int event_rate, int64_t epoch_tai_ns, double epoch_host_seconds);
    QrPayload qr(uint64_t seq) const;
    double host_seconds(uint64_t seq) const;
    std::vector<float> audio(uint64_t seq, int sample_rate = 48000) const;
    int period_frames() const { return 60 / rate_; }
    uint64_t frame_at(double target_host_seconds) const;
    // AVS3: sequence the QR names at this frame (from half a period before each burst).
    uint64_t named_seq(uint64_t frame) const { return 1 + optical_named_seq(frame, period_frames()); }
    // AVS3: field A white at this frame (B is always the opposite).
    bool a_white(uint64_t frame) const;
    // Clock refresh (docs/phone-clock.md). A newer clock check changes the TAI stamps of the events
    // from from_seq on (the first one not scheduled yet), never their host times: frames stay on the
    // display grid, and the sound and the picture of one event keep one stamp. The stamps move toward
    // offset_ns (relative to the start mapping) by at most clock_slew_ppm of the event period per event.
    // A change above max_step_ns is refused: false, nothing changes.
    bool retime(int64_t offset_ns, uint64_t from_seq, int64_t max_step_ns);
    int64_t clock_offset_ns(uint64_t seq) const;
    static constexpr int width = 1280, height = 720, fps = 60;
    static constexpr double clock_slew_ppm = 100;
private:
    int64_t offset_locked(uint64_t seq) const;
    std::string id_;
    int rate_;
    int64_t epoch_;
    double host_;
    uint32_t source_;
    // (seq, offset ns) corners of the slew, linear in between. audio() and the picture renderer may
    // read stamps on another thread than retime().
    mutable std::mutex clock_;
    std::vector<std::pair<uint64_t, int64_t>> slew_;
};

// The phone generator's clock reference rule, as referenceAt in mobile-app/src/time-reference.mjs:
// usable while at most 300 s old and the check's radius plus 50 ppm holdover stays within the limit.
bool clock_reference_expired(double age_ms, double radius_ms, double limit_ms);
} // namespace avsync
