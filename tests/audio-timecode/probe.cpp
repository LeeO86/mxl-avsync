// Experimental audio-only timecode modem. Not used by the production generator.
#include "avsync/avsync.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace modem {
constexpr double pi = 3.14159265358979323846;
constexpr int payload_bytes = 25, coded_bits = (payload_bytes * 8 + 6) * 2, preamble_bits = 32,
              sync_bits = 32;
constexpr uint32_t sync_word = 0x1ACFFC1D;
constexpr int permutation = 73;
using Bytes = std::array<uint8_t, payload_bytes>;
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
Bytes packet(uint32_t source, uint32_t seq, uint64_t tai, int32_t offset, bool bad_crc = false,
             bool start_reference = true) {
    Bytes b{};
    // Bit 1 explicitly distinguishes onset from the historical centre reference.
    b[0] = start_reference ? 0x23 : 0x21;
    put(b, 1, source, 4);
    put(b, 5, seq, 4);
    put(b, 9, tai, 8);
    put(b, 17, uint32_t(offset), 4);
    put(b, 21, crc32(b.data(), 21) ^ (bad_crc ? 1u : 0u), 4);
    return b;
}
std::vector<int> fec(const Bytes &b) {
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
Bytes viterbi(const std::vector<float> &soft) {
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
    Bytes out{};
    int state = 0;
    for (int t = payload_bytes * 8 + 5; t >= 0; t--) {
        if (t < payload_bytes * 8)
            out[t / 8] |= (state & 1) << (7 - t % 8);
        state = trace[t * 64 + state];
    }
    return out;
}
void u16(std::ofstream &f, uint16_t v) {
    char b[2] = {char(v), char(v >> 8)};
    f.write(b, 2);
}
void u32(std::ofstream &f, uint32_t v) {
    u16(f, v);
    u16(f, v >> 16);
}
void wav_write(const std::string &path, const std::vector<float> &x, int rate) {
    std::ofstream f(path, std::ios::binary);
    f.write("RIFF", 4);
    u32(f, 36 + x.size() * 2);
    f.write("WAVEfmt ", 8);
    u32(f, 16);
    u16(f, 1);
    u16(f, 1);
    u32(f, rate);
    u32(f, rate * 2);
    u16(f, 2);
    u16(f, 16);
    f.write("data", 4);
    u32(f, x.size() * 2);
    for (float v : x)
        u16(f, uint16_t(int16_t(std::lround(std::clamp(v, -1.f, 1.f) * 32767))));
    if (!f)
        throw std::runtime_error("WAV write failed");
}
uint16_t read16(std::ifstream &f) {
    unsigned char b[2];
    f.read(reinterpret_cast<char *>(b), 2);
    return b[0] | (uint16_t(b[1]) << 8);
}
uint32_t read32(std::ifstream &f) {
    uint32_t lo = read16(f);
    return lo | (uint32_t(read16(f)) << 16);
}
std::vector<float> wav_read(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    char id[4];
    f.read(id, 4);
    if (std::string(id, 4) != "RIFF")
        throw std::runtime_error("Not RIFF WAV");
    read32(f);
    f.read(id, 4);
    if (std::string(id, 4) != "WAVE")
        throw std::runtime_error("Not WAVE");
    bool fmt = false;
    std::vector<float> x;
    while (f.read(id, 4)) {
        uint32_t n = read32(f);
        if (std::string(id, 4) == "fmt ") {
            if (n < 16)
                throw std::runtime_error("Invalid fmt");
            int format = read16(f), channels = read16(f), rate = read32(f);
            read32(f);
            read16(f);
            int bits = read16(f);
            if (format != 1 || channels != 1 || rate != 8000 || bits != 16)
                throw std::runtime_error("Decoder requires mono 8-kHz PCM16 WAV");
            fmt = true;
            f.seekg(n - 16, std::ios::cur);
        } else if (std::string(id, 4) == "data") {
            if (!fmt || n % 2)
                throw std::runtime_error("Invalid PCM data");
            x.resize(n / 2);
            for (float &v : x)
                v = int16_t(read16(f)) / 32768.f;
        } else
            f.seekg(n, std::ios::cur);
        if (n & 1)
            f.seekg(1, std::ios::cur);
    }
    return x;
}
void generate(const std::map<std::string, std::string> &args) {
    auto val = [&](const char *k, const char *d) {
        auto i = args.find(k);
        return i == args.end() ? std::string(d) : i->second;
    };
    int baud = std::stoi(val("--baud", "1200")), count = std::stoi(val("--packets", "32")),
        rate = 48000;
    uint32_t seed = std::stoul(val("--seed", "20261026"));
    const auto event_rate_arg = val("--event-rate", baud == 1200 ? "2" : "1");
    if (event_rate_arg != "1" && event_rate_arg != "2")
        throw std::runtime_error("--event-rate must be 1 or 2");
    int event_rate = std::stoi(event_rate_arg);
    const auto period_arg = val("--period-ms", event_rate == 2 ? "500" : "1000");
    size_t period_end = 0;
    double period = std::stod(period_arg, &period_end);
    if (period_end != period_arg.size())
        throw std::runtime_error("Invalid --period-ms");
    const auto reference = val("--anchor", "start");
    bool start_reference = reference == "start";
    if (reference != "start" && reference != "centre")
        throw std::runtime_error("--anchor must be start or centre");
    if ((event_rate != 1 && event_rate != 2) || !std::isfinite(period) ||
        (period != 500 && period != 1000) ||
        (args.contains("--event-rate") && period != 1000.0 / event_rate))
        throw std::runtime_error(
            "Choose --event-rate 1 or 2; --period-ms must agree (1000 or 500)");
    event_rate = period == 500 ? 2 : 1;
    bool bad = val("--bad-crc", "0") == "1", legacy = val("--legacy", "0") == "1";
    if ((baud != 600 && baud != 1200) || count < 1 || count > 10000)
        throw std::runtime_error("Unsupported generator configuration");
    double length = 40 + (preamble_bits + sync_bits + coded_bits) * 1000.0 / baud;
    if (!legacy && period < length + 10)
        throw std::runtime_error("Packet does not fit event period");
    std::vector<float> x(size_t((count * period + 400) * rate / 1000), 0);
    std::ofstream manifest(val("--manifest", "/tmp/manifest.json"));
    manifest << "{\"baud\":" << baud << ",\"period_ms\":" << period
             << ",\"events_per_second\":" << event_rate << ",\"anchor_reference\":\"" << reference
             << "\",\"packet_duration_ms\":" << std::setprecision(12) << length << ",\"events\":[";
    avsync::ChirpSpec chirp;
    chirp.sample_rate = rate;
    for (int i = 0; i < count; i++) {
        double start = (100 + i * period) * rate / 1000, centre = start + 0.015 * rate;
        double anchor = start_reference ? start : centre;
        uint64_t tai = 1792963237000000000ull + uint64_t(seed % 1000000) * 1000000ull +
                       uint64_t(std::llround(anchor * 1e9 / rate));
        uint32_t seq = 0xFFFFF0u + uint32_t(i), source = 0xA5500000u | (seed & 0xFFFFu);
        int32_t offset =
            std::array<int32_t, 5>{-350000000, 0, 35000000, 100000000, 1400000000}[i % 5];
        for (int j = 0; j < 1440; j++)
            x[size_t(start + j)] =
                avsync::chirp_sample(chirp, legacy ? i % 4 : 0, centre, start + j);
        if (!legacy) {
            auto bits = symbols(packet(source, seq, tai, offset, bad, start_reference));
            double phase = 0;
            int spb = rate / baud;
            int at = int(start) + 1920;
            for (size_t k = 0; k < bits.size(); k++)
                for (int j = 0; j < spb; j++) {
                    int index = int(k) * spb + j;
                    double t = index / double(rate),
                           remaining = (bits.size() * spb - index) / double(rate);
                    double envelope = std::min({1.0, t / 0.003, remaining / 0.003});
                    x[at + index] = float(std::pow(10., -12. / 20) * envelope * std::sin(phase));
                    phase += 2 * pi * (bits[k] ? 2100 : 1300) / rate;
                    if (phase > 2 * pi)
                        phase -= 2 * pi;
                }
        }
        if (i)
            manifest << ',';
        manifest << "{\"source\":" << source << ",\"seq\":" << seq << ",\"send_tai_ns\":" << tai
                 << ",\"video_offset_ns\":" << offset << ",\"anchor_ms\":" << anchor * 1000 / rate
                 << ",\"chirp_start_ms\":" << start * 1000 / rate
                 << ",\"chirp_centre_ms\":" << centre * 1000 / rate << '}';
    }
    manifest << "]}\n";
    wav_write(val("--output", "/tmp/timecode.wav"), x, rate);
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
struct Decoded {
    Bytes b{};
    int baud = 0, hard_changes = 0;
    double time = 0, correlation = 0, score = 0, rate_ppm = 0, start_shift_ms = 0;
};
bool decode_candidate(const Demod &d, const Candidate &c, Decoded &out) {
    std::vector<float> soft(coded_bits), wire(coded_bits);
    for (int k = 0; k < coded_bits; k++)
        wire[k] = d.soft(c.start, k + 64, c.scale, c.g0, c.g1);
    for (int i = 0; i < coded_bits; i++)
        soft[i] = wire[(i * permutation) % coded_bits];
    out.b = viterbi(soft);
    if ((out.b[0] != 0x21 && out.b[0] != 0x23) || get(out.b, 21, 4) != crc32(out.b.data(), 21))
        return false;
    auto expected = fec(out.b);
    for (int i = 0; i < coded_bits; i++)
        out.hard_changes += int(soft[i] >= 0) != expected[i];
    return true;
}
void analyze(const std::map<std::string, std::string> &args) {
    auto input = args.at("--input");
    auto x = wav_read(input);
    avsync::AudioDetectConfig cfg;
    cfg.chirp.sample_rate = 8000;
    cfg.threshold = 0.45;
    cfg.period_ms = 250;
    avsync::AudioDetector detector(cfg);
    auto hits = detector.detect(x.data(), x.size(), 0);
    std::vector<Decoded> result;
    int prefix_hits = 0, crc_failures = 0, sync_candidates = 0;
    for (const auto &hit : hits) {
        if (hit.chirp_id != 0)
            continue;
        prefix_hits++;
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
                sync_candidates++;
                if (c.start + (64 + coded_bits) * d.spb * c.scale + 1 >= x.size())
                    continue;
                Decoded decoded;
                decoded.baud = baud;
                decoded.time = hit.time_ms;
                decoded.correlation = hit.correlation;
                decoded.score = c.score;
                decoded.rate_ppm = (c.scale - 1) * 1e6;
                decoded.start_shift_ms = (c.start - nominal) / 8;
                if (decode_candidate(d, c, decoded)) {
                    // Keep the matched filter's centre internally. Only a CRC-validated
                    // onset-reference packet changes the externally reported arrival.
                    if (decoded.b[0] == 0x23)
                        decoded.time -= cfg.chirp.duration_ms * 0.5 * c.scale;
                    result.push_back(decoded);
                    found = true;
                    break;
                }
                crc_failures++;
            }
            if (found)
                break;
        }
    }
    std::sort(result.begin(), result.end(), [](auto &a, auto &b) { return a.time < b.time; });
    std::set<std::array<uint64_t, 3>> seen;
    int duplicates = 0;
    std::ostringstream out;
    out << std::setprecision(12) << "{\"input_samples\":" << x.size()
        << ",\"prefix_candidates\":" << prefix_hits << ",\"sync_candidates\":" << sync_candidates
        << ",\"crc_rejects\":" << crc_failures << ",\"packets\":[";
    bool first = true;
    for (auto &r : result) {
        auto key = std::array<uint64_t, 3>{get(r.b, 1, 4), get(r.b, 5, 4), get(r.b, 9, 8)};
        if (!seen.insert(key).second) {
            duplicates++;
            continue;
        }
        if (!first)
            out << ',';
        first = false;
        out << "{\"source\":" << key[0] << ",\"seq\":" << key[1] << ",\"send_tai_ns\":" << key[2]
            << ",\"video_offset_ns\":" << int32_t(get(r.b, 17, 4)) << ",\"baud\":" << r.baud
            << ",\"anchor_reference\":\"" << (r.b[0] == 0x23 ? "start" : "centre")
            << "\",\"anchor_ms\":" << r.time << ",\"correlation\":" << r.correlation
            << ",\"sync_score\":" << r.score << ",\"hard_changes\":" << r.hard_changes
            << ",\"rate_search_ppm\":" << r.rate_ppm
            << ",\"fsk_start_shift_ms\":" << r.start_shift_ms << '}';
    }
    out << "],\"duplicates\":" << duplicates << "}\n";
    auto dest = args.find("--output");
    if (dest == args.end())
        std::cout << out.str();
    else {
        std::ofstream f(dest->second);
        f << out.str();
    }
}
void self_test() {
    const auto *p = reinterpret_cast<const uint8_t *>("123456789");
    if (crc32(p, 9) != 0xCBF43926u)
        throw std::runtime_error("CRC known vector failed");
    auto b = packet(123, 0x1000001, 1700000037000000000ull, -35000000);
    auto code = fec(b);
    std::vector<float> s;
    for (int bit : code)
        s.push_back(bit ? 1.f : -1.f);
    if (viterbi(s) != b)
        throw std::runtime_error("FEC roundtrip failed");
    for (int i = 20; i < 24; i++)
        s[i] *= -1;
    if (viterbi(s) != b)
        throw std::runtime_error("FEC correction failed");
    if (std::gcd(permutation, coded_bits) != 1)
        throw std::runtime_error("Interleave permutation invalid");
    std::cout
        << "PASS: CRC known vector, exact 64-bit fields, FEC roundtrip/correction, interleaver.\n";
}
} // namespace modem
int main(int argc, char **argv) {
    try {
        if (argc < 2)
            return 78;
        std::map<std::string, std::string> args;
        for (int i = 2; i + 1 < argc; i += 2)
            args[argv[i]] = argv[i + 1];
        if (std::string(argv[1]) == "dump-vector") {
            auto b =
                modem::packet(0x01020304u, 0x10203040u, 1792963237123456789ull, 35000000, false,
                              !args.contains("--anchor") || args.at("--anchor") == "start");
            for (auto v : b)
                std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(v);
            std::cout << '\n';
        } else if (std::string(argv[1]) == "generate")
            modem::generate(args);
        else if (std::string(argv[1]) == "analyze")
            modem::analyze(args);
        else if (std::string(argv[1]) == "self-test")
            modem::self_test();
        else
            return 78;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
