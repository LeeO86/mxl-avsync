#include "avsync/calibrator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avsync {
namespace {

constexpr double kPi = 3.14159265358979323846;

double median(std::vector<double> v)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

// Energy of one frequency over a stretch of samples (Goertzel).
double tone_energy(const std::vector<float>& x, size_t from, size_t to, double hz, int rate)
{
    const double k = 2 * std::cos(2 * kPi * hz / rate);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < to; ++i) {
        const double s = x[i] + k * s1 - s2;
        s2 = s1, s1 = s;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

// Median and robust outlier rejection: keep values within max(3 sigma, 0.5 ms) of the
// median (sigma from the median absolute deviation), then the median of the rest.
struct Summary {
    double median = 0, uncertainty = 0, spread = 0;
    std::vector<double> kept;
    int rejected = 0;
};
Summary summarize(const std::vector<double>& values)
{
    Summary s;
    if (values.empty()) return s;
    const double m = median(values);
    std::vector<double> dev;
    for (double v : values) dev.push_back(std::abs(v - m));
    const double limit = std::max(3 * 1.4826 * median(dev), 0.5);
    for (double v : values) (std::abs(v - m) <= limit ? s.kept.push_back(v) : void(++s.rejected));
    s.median = median(s.kept);
    const double n = double(s.kept.size());
    double var = 0;
    for (double v : s.kept) var += (v - s.median) * (v - s.median);
    const double sigma = n > 1 ? std::sqrt(var / (n - 1)) : 0;
    s.uncertainty = 1.2533 * sigma / std::sqrt(n); // standard error of a median
    auto sorted = s.kept;
    std::sort(sorted.begin(), sorted.end());
    auto at = [&](double q) { return sorted[size_t(std::lround(q * (n - 1)))]; };
    s.spread = sorted.size() < 10 ? sorted.back() - sorted.front() : at(.9) - at(.1);
    return s;
}

CalibratorResult fail(const std::string& error)
{
    CalibratorResult r;
    r.error = error;
    return r;
}

} // namespace

std::vector<SoundOnset> find_sound_onsets(const float* x, size_t n, int rate, double start_ms)
{
    std::vector<SoundOnset> out;
    if (!x || rate < 8000 || n < size_t(rate / 20)) return out;
    // High-pass at 300 Hz: handling noise and room rumble out, clicks and beeps in.
    std::vector<float> hp(n);
    const double rc = 1 / (2 * kPi * 300), a = rc / (rc + 1.0 / rate);
    for (size_t i = 1; i < n; ++i) hp[i] = float(a * (hp[i - 1] + x[i] - x[i - 1]));
    // Envelope: RMS over the last 0.25 ms (trailing, so it never rises before the sound).
    const size_t w = std::max<size_t>(4, size_t(rate / 4000));
    std::vector<double> sum(n + 1, 0);
    for (size_t i = 0; i < n; ++i) sum[i + 1] = sum[i] + double(hp[i]) * hp[i];
    std::vector<double> e(n);
    for (size_t i = 0; i < n; ++i) {
        const size_t lo = i + 1 >= w ? i + 1 - w : 0;
        e[i] = std::sqrt((sum[i + 1] - sum[lo]) / double(w));
    }
    // Noise floor: the median envelope (the sounds are short and sparse).
    std::vector<double> sample;
    for (size_t i = 0; i < n; i += 16) sample.push_back(e[i]);
    const double floor = std::max(median(sample), 1e-6);
    const double threshold = std::max(8 * floor, 3e-4);
    const size_t ms = size_t(rate / 1000);
    size_t i = 0;
    while (i < n) {
        if (e[i] < threshold) { ++i; continue; }
        // The peak within 10 ms, then back to where the envelope first reaches 25 % of it.
        size_t ip = i;
        for (size_t j = i; j < std::min(n, i + 10 * ms); ++j) if (e[j] > e[ip]) ip = j;
        const double peak = e[ip], level = .25 * peak;
        size_t k = ip;
        while (k > 0 && e[k] >= level) --k;
        const double frac = e[k + 1] > e[k] ? (level - e[k]) / (e[k + 1] - e[k]) : 0;
        const double onset = double(k) + std::clamp(frac, 0.0, 1.0);
        const size_t from = size_t(onset), to = std::min(n, from + 3 * ms);
        const double click = tone_energy(hp, from, to, CalibratorContract::click_hz, rate);
        const double beep = tone_energy(hp, from, to, CalibratorContract::beep_hz, rate);
        out.push_back({start_ms + onset * 1000.0 / rate, peak, click + beep > 0 ? click / (click + beep) : 0});
        // The sound ends when the envelope stays below max(threshold, 10 % of the peak) for 2 ms
        // (ringing and room echo then do not count as new sounds); at least 8 ms.
        const double quiet = std::max(threshold, .1 * peak);
        size_t j = std::max(ip, from + 8 * ms), calm = 0;
        while (j < n && calm < 2 * ms) calm = e[j++] < quiet ? calm + 1 : 0;
        i = j;
    }
    return out;
}

std::vector<FlashOnset> find_flash_onsets(const std::vector<FlashFrame>& frames, double led_ms, double* readout_out)
{
    std::vector<FlashOnset> out;
    if (readout_out) *readout_out = 0;
    if (frames.size() < 3 || frames[0].rows.empty()) return out;
    const size_t bands = frames[0].rows.size();
    for (const auto& f : frames) if (f.rows.size() != bands) return out;
    // Dark level of each band: its median (a flash lights only a few frames).
    std::vector<double> base(bands);
    for (size_t b = 0; b < bands; ++b) {
        std::vector<double> v;
        for (const auto& f : frames) v.push_back(f.rows[b]);
        base[b] = median(v);
    }
    auto rise = [&](const FlashFrame& f, size_t b) { return std::max(0.0, double(f.rows[b]) - base[b]); };
    auto lit = [&](const FlashFrame& f) {
        double top = 0;
        for (size_t b = 0; b < bands; ++b) top = std::max(top, rise(f, b));
        return top;
    };
    constexpr double kLit = 30; // 8-bit levels above dark
    // Runs of lit frames are flashes.
    struct Run { size_t first, last; double peak; };
    std::vector<Run> runs;
    for (size_t i = 0; i < frames.size(); ++i) {
        const double level = lit(frames[i]);
        if (level < kLit) continue;
        if (!runs.empty() && runs.back().last + 1 == i) runs.back().last = i, runs.back().peak = std::max(runs.back().peak, level);
        else runs.push_back({i, i, level});
    }
    // Share of the flash each band saw: undo the camera tone curve (gamma 2.2), 0..1.
    auto share = [&](const FlashFrame& f, size_t b, double peak) { return std::pow(std::clamp(rise(f, b) / peak, 0.0, 1.0), 2.2); };
    // Readout when no frame knows it: a band lit with both edges inside one frame is
    // max(exposure, LED) high at half level.
    double readout = 0;
    bool known = false;
    for (const auto& f : frames) known |= f.readout_ms > 0;
    if (!known) {
        std::vector<double> estimates;
        for (const auto& run : runs)
            for (size_t i = run.first; i <= run.last; ++i) {
                const auto& f = frames[i];
                if (f.exposure_ms <= 0 || bands < 8) continue;
                std::vector<double> s(bands);
                for (size_t b = 0; b < bands; ++b) s[b] = share(f, b, run.peak);
                if (s.front() >= .5 || s.back() >= .5) continue;
                size_t lo = 0, hi = bands - 1;
                while (lo < bands && s[lo] < .5) ++lo;
                while (hi > lo && s[hi] < .5) --hi;
                if (lo >= bands || lo == 0 || hi + 1 >= bands) continue;
                const double y1 = double(lo) - 1 + (.5 - s[lo - 1]) / std::max(1e-9, s[lo] - s[lo - 1]);
                const double y2 = double(hi) + (s[hi] - .5) / std::max(1e-9, s[hi] - s[hi + 1]);
                if (y2 - y1 >= .5) estimates.push_back(std::max(f.exposure_ms, led_ms) / (y2 - y1) * double(bands));
            }
        if (estimates.size() >= 3) readout = median(estimates);
    }
    if (readout_out) *readout_out = known ? frames[0].readout_ms : readout;
    for (const auto& run : runs) {
        const auto& f = frames[run.first];
        const double r = known ? f.readout_ms : readout;
        const double e = std::max(0.0, f.exposure_ms), m = std::min(e, led_ms);
        std::vector<double> s(bands);
        for (size_t b = 0; b < bands; ++b) s[b] = share(f, b, run.peak);
        // Row time of a band position y (in bands, 0 = top edge), as the analyzer times rows.
        auto row_time = [&](double y) { return f.time_ms + r * std::clamp(y / double(bands), 0.0, 1.0); };
        size_t b = 0;
        while (b < bands && s[b] < .5) ++b;
        FlashOnset onset;
        onset.row_timed = r > 0;
        if (b < bands && b > 0) {
            // Rolling-shutter edge: the half-level row's exposure ended half a flash after the start.
            const double y = double(b) - .5 + (.5 - s[b - 1]) / std::max(1e-9, s[b] - s[b - 1]);
            onset.time_ms = row_time(y) + e - m / 2;
            onset.edge_in_frame = true;
            onset.row = std::clamp(y / double(bands), 0.0, 1.0);
        } else {
            // No edge inside the frame: the share of the flash inside the exposure.
            if (b >= bands) b = size_t(std::max_element(s.begin(), s.end()) - s.begin());
            onset.time_ms = row_time(double(b) + .5) + e - s[b] * m;
            onset.row = (double(b) + .5) / double(bands);
        }
        out.push_back(onset);
    }
    return out;
}

CalibratorResult camera_calibration(const std::vector<FlashOnset>& flashes, const std::vector<SoundOnset>& sounds, const CameraCalibrationOptions& o)
{
    if (flashes.empty()) return fail("No flashes seen. Lay the calibrator LED-down on the main rear camera.");
    if (sounds.empty()) return fail("No clicks heard. Check that the calibrator is plugged in.");
    // Rough offset: the most common click - flash difference (2 ms bins, counted over +/-3 ms).
    std::vector<double> diffs;
    for (const auto& f : flashes)
        for (const auto& s : sounds) {
            const double d = s.time_ms - f.time_ms;
            if (d > -250 && d < 350) diffs.push_back(d);
        }
    if (diffs.empty()) return fail("Flashes and clicks do not match. Try again.");
    double rough = 0;
    size_t best = 0;
    for (double centre = -250; centre < 350; centre += 2) {
        const size_t count = size_t(std::count_if(diffs.begin(), diffs.end(), [&](double d) { return std::abs(d - centre) <= 3; }));
        if (count > best) best = count, rough = centre;
    }
    // Pair each flash with the nearest unused click within 12 ms of the rough offset.
    std::vector<bool> used(sounds.size(), false);
    std::vector<double> values, rows;
    std::vector<bool> edges;
    bool timed = false;
    const double fixed = o.buzzer_delay_ms + o.mic_distance_cm * CalibratorContract::sound_ms_per_cm;
    for (const auto& f : flashes) {
        long pick = -1;
        double near = 12;
        for (size_t i = 0; i < sounds.size(); ++i) {
            const double d = std::abs(sounds[i].time_ms - f.time_ms - rough);
            if (!used[i] && d <= near) near = d, pick = long(i);
        }
        if (pick < 0) continue;
        used[size_t(pick)] = true;
        values.push_back(sounds[size_t(pick)].time_ms - f.time_ms - fixed);
        rows.push_back(f.row), edges.push_back(f.edge_in_frame), timed |= f.row_timed;
    }
    if (int(values.size()) < o.min_pairs) return fail("Too few flashes with a click (" + std::to_string(values.size()) + "). Keep the calibrator still on the lens.");
    double readout = 0;
    if (!timed) {
        // Fit value = offset + readout * row over the edge pulses (twice: the second time
        // without the outliers of the first), when their rows spread over at least 30 %.
        std::vector<size_t> use;
        for (size_t i = 0; i < values.size(); ++i) if (edges[i]) use.push_back(i);
        for (int pass = 0; pass < 2 && use.size() >= size_t(o.min_pairs); ++pass) {
            double sr = 0, sv = 0, srr = 0, srv = 0, lo = 1, hi = 0;
            for (size_t i : use) sr += rows[i], sv += values[i], srr += rows[i] * rows[i], srv += rows[i] * values[i], lo = std::min(lo, rows[i]), hi = std::max(hi, rows[i]);
            const double n = double(use.size()), var = srr / n - (sr / n) * (sr / n);
            if (hi - lo < .3 || var <= 0) break;
            const double slope = (srv / n - (sr / n) * (sv / n)) / var;
            if (!(slope >= 0 && slope <= 50)) break;
            readout = slope;
            std::vector<double> residual;
            for (size_t i : use) residual.push_back(values[i] - slope * rows[i]);
            const auto fit = summarize(residual);
            std::vector<size_t> keep;
            for (size_t k = 0; k < use.size(); ++k)
                if (std::find(fit.kept.begin(), fit.kept.end(), residual[k]) != fit.kept.end()) keep.push_back(use[k]);
            use = keep;
        }
        for (size_t i = 0; i < values.size(); ++i) values[i] -= readout * rows[i];
    }
    const auto s = summarize(values);
    CalibratorResult r;
    r.ok = true;
    r.readout_ms = readout;
    r.offset_ms = s.median, r.uncertainty_ms = s.uncertainty, r.spread_ms = s.spread;
    r.pairs = int(s.kept.size()), r.rejected = s.rejected, r.values = s.kept;
    return r;
}

CalibratorResult display_calibration(const std::vector<SoundOnset>& sounds, const DisplayCalibrationOptions& o)
{
    size_t clicks = 0;
    for (const auto& s : sounds) clicks += s.click();
    if (!clicks) return fail("No clicks heard. Lay the calibrator on the marked spot and check the cable.");
    if (clicks == sounds.size()) return fail("No test sound heard. Turn the volume up.");
    // Each click with the next beep (not a click) less than one swap period later.
    struct Pair { double click, value; };
    std::vector<Pair> pairs;
    const double fixed = o.buzzer_delay_ms + o.click_minus_speaker_path_cm * CalibratorContract::sound_ms_per_cm;
    for (size_t i = 0; i < sounds.size(); ++i) {
        if (!sounds[i].click()) continue;
        for (size_t j = i + 1; j < sounds.size(); ++j) {
            const double gap = sounds[j].time_ms - sounds[i].time_ms;
            if (gap >= CalibratorContract::swap_period_ms - 50 || sounds[j].click()) break;
            pairs.push_back({sounds[i].time_ms, gap - o.sound_delay_ms + fixed});
            break;
        }
    }
    if (int(pairs.size()) < o.min_pairs) return fail("Too few clicks with a test sound (" + std::to_string(pairs.size()) + "). Check the placement and the volume.");
    std::vector<double> all, even, odd;
    for (const auto& p : pairs) {
        all.push_back(p.value);
        // Swap direction: every other swap, counted from the first click.
        const long k = std::lround((p.click - pairs[0].click) / CalibratorContract::swap_period_ms);
        (k % 2 ? odd : even).push_back(p.value);
    }
    const auto s = summarize(all);
    CalibratorResult r;
    r.ok = true;
    r.offset_ms = s.median, r.uncertainty_ms = s.uncertainty, r.spread_ms = s.spread;
    r.pairs = int(s.kept.size()), r.rejected = s.rejected, r.values = s.kept;
    if (even.size() >= 3 && odd.size() >= 3) {
        // A sensor mismatch moves one direction early and the other late: use both equally.
        const auto a = summarize(even), b = summarize(odd);
        r.offset_ms = (a.median + b.median) / 2;
        r.uncertainty_ms = std::hypot(a.uncertainty, b.uncertainty) / 2;
        r.direction_ms = a.median - b.median;
    }
    return r;
}

uint64_t calibration_period(double ms) { return ms <= 0 ? 0 : uint64_t(std::floor(ms / CalibratorContract::swap_period_ms + 1e-9)); }

bool calibration_left_white(double ms) { return calibration_period(ms) % 2 == 0; }

std::vector<float> calibration_beep(int rate)
{
    // A sine that starts at full slope (sharp onset), fades out over its last cycle.
    const size_t n = size_t(std::lround(CalibratorContract::beep_ms * rate / 1000));
    const double cycle = rate / CalibratorContract::beep_hz;
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double left = double(n - i);
        const double fade = left < cycle ? .5 - .5 * std::cos(kPi * left / cycle) : 1;
        out[i] = float(.5 * fade * std::sin(2 * kPi * CalibratorContract::beep_hz * double(i) / rate));
    }
    return out;
}

} // namespace avsync
