#include "avsync/phone_generator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace avsync {
PhoneGeneratorPlan::PhoneGeneratorPlan(std::string id, int rate, int64_t epoch, double host)
    : id_(std::move(id)), rate_(rate), epoch_(epoch), host_(host), source_(timecode_source_id(id_.c_str())) {
    if ((rate != 1 && rate != 2) || id_.empty() || id_.size() > 40 ||
        id_.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos ||
        !std::isfinite(host) || host < 0) throw std::invalid_argument("invalid phone source plan");
    (void)quantize_timecode_ns(epoch);
}
QrPayload PhoneGeneratorPlan::qr(uint64_t seq) const {
    if (seq == 0 || seq > 86400ULL * rate_) throw std::invalid_argument("phone source session exceeds one day");
    QrPayload p;
    p.test_id = id_; p.mode = "LIVE"; p.rate = rate_; p.format = "720p60";
    p.k = seq; p.tk = epoch_ + int64_t(seq - 1) * (1000000000LL / rate_) + clock_offset_ns(seq);
    p.source_id = source_; p.timecode = true; p.qr_flash = true; p.optical_code = true; p.estimated_source = true;
    return p;
}
double PhoneGeneratorPlan::host_seconds(uint64_t seq) const {
    (void)qr(seq);
    return host_ + double(seq - 1) / rate_;
}
std::vector<float> PhoneGeneratorPlan::audio(uint64_t seq, int sample_rate) const {
    const auto p = qr(seq);
    return timecode_waveform(encode_timecode(source_, p.tk, 0, true, true), 1200, sample_rate);
}
bool PhoneGeneratorPlan::a_white(uint64_t frame) const {
    return optical_a_white(int64_t(frame % uint64_t(period_frames())), optical_chip_grains(fps, rate_), 1 + frame / uint64_t(period_frames()));
}
uint64_t PhoneGeneratorPlan::frame_at(double target) const {
    if (!std::isfinite(target) || target < host_) return 0;
    return uint64_t(std::floor((target - host_) * fps + 1e-5));
}
int64_t PhoneGeneratorPlan::offset_locked(uint64_t seq) const {
    if (slew_.empty() || seq <= slew_.front().first) return slew_.empty() ? 0 : slew_.front().second;
    for (size_t i = 1; i < slew_.size(); ++i) {
        const auto [s0, v0] = slew_[i - 1];
        const auto [s1, v1] = slew_[i];
        if (seq < s1) return v0 + std::llround(double(v1 - v0) * double(seq - s0) / double(s1 - s0));
    }
    return slew_.back().second;
}
int64_t PhoneGeneratorPlan::clock_offset_ns(uint64_t seq) const {
    std::lock_guard<std::mutex> lock(clock_);
    return offset_locked(seq);
}
bool PhoneGeneratorPlan::retime(int64_t offset, uint64_t from, int64_t max_step) {
    if (from == 0 || max_step < 0) return false;
    std::lock_guard<std::mutex> lock(clock_);
    const int64_t current = offset_locked(from), change = offset - current;
    if (std::llabs(change) > max_step) return false;
    // Events before from keep their stamps; a minute of older corners is enough for late renders.
    while (!slew_.empty() && slew_.back().first >= from) slew_.pop_back();
    while (slew_.size() > 1 && slew_[1].first + 120 <= from) slew_.erase(slew_.begin());
    const auto step = std::max<int64_t>(1, std::llround(1e9 / rate_ * clock_slew_ppm / 1e6));
    slew_.push_back({from, current});
    slew_.push_back({from + uint64_t(std::max<int64_t>(1, (std::llabs(change) + step - 1) / step)), offset});
    return true;
}
bool clock_reference_expired(double age_ms, double radius_ms, double limit_ms) {
    return !(age_ms >= 0 && age_ms <= 300000 && radius_ms + age_ms * 50 / 1e6 <= limit_ms);
}
} // namespace avsync
