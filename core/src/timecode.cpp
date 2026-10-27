#include "avsync/timecode.hpp"
#include "avsync/avsync.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace avsync {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int preamble_bits = 32, sync_bits = 32;
constexpr uint32_t sync_word = 0x1ACFFC1D;
constexpr int permutation = 73;
using Bytes = std::vector<uint8_t>;
uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}
void put(Bytes &b, int at, uint64_t value, int n) {
    for (int i = 0; i < n; i++)
        b[at + n - 1 - i] = uint8_t(value >> (8 * i));
}
uint64_t get(const Bytes &b, int at, int n) {
    uint64_t value = 0;
    for (int i = 0; i < n; i++)
        value = (value << 8) | b[at + i];
    return value;
}
std::vector<int> fec(const Bytes &b) {
    const int payload_bytes = int(b.size());
    std::vector<int> out;
    int state = 0;
    for (int t = 0; t < payload_bytes * 8 + 6; t++) {
        int bit = t < payload_bytes * 8 ? ((b[t / 8] >> (7 - t % 8)) & 1) : 0;
        int reg = (state << 1) | bit;
        out.push_back(std::popcount(unsigned(reg & 0x79)) & 1);
        out.push_back(std::popcount(unsigned(reg & 0x5B)) & 1);
        state = reg & 63;
    }
    return out;
}
std::vector<int> symbols(const Bytes &b) {
    const int coded_bits = (int(b.size()) * 8 + 6) * 2;
    std::vector<int> out;
    for (int i = 0; i < preamble_bits; i++)
        out.push_back(i & 1);
    for (int i = 31; i >= 0; i--)
        out.push_back((sync_word >> i) & 1);
    auto code = fec(b);
    out.resize(preamble_bits + sync_bits + coded_bits);
    for (int i = 0; i < coded_bits; i++)
        out[preamble_bits + sync_bits + (i * permutation) % coded_bits] = code[i];
    return out;
}
Bytes viterbi(const std::vector<float> &soft, int payload_bytes) {
    std::vector<float> a(64, -1e30f), b(64);
    std::vector<uint8_t> trace((payload_bytes * 8 + 6) * 64);
    a[0] = 0;
    for (int t = 0; t < payload_bytes * 8 + 6; t++) {
        std::fill(b.begin(), b.end(), -1e30f);
        for (int s = 0; s < 64; s++)
            for (int bit = 0; bit < 2; bit++) {
                if (t >= payload_bytes * 8 && bit)
                    continue;
                int reg = (s << 1) | bit, ns = reg & 63;
                int x = std::popcount(unsigned(reg & 0x79)) & 1,
                    y = std::popcount(unsigned(reg & 0x5B)) & 1;
                float value = a[s] + (x ? 1 : -1) * soft[2 * t] + (y ? 1 : -1) * soft[2 * t + 1];
                if (value > b[ns]) {
                    b[ns] = value;
                    trace[t * 64 + ns] = uint8_t(s);
                }
            }
        a.swap(b);
    }
    Bytes out(payload_bytes);
    int state = 0;
    for (int t = payload_bytes * 8 + 5; t >= 0; t--) {
        if (t < payload_bytes * 8)
            out[t / 8] |= (state & 1) << (7 - t % 8);
        state = trace[t * 64 + state];
    }
    return out;
}
struct Demod {
    const std::vector<float> &x;
    double spb;
    std::array<std::array<double, 16>, 2> co{}, si{};
    Demod(const std::vector<float> &v, int baud) : x(v), spb(8000. / baud) {
        for (int tone = 0; tone < 2; tone++)
            for (int i = 0; i < 16; i++) {
                double phase = 2 * pi * (tone ? 2100 : 1300) * (i + 0.5) / 16 / baud;
                co[tone][i] = std::cos(phase);
                si[tone][i] = std::sin(phase);
            }
    }
    float sample(double at) const {
        if (at < 0 || at + 1 >= x.size())
            return 0;
        size_t i = size_t(at);
        return float(x[i] + (at - i) * (x[i + 1] - x[i]));
    }
    std::array<double, 2> energy(double start, int k, double scale) const {
        std::array<double, 2> re{}, im{};
        for (int i = 0; i < 16; i++) {
            double at = start + (k + (i + 0.5) / 16.) * spb * scale;
            float value = sample(at);
            for (int tone = 0; tone < 2; tone++) {
                re[tone] += value * co[tone][i];
                im[tone] += value * si[tone][i];
            }
        }
        return {re[0] * re[0] + im[0] * im[0], re[1] * re[1] + im[1] * im[1]};
    }
    float soft(double start, int k, double scale, double g0, double g1) const {
        auto e = energy(start, k, scale);
        double a = e[0] / g0, b = e[1] / g1;
        return float((b - a) / (a + b + 1e-15));
    }
};
struct Candidate {
    double start = 0, scale = 1, score = -1, g0 = 1, g1 = 1;
    int sync_errors = 32;
};
Candidate assess(const Demod &d, double start, double scale) {
    Candidate c;
    c.start = start;
    c.scale = scale;
    std::array<std::array<double, 2>, 64> e{};
    double gain[2] = {};
    for (int k = 0; k < 64; k++) {
        e[k] = d.energy(start, k, scale);
        if (k >= 8 && k < 32)
            gain[k & 1] += e[k][k & 1];
    }
    c.g0 = std::max(1e-12, gain[0] / 12);
    c.g1 = std::max(1e-12, gain[1] / 12);
    c.sync_errors = 0;
    c.score = 0;
    for (int k = 8; k < 64; k++) {
        int bit = k < 32 ? k & 1 : (sync_word >> (63 - k)) & 1;
        double a = e[k][0] / c.g0, b = e[k][1] / c.g1;
        double soft = (b - a) / (a + b + 1e-15);
        c.score += (bit ? 1 : -1) * soft;
        if (k >= 32 && int(soft >= 0) != bit)
            c.sync_errors++;
    }
    c.score /= 56;
    return c;
}
bool decode_candidate(const Demod &d, const Candidate &c, AudioTimecode &out, int payload_bytes) {
    const int coded_bits = (payload_bytes * 8 + 6) * 2;
    std::vector<float> soft(coded_bits), wire(coded_bits);
    for (int k = 0; k < coded_bits; k++)
        wire[k] = d.soft(c.start, k + 64, c.scale, c.g0, c.g1);
    for (int i = 0; i < coded_bits; i++)
        soft[i] = wire[(i * permutation) % coded_bits];
    auto bytes = viterbi(soft, payload_bytes);
    return parse_timecode(bytes, out);
}
} // namespace

uint32_t timecode_source_id(const char *text) {
    const std::string value(text ? text : "");
    return crc32(reinterpret_cast<const uint8_t *>(value.data()), value.size());
}

int64_t quantize_timecode_ns(int64_t ns) {
    if (ns < 0)
        throw std::invalid_argument("negative source time");
    const auto ticks = (static_cast<__int128>(ns) * 65536 + 500000000) / 1000000000;
    if (ticks >= (static_cast<__int128>(1) << 48))
        throw std::invalid_argument("source time exceeds 48-bit Q16 range");
    return int64_t((ticks * 1000000000 + 32768) / 65536);
}

std::vector<uint8_t> encode_timecode(uint32_t source, int64_t video_ns, int64_t offset_ns,
                                     bool live) {
    (void)quantize_timecode_ns(video_ns);
    const int64_t offset = std::llround(offset_ns / 62500.0);
    if (offset < -32768 || offset > 32767)
        throw std::invalid_argument("timecode offset exceeds +/-2048 ms");
    const bool zero = offset_ns == 0;
    Bytes b(zero ? 15 : 17);
    b[0] = uint8_t(0x30 | (live ? 1 : 0) | (zero ? 4 : 0));
    put(b, 1, source, 4);
    const auto ticks = (static_cast<__int128>(video_ns) * 65536 + 500000000) / 1000000000;
    put(b, 5, uint64_t(ticks), 6);
    if (!zero)
        put(b, 11, uint16_t(int16_t(offset)), 2);
    put(b, int(b.size()) - 4, crc32(b.data(), b.size() - 4), 4);
    return b;
}

bool parse_timecode(const std::vector<uint8_t> &b, AudioTimecode &out) {
    if (b.size() != 15 && b.size() != 17 && b.size() != 25)
        return false;
    if (get(b, int(b.size()) - 4, 4) != crc32(b.data(), b.size() - 4))
        return false;
    AudioTimecode p;
    p.bytes = b;
    p.source_id = uint32_t(get(b, 1, 4));
    if (b.size() == 25) {
        if (b[0] != 0x21 && b[0] != 0x23)
            return false;
        p.compact = false;
        p.live = true;
        p.onset = b[0] == 0x23;
        p.legacy_seq = uint32_t(get(b, 5, 4));
        const auto audio = get(b, 9, 8);
        if (audio > uint64_t(std::numeric_limits<int64_t>::max()) - 2147483648ULL)
            return false;
        p.audio_send_ns = int64_t(audio);
        p.video_send_ns = p.audio_send_ns - int32_t(get(b, 17, 4));
    } else {
        const bool zero = b.size() == 15;
        if (b[0] != uint8_t(0x30 | (b[0] & 1) | (zero ? 4 : 0)))
            return false;
        p.live = b[0] & 1;
        p.video_send_ns =
            int64_t((static_cast<__int128>(get(b, 5, 6)) * 1000000000 + 32768) / 65536);
        p.audio_send_ns = p.video_send_ns + (zero ? 0 : int64_t(int16_t(get(b, 11, 2))) * 62500);
        if (p.audio_send_ns < 0)
            return false;
    }
    out = std::move(p);
    return true;
}

double timecode_duration_ms(size_t bytes, int baud) {
    if ((bytes != 15 && bytes != 17 && bytes != 25) || (baud != 600 && baud != 1200))
        throw std::invalid_argument("unsupported timecode profile");
    return 40 + (64 + (bytes * 8 + 6) * 2) * 1000.0 / baud;
}

std::vector<float> timecode_waveform(const std::vector<uint8_t> &bytes, int baud, int rate,
                                     double level) {
    if ((bytes.size() != 15 && bytes.size() != 17 && bytes.size() != 25) ||
        (rate != 48000 && rate != 8000))
        throw std::invalid_argument("invalid timecode waveform");
    auto bits = symbols(bytes);
    std::vector<float> x(size_t(std::ceil(timecode_duration_ms(bytes.size(), baud) * rate / 1000)),
                         0);
    ChirpSpec chirp;
    chirp.sample_rate = rate;
    chirp.level_dbfs = level;
    const int n = rate * 30 / 1000, start = rate * 40 / 1000;
    for (int i = 0; i < n; ++i)
        x[i] = chirp_sample(chirp, 0, rate * 0.015, i);
    double phase = 0;
    const int length = int(x.size()) - start;
    for (int i = 0; i < length; ++i) {
        const size_t bit = std::min(bits.size() - 1, size_t(int64_t(i) * baud / rate));
        const double envelope = std::min({1.0, i / (0.003 * rate), (length - i) / (0.003 * rate)});
        x[start + i] = float(std::pow(10., level / 20) * envelope * std::sin(phase));
        phase += 2 * pi * (bits[bit] ? 2100 : 1300) / rate;
        if (phase > 2 * pi)
            phase -= 2 * pi;
    }
    return x;
}

std::vector<AudioTimecode> decode_timecodes(const float *audio, size_t count, int rate,
                                            double origin_ms) {
    if (!audio || count == 0 || rate < 8000 || !std::isfinite(origin_ms))
        return {};
    std::vector<float> x = rate == 8000 ? std::vector<float>(audio, audio + count)
                                        : resample_linear(audio, count, rate, 8000);
    avsync::AudioDetectConfig cfg;
    cfg.chirp.sample_rate = 8000;
    cfg.threshold = 0.45;
    cfg.period_ms = 250;
    avsync::AudioDetector detector(cfg);
    auto hits = detector.detect(x.data(), x.size(), 0);
    std::vector<AudioTimecode> result;
    for (const auto &hit : hits) {
        if (hit.chirp_id != 0)
            continue;
        bool found = false;
        for (int baud : {1200, 600}) {
            Demod d(x, baud);
            double nominal = hit.sample_pos + 200;
            std::vector<Candidate> best;
            for (int shift = -32; shift <= 32; shift++) {
                auto c = assess(d, nominal + shift, 1);
                if (c.sync_errors <= 6 && c.score > 0.18)
                    best.push_back(c);
            }
            std::sort(best.begin(), best.end(), [](auto &a, auto &b) { return a.score > b.score; });
            if (best.size() > 4)
                best.resize(4);
            if (!best.empty()) {
                auto initial = best.front();
                for (double ppm : {-1500., -1000., -500., 500., 1000., 1500.})
                    for (int shift = -2; shift <= 2; shift++) {
                        auto c = assess(d, initial.start + shift, 1 + ppm / 1e6);
                        if (c.sync_errors <= 6 && c.score > 0.18)
                            best.push_back(c);
                    }
                std::stable_sort(best.begin(), best.end(),
                                 [](auto &a, auto &b) { return a.score > b.score; });
            }
            for (const auto &c : best) {
                for (int payload_bytes : {15, 17, 25}) {
                    const int coded_bits = (payload_bytes * 8 + 6) * 2;
                    if (c.start + (64 + coded_bits) * d.spb * c.scale + 1 >= x.size())
                        continue;
                    AudioTimecode decoded;
                    if (!decode_candidate(d, c, decoded, payload_bytes))
                        continue;
                    decoded.baud = baud;
                    decoded.time_ms = hit.time_ms + origin_ms - (decoded.onset ? 15 * c.scale : 0);
                    decoded.arrival_ms = decoded.time_ms;
                    decoded.correlation = hit.correlation;
                    decoded.level_dbfs = hit.level_dbfs;
                    const auto same = [&](const AudioTimecode &old) {
                        return old.source_id == decoded.source_id &&
                               old.video_send_ns == decoded.video_send_ns &&
                               old.audio_send_ns == decoded.audio_send_ns;
                    };
                    if (std::none_of(result.begin(), result.end(), same))
                        result.push_back(decoded);
                    found = true;
                    break;
                }
                if (found)
                    break;
            }
            if (found)
                break;
        }
    }
    return result;
}

void AudioTimecodeWriter::write(const std::vector<PlannedEvent> &events, uint64_t start, float *dst,
                                size_t count, int baud, double level) {
    std::fill(dst, dst + count, 0.f);
    const int64_t begin = int64_t(start), end = begin + int64_t(count);
    const bool started = have_seq_;
    for (const auto &event : events) {
        if (!event.timecode || event.audio_sample >= end || (have_seq_ && event.seq <= last_seq_))
            continue;
        last_seq_ = event.seq;
        have_seq_ = true;
        if (event.audio_sample < last_end_ || event.audio_sample < begin) {
            if (started || event.audio_sample >= begin)
                ++skipped_;
            continue;
        }
        auto samples = timecode_waveform(event.audio_packet, baud, 48000, level);
        last_end_ = event.audio_sample + int64_t(samples.size());
        waves_.emplace(event.seq, Wave{event.audio_sample, std::move(samples)});
    }
    for (auto it = waves_.begin(); it != waves_.end();) {
        const auto &wave = it->second;
        const int64_t finish = wave.start + int64_t(wave.samples.size());
        for (int64_t at = std::max(begin, wave.start); at < std::min(end, finish); ++at)
            dst[at - begin] += wave.samples[size_t(at - wave.start)];
        if (finish <= end)
            it = waves_.erase(it);
        else
            ++it;
    }
}

void AudioTimecodeStream::reset() { *this = AudioTimecodeStream{}; }

void AudioTimecodeStream::push(const float *audio, size_t count, int rate, double start,
                               double arrival) {
    if (!audio || !count || rate < 8000 || !std::isfinite(start) || !std::isfinite(arrival))
        return;
    const double expected = origin_ms_ + audio_.size() * 1000.0 / std::max(1, rate_);
    if (rate_ != rate || std::abs(start - expected) > 1) {
        audio_.clear();
        arrivals_.clear();
        origin_ms_ = start;
        last_scan_ms_ = -1e30;
    }
    rate_ = rate;
    audio_.insert(audio_.end(), audio, audio + count);
    arrivals_.push_back({start, start + count * 1000.0 / rate, arrival});
    if (audio_.size() > size_t(rate) * 3) {
        const size_t drop = audio_.size() - size_t(rate) * 3;
        audio_.erase(audio_.begin(), audio_.begin() + drop);
        origin_ms_ += drop * 1000.0 / rate;
    }
    arrivals_.erase(std::remove_if(arrivals_.begin(), arrivals_.end(),
                                   [&](const Arrival &a) { return a.end < origin_ms_; }),
                    arrivals_.end());
}

const std::vector<AudioTimecode> &AudioTimecodeStream::scan(bool force) {
    const double end = origin_ms_ + audio_.size() * 1000.0 / std::max(1, rate_);
    if (!force && end - last_scan_ms_ < 100)
        return decoded_;
    last_scan_ms_ = end;
    for (auto packet : decode_timecodes(audio_.data(), audio_.size(), rate_, origin_ms_)) {
        for (const auto &clock : arrivals_)
            if (packet.time_ms >= clock.start - 0.2 && packet.time_ms < clock.end) {
                packet.arrival_ms = clock.arrival + packet.time_ms - clock.start;
                break;
            }
        const auto same = [&](const AudioTimecode &old) {
            return old.source_id == packet.source_id && old.video_send_ns == packet.video_send_ns &&
                   old.audio_send_ns == packet.audio_send_ns;
        };
        if (std::none_of(decoded_.begin(), decoded_.end(), same))
            decoded_.push_back(std::move(packet));
    }
    decoded_.erase(std::remove_if(decoded_.begin(), decoded_.end(),
                                  [&](const AudioTimecode &p) { return p.time_ms < end - 20000; }),
                   decoded_.end());
    return decoded_;
}
} // namespace avsync
