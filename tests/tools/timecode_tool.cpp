// Exercise the production codec with independent FFmpeg channel transforms.
#include "avsync/avsync.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

using namespace avsync;
using nlohmann::json;

void write_wav(const std::string &path, const std::vector<float> &samples) {
    std::ofstream f(path, std::ios::binary);
    auto word = [&](uint32_t v, int n) {
        for (int i = 0; i < n; ++i)
            f.put(char(v >> (i * 8)));
    };
    f.write("RIFF", 4);
    word(36 + samples.size() * 2, 4);
    f.write("WAVEfmt ", 8);
    word(16, 4);
    word(1, 2);
    word(1, 2);
    word(48000, 4);
    word(96000, 4);
    word(2, 2);
    word(16, 2);
    f.write("data", 4);
    word(samples.size() * 2, 4);
    for (auto x : samples)
        word(uint16_t(int16_t(std::lround(std::clamp(x, -1.f, 1.f) * 32767))), 2);
    if (!f)
        throw std::runtime_error("cannot write WAV");
}

std::vector<float> read_wav(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    char tag[4];
    auto word = [&](int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v |= uint32_t(uint8_t(f.get())) << (i * 8);
        return v;
    };
    f.read(tag, 4);
    if (std::string(tag, 4) != "RIFF")
        throw std::runtime_error("not WAV");
    word(4);
    f.read(tag, 4);
    if (std::string(tag, 4) != "WAVE")
        throw std::runtime_error("not WAVE");
    bool valid = false;
    std::vector<float> out;
    while (f.read(tag, 4)) {
        const auto length = word(4);
        if (std::string(tag, 4) == "fmt ") {
            if (length < 16)
                throw std::runtime_error("bad WAV fmt");
            const auto type = word(2), channels = word(2), rate = word(4);
            word(4);
            word(2);
            const auto bits = word(2);
            valid = type == 1 && channels == 1 && rate == 8000 && bits == 16;
            f.seekg(length - 16, std::ios::cur);
        } else if (std::string(tag, 4) == "data") {
            if (!valid || length % 2)
                throw std::runtime_error("requires mono PCM16/8k");
            out.resize(length / 2);
            for (auto &x : out)
                x = int16_t(word(2)) / 32768.f;
        } else
            f.seekg(length, std::ios::cur);
        if (length & 1)
            f.seekg(1, std::ios::cur);
    }
    return out;
}

int main(int argc, char **argv) {
    try {
        if (argc < 2)
            return 78;
        std::map<std::string, std::string> args;
        for (int i = 2; i + 1 < argc; i += 2)
            args[argv[i]] = argv[i + 1];
        auto get = [&](const char *key, const char *def) {
            return args.contains(key) ? args.at(key) : std::string(def);
        };
        const std::string command = argv[1];
        if (command == "dump-vector") {
            for (auto b : encode_timecode(0x01020304, 1792963237123456789LL, 35000000, true))
                std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(b);
            std::cout << '\n';
            return 0;
        }
        if (command == "self-test") {
            AudioTimecode p;
            if (!parse_timecode(encode_timecode(123, 1700000037000000000LL, 35000000, true), p))
                return 1;
            return 0;
        }
        if (command == "generate") {
            const int baud = std::stoi(get("--baud", "1200")),
                      count = std::stoi(get("--packets", "32"));
            const auto rate = get("--event-rate", baud == 1200 ? "2" : "1");
            if (rate != "1" && rate != "2")
                throw std::invalid_argument("rate must be 1 or 2");
            const int hz = std::stoi(rate), seed = std::stoi(get("--seed", "20261027"));
            if (count < 1 || count > 10000 || (baud == 600 && hz == 2))
                throw std::invalid_argument("invalid profile");
            std::vector<float> x(size_t((count * 1000.0 / hz + 400) * 48), 0);
            json manifest = {{"baud", baud},
                             {"period_ms", 1000.0 / hz},
                             {"events_per_second", hz},
                             {"anchor_reference", "start"},
                             {"packet_duration_ms", timecode_duration_ms(17, baud)},
                             {"payload_bytes", "15/17"},
                             {"events", json::array()}};
            for (int i = 0; i < count; ++i) {
                const double start = 100 + i * 1000.0 / hz;
                const uint32_t source = 0xA5500000u | (seed & 0xffffu);
                const int64_t offset =
                    std::array<int64_t, 5>{-350000000, 0, 35000000, 100000000, 1400000000}[i % 5];
                const auto bytes =
                    encode_timecode(source,
                                    1792963237000000000LL + int64_t(seed % 1000000) * 1000000LL +
                                        int64_t(std::llround(start * 1e6)) - offset,
                                    offset, true);
                AudioTimecode p;
                parse_timecode(bytes, p);
                auto transmitted = bytes;
                if (get("--bad-crc", "0") == "1")
                    transmitted.back() ^= 1;
                auto wave = timecode_waveform(transmitted, baud);
                if (get("--legacy", "0") == "1") {
                    wave.resize(1440);
                    for (size_t j = 0; j < wave.size(); ++j)
                        wave[j] = chirp_sample(ChirpSpec{}, i % 4, 720, j);
                }
                std::copy(wave.begin(), wave.end(), x.begin() + size_t(std::llround(start * 48)));
                // seq is only a benchmark label; it is not on the compact wire.
                manifest["events"].push_back(
                    {{"source", source},
                     {"seq", i},
                     {"send_tai_ns", p.audio_send_ns},
                     {"video_send_ns", p.video_send_ns},
                     {"video_offset_ns", p.audio_send_ns - p.video_send_ns},
                     {"anchor_ms", start},
                     {"chirp_start_ms", start},
                     {"payload_bytes", bytes.size()},
                     {"packet_duration_ms", timecode_duration_ms(bytes.size(), baud)}});
            }
            write_wav(get("--output", "/tmp/timecode.wav"), x);
            std::ofstream(get("--manifest", "/tmp/manifest.json")) << manifest.dump(2) << '\n';
            return 0;
        }
        if (command == "analyze") {
            const auto x = read_wav(args.at("--input"));
            json output = {{"packets", json::array()},
                           {"crc_rejects", nullptr},
                           {"prefix_candidates", nullptr}};
            for (const auto &p : decode_timecodes(x.data(), x.size(), 8000))
                output["packets"].push_back({{"source", p.source_id},
                                             {"video_send_ns", p.video_send_ns},
                                             {"send_tai_ns", p.audio_send_ns},
                                             {"video_offset_ns", p.audio_send_ns - p.video_send_ns},
                                             {"anchor_reference", p.onset ? "start" : "centre"},
                                             {"anchor_ms", p.time_ms},
                                             {"baud", p.baud},
                                             {"hard_changes", nullptr}});
            std::ofstream(args.at("--output")) << output.dump(2) << '\n';
            return 0;
        }
        return 78;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 78;
    }
}
