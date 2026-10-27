#include "avsync/phone_generator.hpp"
#include <cmath>
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
    p.k = seq; p.tk = epoch_ + int64_t(seq - 1) * (1000000000LL / rate_);
    p.source_id = source_; p.timecode = true; p.estimated_source = true;
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
uint64_t PhoneGeneratorPlan::frame_at(double target) const {
    if (!std::isfinite(target) || target < host_) return 0;
    return uint64_t(std::floor((target - host_) * fps + 1e-5));
}
} // namespace avsync
