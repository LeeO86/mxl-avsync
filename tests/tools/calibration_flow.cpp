#include "avsync/calibrator.hpp"
#include "domain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

// Reads a generator's video and audio flows with AVSYNC_PATTERN=calibration for some seconds and
// checks the calibration pattern: the halves swap every 500 ms, each beep starts 100 ms after its
// swap, and a simulated calibrator (a click at each swap) gives a display calibration of 0 ms.
// Prints one JSON line; exit 0 when all checks pass.
int main(int argc, char** argv)
{
    using namespace avsync;
    if (argc < 4) {
        std::cerr << "usage: avsync-calibration-flow <domain-dir> <video-flow-id> <audio-flow-id> [seconds]\n";
        return 78;
    }
    const double seconds = argc > 4 ? std::stod(argv[4]) : 5;
    Domain domain;
    std::string error;
    FlowDesc desc;
    Rate pace{};
    int rows = 0, channels = 0;
    uint32_t grains = 0;
    size_t max_read = 0;
    if (!domain.open(argv[1], &error) || !domain.create_video_reader(argv[2], &desc, &pace, &rows, &grains, &error)
        || !domain.create_audio_reader(argv[3], &channels, &max_read, &error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const Rate audio{48000, 1};
    uint64_t index = domain.video_head();
    const uint64_t first = index, last = index + uint64_t(seconds * double(pace.num) / double(pace.den));
    const double origin = double(time_of_index(pace, first)) / 1e6;
    uint64_t sample = index_at(audio, time_of_index(pace, first));
    const uint64_t first_sample = sample;
    std::vector<float> mono, block;
    std::vector<double> swaps;
    int previous = -1, missing = 0;
    std::vector<uint8_t> payload;
    std::vector<uint16_t> luma;
    for (; index < last; ++index) {
        bool invalid = false;
        if (!domain.copy_grain(index, 500000000, rows, desc.width, grains, &payload, &invalid)) {
            ++missing;
            continue;
        }
        luma.assign(size_t(desc.width) * size_t(rows), 0);
        unpack_v210_luma(payload.data(), desc.width, rows, luma.data(), desc.width);
        const size_t middle = size_t(rows / 2) * size_t(desc.width);
        const int state = luma[middle + size_t(desc.width / 4)] > 500 ? 1 : 0;
        if ((luma[middle + size_t(3 * desc.width / 4)] > 500) == bool(state)) {
            std::cerr << "halves are not opposite at grain " << index << "\n";
            return 1;
        }
        if (previous >= 0 && state != previous) swaps.push_back(double(time_of_index(pace, index)) / 1e6 - origin);
        previous = state;
        // Audio up to this grain's time (the writer commits it ahead, like the video).
        const uint64_t until = index_at(audio, time_of_index(pace, index));
        if (until > sample) {
            const int frames = int(until - sample);
            if (!domain.copy_samples(until, frames, channels, 500000000, max_read, &block, &invalid)) {
                std::cerr << "audio not readable\n";
                return 1;
            }
            for (int i = 0; i < frames; ++i) mono.push_back(block[size_t(i * channels)]);
            sample = until;
        }
    }
    const double audio_origin = double(time_of_index(audio, first_sample)) / 1e6 - origin;
    const auto beeps = find_sound_onsets(mono.data(), mono.size(), 48000, audio_origin);
    // Each beep 100 ms after the swap before it.
    double worst = 0;
    size_t matched = 0;
    for (const auto& beep : beeps) {
        const auto swap = std::upper_bound(swaps.begin(), swaps.end(), beep.time_ms);
        if (swap == swaps.begin()) continue;
        worst = std::max(worst, std::abs(beep.time_ms - *(swap - 1) - CalibratorContract::sound_delay_ms));
        ++matched;
    }
    double period = 0;
    for (size_t i = 1; i < swaps.size(); ++i) period = std::max(period, std::abs(swaps[i] - swaps[i - 1] - CalibratorContract::swap_period_ms));
    // A calibrator with an instant screen: a click at every swap, through the same analysis as the phone.
    auto mic = mono;
    for (double swap : swaps) {
        const long at = std::lround((swap - audio_origin) * 48);
        for (long i = 0; i < 240 && at + i < long(mic.size()); ++i) {
            if (at + i < 0) continue;
            const double t = double(i) / 48000, on = 3 / CalibratorContract::click_hz;
            mic[size_t(at + i)] += float(.3 * (t < on ? 1 : std::exp(-(t - on) / 0.0006)) * std::sin(2 * 3.14159265358979 * CalibratorContract::click_hz * t));
        }
    }
    DisplayCalibrationOptions options;
    options.min_pairs = 4;
    const auto r = display_calibration(find_sound_onsets(mic.data(), mic.size(), 48000, audio_origin), options);
    // Grain timing: an interlaced field grid or 59.94 fps moves a swap by up to one grain.
    const double grain_ms = 1000.0 * double(pace.den) / double(pace.num);
    const bool ok = swaps.size() >= size_t(seconds * 2) - 2 && matched + 2 >= swaps.size() && worst < .15 && period <= grain_ms + .01 && r.ok
        && std::abs(r.offset_ms) < .15 && missing == 0;
    std::printf("{\"ok\":%s,\"swaps\":%zu,\"beeps\":%zu,\"matched\":%zu,\"worstBeepErrorMs\":%.3f,\"worstPeriodErrorMs\":%.3f,\"offsetMs\":%.3f,\"pairs\":%d,\"missingGrains\":%d}\n",
        ok ? "true" : "false", swaps.size(), beeps.size(), matched, worst, period, r.offset_ms, r.pairs, missing);
    return ok ? 0 : 1;
}
