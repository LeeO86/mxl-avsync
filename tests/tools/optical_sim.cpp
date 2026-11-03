// Physical simulation of the optical marker path: source -> transport ->
// display (refresh, pixel response, local dimming) -> camera (rate, exposure,
// auto gain, clipping, noise, drops). Compares the AVS2 single flash with the
// 70 % threshold against the AVS3 complementary burst code.
#include "avsync/optical_code.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace avsync;

struct Scenario {
    std::string name;
    double source_fps = 50, refresh_hz = 50;
    double rise_ms = 2, fall_ms = 2;  // pixel response time constants
    double backlight_ms = 0;          // local-dimming zone time constant (0 = none)
    double transport_jitter_ms = 0;   // arrival jitter before the display (WebRTC)
    double transport_drop = 0;        // probability a source frame never shows
    double camera_fps = 240, exposure_ms = 4.1667;
    double gain_walk = 0;             // auto exposure: per-frame log-gain random walk
    double over = 1.0;                // white maps to this before clipping at 1
    double noise = .01;
    double camera_drop = 0;
    int rate = 2;
    bool low_yield = false; // known to miss bursts (30 fps camera needs 1 event/s)
    double tone = 2.2;      // camera tone curve exponent; 0 = BT.709 OETF
    double pts_jitter_ms = 0;
};

struct Stats { double bias = 0, sd = 0, p5p95 = 0, win_p5p95 = 0; int events = 0, expected = 0; };

static double pct(std::vector<double> v, double q)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[size_t(std::lround(q * double(v.size() - 1)))];
}

static Stats summarize(const std::vector<double>& err, int expected)
{
    Stats s;
    s.events = int(err.size());
    s.expected = expected;
    if (err.empty()) return s;
    for (double e : err) s.bias += e;
    s.bias /= double(err.size());
    for (double e : err) s.sd += (e - s.bias) * (e - s.bias);
    s.sd = std::sqrt(s.sd / double(err.size()));
    s.p5p95 = pct(err, .95) - pct(err, .05);
    std::vector<double> win;
    for (size_t i = 3; i < err.size(); ++i) win.push_back((err[i] + err[i - 1] + err[i - 2] + err[i - 3]) / 4);
    s.win_p5p95 = pct(win, .95) - pct(win, .05);
    return s;
}

// Run one scenario; avs3 selects the signal. Returns per-event error vs the source event time.
static Stats run(const Scenario& sc, bool avs3, uint32_t seed, double seconds = 60)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uni(0, 1);
    std::normal_distribution<double> gauss(0, 1);
    const double rate = sc.rate, frame = 1000 / sc.source_fps;
    const int chip_grains = optical_chip_grains(sc.source_fps, sc.rate);
    const int period_grains = int(std::lround(1000 / rate / frame));
    const double period = period_grains * frame;
    const double chip_ms = chip_grains * frame;
    const double latency = 30 + 20 * uni(rng); // fixed transport latency for this run
    // Free-running clocks: the display and camera are never locked to the source.
    const double refresh = 1000 / (sc.refresh_hz * (1 + 40e-6)), refresh_phase = refresh * uni(rng);
    const double dt = .05;
    const int steps = int(seconds * 1000 / dt);

    // Which source frame is on the panel at each refresh.
    const int frames = int(seconds * sc.source_fps) + 2;
    std::vector<double> arrival(frames);
    for (int n = 0; n < frames; ++n) arrival[n] = uni(rng) < sc.transport_drop ? 1e30 : n * frame + latency + sc.transport_jitter_ms * uni(rng);
    auto source_a = [&](int n) { // target of field A (AVS3) or the flash (AVS2), 0..1
        const int64_t seq = n / period_grains + 1, pos = n % period_grains;
        if (avs3) return optical_a_white(pos, chip_grains, uint64_t(seq)) ? 1.0 : 0.0;
        return pos * frame < 100 ? 1.0 : 0.0;
    };
    std::vector<double> shown_at(frames, 1e30); // when each source frame (or a later one) first reached the panel
    // Physical onset of event k: mean over its transitions of (panel time - source offset).
    auto truth = [&](int64_t k) {
        const int64_t first = k * period_grains;
        if (!avs3) return shown_at[size_t(first)] - first * frame;
        double sum = 0;
        for (int o : optical_offsets(int((k + 1) & 1))) { const int64_t n = first + int64_t(o) * chip_grains; sum += shown_at[size_t(n)] - n * frame; }
        return sum / kOpticalTransitions;
    };
    // Simulate light at dt resolution.
    std::vector<float> la(steps), lb(steps);
    double pa = 0, pb = 1, bl = avs3 ? .5 : 0; // pixel states and zone backlight
    int shown = -1;
    double next_refresh = refresh_phase;
    double ta = 0, tb = 1;
    for (int i = 0; i < steps; ++i) {
        const double t = i * dt;
        if (t >= next_refresh) {
            next_refresh += refresh;
            int best = shown;
            for (int n = std::max(0, shown); n < frames && n * frame <= t; ++n)
                if (arrival[n] <= t) best = n;
            if (best >= 0 && best != shown) for (int n = std::max(0, shown + 1); n <= best; ++n) shown_at[n] = next_refresh - refresh;
            if (best >= 0) { shown = best; ta = source_a(shown); tb = avs3 ? 1 - ta : 0; }
        }
        pa += (ta - pa) * (1 - std::exp(-dt / (ta > pa ? sc.rise_ms : sc.fall_ms)));
        pb += (tb - pb) * (1 - std::exp(-dt / (tb > pb ? sc.rise_ms : sc.fall_ms)));
        // Local dimming: the zone backlight follows the zone's mean picture level.
        const double zone = avs3 ? (ta + tb) / 2 : ta;
        if (sc.backlight_ms > 0) bl += (std::max(.05, zone) - bl) * (1 - std::exp(-dt / sc.backlight_ms));
        const double light = sc.backlight_ms > 0 ? bl / std::max(.05, avs3 ? .5 : 1.0) : 1;
        la[i] = float(pa * std::min(1.0, light));
        lb[i] = float(pb * std::min(1.0, light));
    }

    // Camera.
    std::vector<double> err;
    OpticalCodeDetector det(chip_ms, period);
    double gain = 0;
    int blacks = 0;
    bool armed = false;
    const double cam = 1000 / (sc.camera_fps * .99785), cam_phase = cam * uni(rng);
    for (double t0 = cam_phase + 200; t0 + sc.exposure_ms < seconds * 1000 - 1; t0 += cam) {
        gain += sc.gain_walk * gauss(rng);
        gain = std::clamp(gain, -.5, .5);
        if (uni(rng) < sc.camera_drop) continue;
        const int i0 = int(t0 / dt), i1 = std::max(i0 + 1, int((t0 + sc.exposure_ms) / dt));
        double a = 0, b = 0;
        for (int i = i0; i < i1; ++i) a += la[i], b += lb[i];
        a /= (i1 - i0), b /= (i1 - i0);
        auto sense = [&](double v) {
            v = v * sc.over * std::exp(gain) + sc.noise * gauss(rng);
            v = std::clamp(v, 0.0, 1.0);
            if (sc.tone == 0) return v < .018 ? 4.5 * v : 1.099 * std::pow(v, .45) - .099; // BT.709
            return std::pow(v, 1 / sc.tone); // camera tone curve
        };
        const double ca = sense(a), cb = sense(b), white = sense(1.0);
        if (avs3) {
            det.push({t0 + sc.pts_jitter_ms * gauss(rng), sc.exposure_ms, ca, cb});
            for (const auto& burst : det.take()) {
                const double k = std::round((burst.time_ms - latency) / period);
                if (k >= 0 && (k + 1) * period_grains < frames) err.push_back(burst.time_ms - k * period - truth(int64_t(k)));
            }
        } else {
            // AVS2: normalized by the always-white finder (lit zone), first frame above 70 %.
            const double level = ca / white;
            if (level < .3) { ++blacks; armed = blacks >= 2; continue; }
            if (level <= .7) continue; // ramp frame: keep waiting, as AVS2 does
            if (armed) {
                const double k = std::round((t0 - latency) / period);
                if (k >= 0 && (k + 1) * period_grains < frames) err.push_back(t0 - k * period - truth(int64_t(k)));
            }
            blacks = 0;
            armed = false;
        }
    }
    // Remove the run's mean (only spread matters) after matching events to the nearest period.
    return summarize(err, int(seconds * rate) - 1);
}

int main(int argc, char** argv)
{
    const std::string only = argc > 1 ? argv[1] : "";
    std::vector<Scenario> scenarios;
    auto add = [&](std::string name, auto edit) { Scenario s; s.name = std::move(name); edit(s); scenarios.push_back(s); };
    add("broadcast OLED 50 Hz", [](Scenario& s) { s.rise_ms = .2; s.fall_ms = .2; });
    add("broadcast LCD 50 Hz", [](Scenario& s) { s.rise_ms = 3; s.fall_ms = 1.5; });
    add("slow LCD 60 Hz, 50p", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 8; s.fall_ms = 3; });
    add("WebRTC LCD 60 Hz", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.transport_jitter_ms = 8; s.transport_drop = .01; });
    add("mini-LED 120 Hz", [](Scenario& s) { s.refresh_hz = 120; s.rise_ms = 12; s.fall_ms = 6; s.backlight_ms = 15; });
    add("LCD + auto exposure", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.gain_walk = .03; s.over = 1.6; });
    add("camera 120 fps", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.camera_fps = 120; s.exposure_ms = 8.33; });
    add("camera 60 fps", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.camera_fps = 60; s.exposure_ms = 16.6; });
    add("camera 30 fps", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.camera_fps = 30; s.exposure_ms = 33.2; s.low_yield = true; });
    add("camera 30 fps, 1/s", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.camera_fps = 30; s.exposure_ms = 33.2; s.rate = 1; });
    add("WebRTC LCD 60 Hz, 1/s", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.transport_jitter_ms = 8; s.transport_drop = .01; s.rate = 1; });
    add("camera 60 fps short exp", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.camera_fps = 60; s.exposure_ms = 2; });
    add("noisy, drops", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.noise = .04; s.camera_drop = .03; });
    add("tone 1.6", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.tone = 1.6; });
    add("tone 3.0", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.tone = 3.0; });
    add("tone BT.709", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.tone = 0; });
    add("PTS jitter 0.3 ms", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; s.pts_jitter_ms = .3; });
    add("bad WebRTC 60 Hz", [](Scenario& s) { s.refresh_hz = 60; s.rise_ms = 8; s.fall_ms = 4; s.transport_jitter_ms = 15; s.transport_drop = .03; s.camera_fps = 120; s.exposure_ms = 8.3; });
    add("25p on 60 Hz", [](Scenario& s) { s.source_fps = 25; s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; });
    add("59.94p on 60 Hz", [](Scenario& s) { s.source_fps = 59.94; s.refresh_hz = 60; s.rise_ms = 6; s.fall_ms = 3; });

    std::printf("%-26s | %-36s | %-36s\n", "scenario", "AVS2 70% flash: n sd p5-95 win", "AVS3 burst: n sd p5-95 win(4)");
    int failures = 0;
    for (const auto& sc : scenarios) {
        if (!only.empty() && sc.name.find(only) == std::string::npos) continue;
        Stats o, n;
        double worst = 0, worst_win = 0;
        int got = 0, want = 0;
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            o = run(sc, false, seed);
            n = run(sc, true, seed);
            worst = std::max(worst, n.p5p95);
            worst_win = std::max(worst_win, n.win_p5p95);
            got += n.events, want += n.expected;
        }
        std::printf("%-26s | %4d/%-4d %5.2f %6.2f %6.2f     | %4d/%-4d %5.2f %6.2f %6.2f  (worst of 5: %.2f / %.2f)\n", sc.name.c_str(),
            o.events, o.expected, o.sd, o.p5p95, o.win_p5p95, n.events, n.expected, n.sd, n.p5p95, n.win_p5p95, worst, worst_win);
        if (!sc.low_yield && (got < want * 9 / 10 || worst_win > 3.5)) ++failures;
    }
    return failures ? 1 : 0;
}
