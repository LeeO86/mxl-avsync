#include "avsync/avsync.hpp"

#include <complex>
#include <cmath>
#include <algorithm>
#include <numeric>

#define POCKETFFT_NO_MULTITHREADING 1
#include "pocketfft_hdronly.h"

namespace avsync {
namespace {

int next_pow2(int n)
{
    int p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

std::vector<double> correlate(const float* x, int nx, const float* ref, int m)
{
    const int nfft = next_pow2(std::max(nx, m + 8));
    std::vector<double> in(static_cast<size_t>(nfft), 0.0);
    std::vector<double> rin(static_cast<size_t>(nfft), 0.0);
    for (int i = 0; i < nx; ++i) {
        in[static_cast<size_t>(i)] = x[i];
    }
    for (int i = 0; i < m; ++i) {
        rin[static_cast<size_t>(i)] = ref[i];
    }
    const int nfreq = nfft / 2 + 1;
    std::vector<std::complex<double>> X(static_cast<size_t>(nfreq));
    std::vector<std::complex<double>> R(static_cast<size_t>(nfreq));
    const pocketfft::shape_t shape{static_cast<size_t>(nfft)};
    const pocketfft::stride_t stride_in{static_cast<ptrdiff_t>(sizeof(double))};
    const pocketfft::stride_t stride_out{static_cast<ptrdiff_t>(sizeof(std::complex<double>))};
    pocketfft::r2c(shape, stride_in, stride_out, 0, true, in.data(), X.data(), 1.0);
    pocketfft::r2c(shape, stride_in, stride_out, 0, true, rin.data(), R.data(), 1.0);
    for (int i = 0; i < nfreq; ++i) {
        X[static_cast<size_t>(i)] *= std::conj(R[static_cast<size_t>(i)]);
    }
    std::vector<double> time(static_cast<size_t>(nfft), 0.0);
    pocketfft::c2r(shape, stride_out, stride_in, 0, false, X.data(), time.data(), 1.0 / static_cast<double>(nfft));
    const int lags = std::max(0, nx - m + 1);
    time.resize(static_cast<size_t>(lags));
    return time;
}

} // namespace

AudioDetector::AudioDetector(AudioDetectConfig config)
    : config_(std::move(config))
{
    length_ = chirp_length(config_.chirp);
    const double centre = (length_ - 1) * 0.5;
    for (int id = 0; id < 4; ++id) {
        refs_[id].resize(static_cast<size_t>(length_));
        double sum = 0;
        for (int i = 0; i < length_; ++i) {
            refs_[id][static_cast<size_t>(i)] = chirp_sample(config_.chirp, id, centre, static_cast<double>(i));
            sum += static_cast<double>(refs_[id][static_cast<size_t>(i)]) * refs_[id][static_cast<size_t>(i)];
        }
        ref_norm_[id] = std::sqrt(std::max(sum, 1e-18));
    }
}

std::vector<AudioHit> AudioDetector::detect(const float* samples, size_t count, uint64_t origin_sample) const
{
    std::vector<AudioHit> hits;
    if (samples == nullptr || count < static_cast<size_t>(length_) || length_ <= 8) {
        return hits;
    }
    const int nx = static_cast<int>(count);
    const int m = length_;
    std::vector<double> prefix(count + 1, 0.0);
    for (size_t i = 0; i < count; ++i) {
        prefix[i + 1] = prefix[i] + static_cast<double>(samples[i]) * samples[i];
    }
    const double energy_floor = 1e-10 * static_cast<double>(m);
    const int hop_in = 8192;
    const double rate = static_cast<double>(config_.chirp.sample_rate);

    struct Peak {
        double lag = 0;
        int id = 0;
        double ncc = 0;
        double level = -120;
        bool clipped = false;
    };
    std::vector<Peak> peaks;
    std::vector<std::vector<double>> ncc(4);

    for (int id = 0; id < 4; ++id) {
        ncc[id].assign(static_cast<size_t>(std::max(0, nx - m + 1)), 0.0);
        for (int start = 0; start + m <= nx;) {
            const int block = std::min(hop_in, nx - start);
            if (block < m) {
                break;
            }
            const auto corr = correlate(samples + start, block, refs_[id].data(), m);
            for (size_t k = 0; k < corr.size(); ++k) {
                const int lag = start + static_cast<int>(k);
                const double energy = prefix[static_cast<size_t>(lag + m)] - prefix[static_cast<size_t>(lag)];
                double value = 0;
                if (energy > energy_floor) {
                    value = corr[k] / (std::sqrt(energy) * ref_norm_[id]);
                }
                if (lag >= 0 && lag < static_cast<int>(ncc[id].size())) {
                    ncc[id][static_cast<size_t>(lag)] = value;
                }
            }
            const int advance = std::max(1, block - m + 1);
            start += advance;
        }
        const int nlag = static_cast<int>(ncc[id].size());
        const int guard = std::max(4, m / 8);
        for (int i = 2; i + 2 < nlag; ++i) {
            const double v = ncc[id][static_cast<size_t>(i)];
            if (v < config_.threshold) {
                continue;
            }
            if (!(v >= ncc[id][static_cast<size_t>(i - 1)] && v > ncc[id][static_cast<size_t>(i + 1)] &&
                    v >= ncc[id][static_cast<size_t>(i - 2)] && v >= ncc[id][static_cast<size_t>(i + 2)])) {
                continue;
            }
            bool local = true;
            double sidelobe = 0;
            const int lo = std::max(0, i - m);
            const int hi = std::min(nlag - 1, i + m);
            for (int j = lo; j <= hi; ++j) {
                if (std::abs(j - i) <= guard) {
                    continue;
                }
                sidelobe = std::max(sidelobe, ncc[id][static_cast<size_t>(j)]);
                if (ncc[id][static_cast<size_t>(j)] > v) {
                    local = false;
                    break;
                }
            }
            if (!local) {
                continue;
            }
            if (sidelobe > 0 && v / sidelobe < config_.sidelobe_ratio && sidelobe > config_.threshold * 0.85) {
                continue;
            }
            const double y0 = ncc[id][static_cast<size_t>(i - 1)];
            const double y1 = v;
            const double y2 = ncc[id][static_cast<size_t>(i + 1)];
            const double denom = (y0 - 2.0 * y1 + y2);
            double delta = 0;
            if (std::abs(denom) > 1e-12) {
                delta = 0.5 * (y0 - y2) / denom;
                delta = std::clamp(delta, -1.0, 1.0);
            }
            Peak peak;
            peak.lag = static_cast<double>(i) + delta;
            peak.id = id;
            peak.ncc = v;
            const int a = std::max(0, i);
            const int b = std::min(nx, i + m);
            double peak_abs = 0;
            bool clip = false;
            for (int s = a; s < b; ++s) {
                const double mag = std::abs(samples[s]);
                peak_abs = std::max(peak_abs, mag);
                if (mag >= 0.999) {
                    clip = true;
                }
            }
            peak.level = (peak_abs > 1e-12) ? 20.0 * std::log10(peak_abs) : -120;
            peak.clipped = clip;
            peaks.push_back(peak);
        }
    }

    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.lag < b.lag; });
    const double centre_offset = (m - 1) * 0.5;
    const double merge = rate * 0.01; // 10 ms
    std::vector<Peak> kept;
    for (const auto& peak : peaks) {
        if (!kept.empty() && std::abs(kept.back().lag - peak.lag) * 1.0 < merge && kept.back().id == peak.id) {
            if (peak.ncc > kept.back().ncc) {
                kept.back() = peak;
            }
            continue;
        }
        kept.push_back(peak);
    }

    const double period_samples = config_.period_ms * 0.001 * rate;
    for (size_t i = 0; i < kept.size(); ++i) {
        double rival = 0;
        for (size_t j = 0; j < kept.size(); ++j) {
            if (i == j) {
                continue;
            }
            if (std::abs(kept[j].lag - kept[i].lag) < merge && kept[j].id != kept[i].id) {
                rival = std::max(rival, kept[j].ncc);
            }
        }
        AudioHit hit;
        const double sample_pos = static_cast<double>(origin_sample) + kept[i].lag + centre_offset;
        hit.sample_pos = sample_pos;
        hit.time_ms = config_.timeline_origin_ms + (sample_pos - static_cast<double>(origin_sample)) * 1000.0 / rate
            + config_.timeline_origin_ms * 0;
        // timeline_origin_ms is the time of sample `origin_sample`.
        hit.time_ms = config_.timeline_origin_ms + (sample_pos - static_cast<double>(origin_sample)) * 1000.0 / rate;
        hit.chirp_id = kept[i].id;
        hit.correlation = kept[i].ncc;
        hit.level_dbfs = kept[i].level;
        hit.clipped = kept[i].clipped;
        hit.ambiguous = rival > 0 && kept[i].ncc < rival * config_.ambiguity_ratio;
        hit.quality = hit.ambiguous ? "invalid" : (hit.correlation >= 0.85 && !hit.clipped ? "good" : "degraded");
        if (hit.correlation < config_.threshold) {
            hit.quality = "invalid";
        }
        for (size_t j = 0; j < i; ++j) {
            if (kept[j].id == kept[i].id && std::abs(kept[j].lag - kept[i].lag) < period_samples) {
                hit.duplicate = true;
            }
        }
        hits.push_back(hit);
    }
    return hits;
}

} // namespace avsync
