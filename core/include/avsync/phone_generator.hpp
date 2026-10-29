#pragma once
#include "avsync/avsync.hpp"

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
    static constexpr int width = 1280, height = 720, fps = 60;
private:
    std::string id_;
    int rate_;
    int64_t epoch_;
    double host_;
    uint32_t source_;
};
} // namespace avsync
