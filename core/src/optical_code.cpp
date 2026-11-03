#include "avsync/optical_code.hpp"

#include <algorithm>
#include <cmath>

namespace avsync {

namespace {
// Transition chips. Runs of 1 and 2 chips; the 2-chip run moves with the parity,
// so a burst also tells even from odd events. Both variants: 4 rising + 4 falling.
constexpr std::array<std::array<int, kOpticalTransitions>, 2> kOffsets{{{0, 1, 2, 4, 5, 6, 7, 8}, {0, 1, 2, 3, 4, 6, 7, 8}}};

double median(std::vector<double> v)
{
    if (v.empty()) return 0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) m = (m + *std::max_element(v.begin(), v.begin() + mid)) / 2;
    return m;
}
} // namespace

int optical_chip_grains(double grain_rate, int event_rate) { return std::max(1, int(std::lround(grain_rate * (event_rate == 1 ? .080 : .040)))); }
const std::array<int, kOpticalTransitions>& optical_offsets(int variant) { return kOffsets[variant & 1]; }
int optical_span_chips() { return kOffsets[0].back(); }

bool optical_a_white(int64_t grain_in_period, int chip_grains, uint64_t seq)
{
    if (grain_in_period < 0 || chip_grains < 1) return false;
    const int64_t chip = grain_in_period / chip_grains;
    int passed = 0;
    for (int offset : optical_offsets(int(seq & 1))) passed += chip >= offset;
    return passed % 2 == 1;
}

OpticalCodeDetector::OpticalCodeDetector(double chip_ms, double period_ms) { configure(chip_ms, period_ms); }

void OpticalCodeDetector::configure(double chip_ms, double period_ms)
{
    chip_ms_ = chip_ms;
    period_ms_ = period_ms;
    reset();
}

void OpticalCodeDetector::reset()
{
    samples_.clear();
    crossings_.clear();
    ready_.clear();
    state_ = 0;
    pending_dir_ = 0;
    interval_ms_ = 0;
    diagnostics_ = {};
}

// Each field's black and white level over the last period (5th/95th percentile).
bool OpticalCodeDetector::range(double& amin, double& amax, double& bmin, double& bmax) const
{
    std::vector<double> a, b;
    const double from = samples_.back().start_ms - period_ms_;
    for (const auto& s : samples_)
        if (s.start_ms >= from) a.push_back(s.a), b.push_back(s.b);
    if (a.size() < 8) return false;
    auto pct = [](std::vector<double>& v, double q) {
        const size_t i = size_t(std::lround(q * double(v.size() - 1)));
        std::nth_element(v.begin(), v.begin() + i, v.end());
        return v[i];
    };
    amin = pct(a, .05), amax = pct(a, .95), bmin = pct(b, .05), bmax = pct(b, .95);
    const double top = std::max(amax, bmax);
    return top > 0 && amax - amin > .15 * top && bmax - bmin > .15 * top;
}

// A minus B, each scaled to its own black..white range: -1 rest, +1 swapped.
double OpticalCodeDetector::level(const Sample& s, double amin, double amax, double bmin, double bmax) const
{
    // Undo the camera's tone curve (about gamma 2.2 for BT.709 / sRGB video) so the
    // exposure mean is linear in light.
    auto lin = [&](double v, double lo, double hi) { return std::pow(std::clamp((v - lo) / (hi - lo), 0.0, 1.0), gamma_); };
    return lin(s.a, amin, amax) - lin(s.b, bmin, bmax);
}

void OpticalCodeDetector::push(const Sample& sample)
{
    ++diagnostics_.samples;
    if (!samples_.empty()) {
        const double step = sample.start_ms - samples_.back().start_ms;
        if (step <= 0) return;
        if (interval_ms_ <= 0) interval_ms_ = step;
        else if (step < 2.5 * interval_ms_) interval_ms_ += (step - interval_ms_) * .05;
    }
    samples_.push_back(sample);
    while (samples_.front().start_ms < sample.start_ms - 2 * period_ms_) samples_.pop_front();

    double amin, amax, bmin, bmax;
    if (!range(amin, amax, bmin, bmax)) {
        if (samples_.size() >= 8) ++diagnostics_.low_contrast;
        state_ = 0;
        close_burst(sample.start_ms);
        return;
    }
    const double d = level(sample, amin, amax, bmin, bmax);
    const int now = d > .3 ? 1 : d < -.3 ? -1 : state_;
    if (state_ != 0 && now != state_) {
        // A short run can flip back before its edge settled: time the earlier edge first.
        if (pending_dir_ != 0) crossing(amin, amax, bmin, bmax);
        pending_dir_ = now, pending_ms_ = sample.start_ms;
    }
    if (now != 0) state_ = now;
    // Time the crossing once the edge has settled on the new side (or half a chip passed).
    if (pending_dir_ != 0 && (d * pending_dir_ >= .85 || sample.start_ms - pending_ms_ > chip_ms_ / 2)) {
        crossing(amin, amax, bmin, bmax);
        pending_dir_ = 0;
    }
    close_burst(sample.start_ms);
}

void OpticalCodeDetector::crossing(double amin, double amax, double bmin, double bmax)
{
    const int dir = pending_dir_;
    std::vector<double> c, d;
    std::vector<double> e;
    for (const auto& s : samples_)
        if (s.start_ms >= pending_ms_ - chip_ms_ - interval_ms_) c.push_back(s.start_ms + s.exposure_ms / 2), d.push_back(level(s, amin, amax, bmin, bmax)), e.push_back(s.exposure_ms);
    // The sign change closest before the hysteresis flip.
    int z = -1;
    for (int i = 0; i + 1 < int(c.size()); ++i)
        if (d[i] * dir <= 0 && d[i + 1] * dir > 0 && c[i + 1] - e[i + 1] / 2 <= pending_ms_ + 1e-9) z = i;
    if (z < 0) { ++diagnostics_.gap_crossings; return; }
    if (c[z + 1] - c[z] > 2.5 * interval_ms_) { ++diagnostics_.gap_crossings; return; }
    // Samples on the edge: not settled, and only the monotonic part (a short run turns back).
    int lo = z + 1, hi = z + 1;
    while (lo > 0 && std::abs(d[lo - 1]) < .85 && d[lo - 1] * dir < d[lo] * dir) --lo;
    while (hi < int(c.size()) && std::abs(d[hi]) < .85 && (hi == lo || d[hi] * dir > d[hi - 1] * dir)) ++hi;
    double t;
    const int n = hi - lo;
    if (n >= 2) {
        // Slow edge: straight line through the edge samples.
        double sc = 0, sd = 0, scc = 0, scd = 0;
        for (int i = lo; i < hi; ++i) sc += c[i], sd += d[i], scc += c[i] * c[i], scd += c[i] * d[i];
        const double mc = sc / n, md = sd / n, var = scc / n - mc * mc, cov = scd / n - mc * md;
        if (var <= 0 || cov * dir <= 0) { ++diagnostics_.gap_crossings; return; }
        t = mc - md * var / cov;
    } else if (n == 1) {
        // Sharp edge inside one exposure: the mean is linear in where the edge fell.
        t = c[lo] - dir * d[lo] * e[lo] / 2;
    } else {
        // Sharp edge between two exposures.
        t = (c[z] + e[z] / 2 + c[z + 1] - e[z + 1] / 2) / 2;
    }
    // The zero crossing lies between the two samples around the sign change; a flat
    // fit can extrapolate far away, then fall back to interpolating those two.
    if (!(t >= c[z] && t <= c[z + 1])) t = c[z] + (c[z + 1] - c[z]) * (-d[z]) / (d[z + 1] - d[z]);
   
    crossings_.push_back({t, dir});
    ++diagnostics_.crossings;
}

void OpticalCodeDetector::close_burst(double now_ms)
{
    while (!crossings_.empty() && crossings_.front().time_ms < now_ms - 2 * period_ms_) crossings_.pop_front();
    if (crossings_.empty()) return;
    // A burst ends after a pause longer than its longest run plus display jitter.
    const double pause = 3.2 * chip_ms_;
    if (now_ms - crossings_.back().time_ms <= pause || state_ == 1) return;
    std::vector<Crossing> group(crossings_.begin(), crossings_.end());
    crossings_.clear();
    std::sort(group.begin(), group.end(), [](const Crossing& x, const Crossing& y) { return x.time_ms < y.time_ms; });

    const double tol = .4 * chip_ms_;
    struct Fit { int variant = 0, count = 0; double tau = 0; std::vector<double> residuals; std::vector<int> dirs; };
    Fit best;
    for (int v = 0; v < 2; ++v)
        for (const auto& anchor : group)
            for (int j = 0; j < kOpticalTransitions; ++j) {
                const int dir = j % 2 == 0 ? 1 : -1;
                if (anchor.direction != dir) continue;
                Fit fit;
                fit.variant = v;
                fit.tau = anchor.time_ms - optical_offsets(v)[j] * chip_ms_;
                for (int e = 0; e < kOpticalTransitions; ++e) {
                    const double expect = fit.tau + optical_offsets(v)[e] * chip_ms_;
                    const int edir = e % 2 == 0 ? 1 : -1;
                    double near = tol;
                    bool found = false;
                    for (const auto& c : group)
                        if (c.direction == edir && std::abs(c.time_ms - expect) <= near) near = std::abs(c.time_ms - expect), found = true;
                    if (!found) continue;
                    for (const auto& c : group)
                        if (c.direction == edir && std::abs(c.time_ms - expect) == near) {
                            fit.residuals.push_back(c.time_ms - optical_offsets(v)[e] * chip_ms_);
                            fit.dirs.push_back(edir);
                            break;
                        }
                }
                fit.count = int(fit.residuals.size());
                auto spread = [](const Fit& f) {
                    double m = median(f.residuals), s = 0;
                    for (double r : f.residuals) s += (r - m) * (r - m);
                    return s;
                };
                if (fit.count > best.count || (fit.count == best.count && fit.count > 0 && spread(fit) < spread(best))) best = std::move(fit);
            }
    int up = 0, down = 0;
    for (int d : best.dirs) (d > 0 ? up : down)++;
    if (best.count < 6 || up < 3 || down < 3) { ++diagnostics_.rejected; return; }

    // Drop only clear outliers (a crossing next to a frame gap); keep display cadence jitter.
    const double m = median(best.residuals);
    std::vector<double> dev;
    for (double r : best.residuals) dev.push_back(std::abs(r - m));
    const double limit = std::max(6.0, 3.5 * 1.4826 * median(dev));
    double sum = 0, sum2 = 0;
    int used = 0;
    for (double r : best.residuals)
        if (std::abs(r - m) <= limit) sum += r - m, sum2 += (r - m) * (r - m), ++used;
    if (used < 6) { ++diagnostics_.rejected; return; }
    Burst burst;
    burst.time_ms = m + sum / used;
    burst.variant = best.variant;
    burst.used = used;
    burst.rms_ms = std::sqrt(std::max(0.0, sum2 / used - (sum / used) * (sum / used)));
    double amin, amax, bmin, bmax;
    if (range(amin, amax, bmin, bmax)) burst.contrast = std::min(amax - amin, bmax - bmin) / std::max(amax, bmax);
    ready_.push_back(burst);
    ++diagnostics_.bursts;
}

std::vector<OpticalCodeDetector::Burst> OpticalCodeDetector::take()
{
    std::vector<Burst> out;
    out.swap(ready_);
    return out;
}

} // namespace avsync
