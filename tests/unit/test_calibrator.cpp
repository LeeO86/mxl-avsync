#include "doctest.h"

#include "avsync/calibrator.hpp"

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

using namespace avsync;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;

struct Noise {
    uint32_t state = 12345;
    double uniform() { state = state * 1664525u + 1013904223u; return (state >> 8) / 16777216.0; }
    double gauss() { return std::sqrt(-2 * std::log(uniform() + 1e-12)) * std::cos(2 * kPi * uniform()); }
};

// The buzzer: 3 cycles of 2.7 kHz (coil on at the start), then a fast decay.
void add_click(std::vector<float>& audio, double at_ms, double level = .3)
{
    const size_t start = size_t(std::lround(at_ms * kRate / 1000));
    for (size_t i = 0; i < size_t(kRate / 200) && start + i < audio.size(); ++i) {
        const double t = double(i) / kRate, on = 3 / CalibratorContract::click_hz;
        const double envelope = t < on ? 1 : std::exp(-(t - on) / 0.0006);
        audio[start + i] += float(level * envelope * std::sin(2 * kPi * CalibratorContract::click_hz * t));
    }
}

void add_beep(std::vector<float>& audio, double at_ms, double level = .6)
{
    const auto beep = calibration_beep(kRate);
    const size_t start = size_t(std::lround(at_ms * kRate / 1000));
    for (size_t i = 0; i < beep.size() && start + i < audio.size(); ++i) audio[start + i] += float(level * beep[i]);
}

std::vector<float> noise_track(double seconds, double rms, Noise& noise)
{
    std::vector<float> audio(size_t(seconds * kRate));
    for (auto& v : audio) v = float(rms * noise.gauss());
    return audio;
}

// The camera train: 3 start pulses, 20 timing pulses every 500 ms, on the board's own
// clock (1.3 % fast here; the contract allows about 2 %), so the flashes drift against the frames.
std::vector<double> train(double start_ms)
{
    std::vector<double> t{0, 125, 250};
    for (int k = 0; k < 20; ++k) t.push_back(1000 + 500 * k);
    for (auto& v : t) v = start_ms + v / 1.013;
    return t;
}

// Rolling-shutter frames: row y (0..1) exposes [t + readout * y, + exposure]; the LED adds light
// in proportion to the overlap with each flash, through a gamma 1/2.2 tone curve.
std::vector<FlashFrame> camera(const std::vector<double>& flashes, double fps, double exposure, double readout, double report_readout,
    Noise& noise, double seconds = 13, const std::set<size_t>& hidden = {})
{
    std::vector<FlashFrame> frames;
    const int bands = 32, sub = 8;
    const double led = CalibratorContract::led_pulse_ms, full = std::min(exposure, led);
    for (double t = 3.1; t < seconds * 1000; t += 1000 / fps) {
        FlashFrame f{t, exposure, report_readout, std::vector<float>(bands)};
        for (int b = 0; b < bands; ++b) {
            double sum = 0;
            for (int s = 0; s < sub; ++s) {
                const double start = t + readout * (b * sub + s + .5) / (bands * sub);
                double light = 0;
                for (size_t i = 0; i < flashes.size(); ++i) {
                    if (hidden.count(i)) continue;
                    light += std::max(0.0, std::min(start + exposure, flashes[i] + led) - std::max(start, flashes[i]));
                }
                sum += 16 + 239 * std::pow(std::min(1.0, light / full), 1 / 2.2);
            }
            f.rows[size_t(b)] = float(sum / sub + 2 * noise.gauss());
        }
        frames.push_back(f);
    }
    return frames;
}

} // namespace

TEST_CASE("calibrator sounds: clicks and beeps are found and told apart")
{
    Noise noise;
    auto audio = noise_track(1, .002, noise);
    add_click(audio, 200);
    add_beep(audio, 300);
    add_click(audio, 700);
    const auto onsets = find_sound_onsets(audio.data(), audio.size(), kRate, 1000);
    REQUIRE(onsets.size() == 3);
    CHECK(onsets[0].click());
    CHECK(!onsets[1].click());
    CHECK(onsets[2].click());
    CHECK(std::abs(onsets[0].time_ms - 1200) < .15);
    CHECK(std::abs(onsets[1].time_ms - 1300) < .15);
    CHECK(onsets[2].time_ms - onsets[0].time_ms == doctest::Approx(500).epsilon(1e-4));
    CHECK(find_sound_onsets(audio.data(), 100, kRate, 0).empty());
}

TEST_CASE("calibration pattern: halves swap every 500 ms, the beep starts at once")
{
    CHECK(calibration_left_white(0));
    CHECK(calibration_left_white(499.9));
    CHECK(!calibration_left_white(500));
    CHECK(calibration_left_white(1000));
    CHECK(calibration_period(1250) == 2);
    const auto beep = calibration_beep(kRate);
    REQUIRE(beep.size() == 192);
    CHECK(beep[0] == 0);
    CHECK(std::abs(beep[1]) > .05);
    float peak = 0;
    for (float v : beep) peak = std::max(peak, std::abs(v));
    CHECK(peak <= .5f);
    CHECK(std::abs(beep.back()) < .01);
}

TEST_CASE("camera mode: flash start on the rolling-shutter row against the click")
{
    Noise noise;
    const auto flashes = train(400);
    const double truth = 12.3, travel = 1.5 * CalibratorContract::sound_ms_per_cm;
    auto audio = noise_track(13, .003, noise);
    for (double t : flashes) add_click(audio, t + truth + travel);
    const auto sounds = find_sound_onsets(audio.data(), audio.size(), kRate, 0);
    REQUIRE(sounds.size() == flashes.size());

    SUBCASE("readout known")
    {
        const auto frames = camera(flashes, 240, 1.0, 3.0, 3.0, noise);
        double readout = 0;
        const auto onsets = find_flash_onsets(frames, CalibratorContract::led_pulse_ms, &readout);
        CHECK(readout == 3.0);
        const auto r = camera_calibration(onsets, sounds);
        REQUIRE(r.ok);
        CHECK(r.pairs >= 20);
        CHECK(std::abs(r.offset_ms - truth) < .25);
        CHECK(r.uncertainty_ms < .3);
    }
    SUBCASE("readout unknown: estimated from the lit band")
    {
        const auto frames = camera(flashes, 240, 0.5, 3.0, 0, noise);
        double readout = 0;
        const auto onsets = find_flash_onsets(frames, CalibratorContract::led_pulse_ms, &readout);
        CHECK(std::abs(readout - 3.0) < .5);
        const auto r = camera_calibration(onsets, sounds);
        REQUIRE(r.ok);
        CHECK(std::abs(r.offset_ms - truth) < .4);
    }
    SUBCASE("full-frame exposure, readout unknown: fitted over the pulses")
    {
        // The analyzer locks exposure to the frame interval; iOS gives no readout time.
        const auto frames = camera(flashes, 240, 1000 / 240.0, 3.5, 0, noise);
        double readout = -1;
        const auto onsets = find_flash_onsets(frames, CalibratorContract::led_pulse_ms, &readout);
        CHECK(readout == 0);
        const auto r = camera_calibration(onsets, sounds);
        REQUIRE(r.ok);
        CHECK(std::abs(r.readout_ms - 3.5) < .5);
        CHECK(std::abs(r.offset_ms - truth) < .4);
    }
    SUBCASE("long exposure at 60 fps")
    {
        const auto frames = camera(flashes, 60, 4.0, 14.0, 14.0, noise);
        const auto r = camera_calibration(find_flash_onsets(frames), sounds);
        REQUIRE(r.ok);
        CHECK(std::abs(r.offset_ms - truth) < .5);
    }
}

TEST_CASE("camera mode: missing pulses, a stray sound and nothing at all")
{
    Noise noise;
    const auto flashes = train(250);
    auto audio = noise_track(13, .01, noise);
    for (size_t i = 0; i < flashes.size(); ++i)
        if (i % 6 != 2) add_click(audio, flashes[i] + 30);
    add_click(audio, 5100, .5); // a tap on the table
    const auto frames = camera(flashes, 120, 1.0, 6.0, 6.0, noise, 13, {4, 9, 15});
    const auto onsets = find_flash_onsets(frames);
    CHECK(onsets.size() == flashes.size() - 3);
    const auto r = camera_calibration(onsets, find_sound_onsets(audio.data(), audio.size(), kRate, 0));
    REQUIRE(r.ok);
    CHECK(r.pairs >= 14);
    CHECK(std::abs(r.offset_ms - (30 - 1.5 * CalibratorContract::sound_ms_per_cm)) < .3);

    CHECK(!camera_calibration({}, {}).ok);
    CHECK(!camera_calibration(onsets, {}).ok);
    const std::vector<FlashOnset> few(onsets.begin(), onsets.begin() + 3);
    const auto short_take = camera_calibration(few, find_sound_onsets(audio.data(), audio.size(), kRate, 0));
    CHECK(!short_take.ok);
    CHECK(short_take.error.find("Too few") == 0);
}

TEST_CASE("display mode: click then beep, both swap directions, slow screen")
{
    Noise noise;
    const double screen = 45, speaker = 7; // light after the planned swap; sound after its planned time
    auto audio = noise_track(13, .003, noise);
    std::vector<double> swaps;
    for (int k = 0; k < 24; ++k) swaps.push_back(150 + 500 * k);
    for (size_t k = 0; k < swaps.size(); ++k) {
        // A sensor mismatch: one direction crosses 1.5 ms early, the other 1.5 ms late.
        const double mismatch = k % 2 ? -1.5 : 1.5;
        if (k != 5 && k != 6 && k != 13) add_click(audio, swaps[k] + screen + mismatch + .2 * noise.gauss());
        add_beep(audio, swaps[k] + CalibratorContract::sound_delay_ms + speaker);
    }
    const auto sounds = find_sound_onsets(audio.data(), audio.size(), kRate, 0);
    const auto r = display_calibration(sounds);
    REQUIRE(r.ok);
    CHECK(r.pairs == 21);
    CHECK(std::abs(r.offset_ms - (speaker - screen)) < .3);
    CHECK(std::abs(std::abs(r.direction_ms) - 3) < .5);
    CHECK(r.uncertainty_ms < .5);

    // The board 4 cm nearer the microphone than the speaker; 0.3 ms buzzer delay.
    DisplayCalibrationOptions o;
    o.buzzer_delay_ms = .3;
    o.click_minus_speaker_path_cm = -4;
    CHECK(std::abs(display_calibration(sounds, o).offset_ms - (r.offset_ms + .3 - 4 * CalibratorContract::sound_ms_per_cm)) < 1e-9);
}

TEST_CASE("display mode: an external screen with picture later than sound, and failures")
{
    Noise noise;
    auto audio = noise_track(8, .003, noise);
    for (int k = 0; k < 14; ++k) {
        add_click(audio, 300 + 500 * k + 120);           // a TV: picture 120 ms late
        add_beep(audio, 300 + 500 * k + 100 + 80, .4);   // its sound 80 ms late
    }
    const auto r = display_calibration(find_sound_onsets(audio.data(), audio.size(), kRate, 0));
    REQUIRE(r.ok);
    CHECK(std::abs(r.offset_ms - (80 - 120)) < .3);

    auto only_beeps = noise_track(4, .003, noise);
    for (int k = 0; k < 8; ++k) add_beep(only_beeps, 200 + 500 * k);
    const auto none = display_calibration(find_sound_onsets(only_beeps.data(), only_beeps.size(), kRate, 0));
    CHECK(!none.ok);
    CHECK(none.error.find("No clicks") == 0);

    auto only_clicks = noise_track(4, .003, noise);
    for (int k = 0; k < 8; ++k) add_click(only_clicks, 200 + 500 * k);
    CHECK(display_calibration(find_sound_onsets(only_clicks.data(), only_clicks.size(), kRate, 0)).error.find("No test sound") == 0);
}
