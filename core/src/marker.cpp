#include "avsync/avsync.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace avsync {
namespace {

int clampi(int v, int lo, int hi)
{
    return std::max(lo, std::min(hi, v));
}

void rect(LumaView view, int x, int y, int w, int h, uint16_t value)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    const int x0 = clampi(x, 0, view.width);
    const int y0 = clampi(y, 0, view.height);
    const int x1 = clampi(x + w, 0, view.width);
    const int y1 = clampi(y + h, 0, view.height);
    for (int row = y0; row < y1; ++row) {
        uint16_t* line = view.data + static_cast<size_t>(row) * static_cast<size_t>(view.stride);
        for (int col = x0; col < x1; ++col) {
            line[col] = value;
        }
    }
}

void frame_rect(LumaView view, int x, int y, int w, int h, int t, uint16_t value)
{
    rect(view, x, y, w, t, value);
    rect(view, x, y + h - t, w, t, value);
    rect(view, x, y, t, h, value);
    rect(view, x + w - t, y, t, h, value);
}

double mean_rect(const uint16_t* luma, int stride, int x, int y, int w, int h, int width, int height, double* spread)
{
    const int x0 = clampi(x, 0, width);
    const int y0 = clampi(y, 0, height);
    const int x1 = clampi(x + w, 0, width);
    const int y1 = clampi(y + h, 0, height);
    double sum = 0;
    double sum2 = 0;
    int n = 0;
    int min_v = 1023;
    int max_v = 0;
    for (int row = y0; row < y1; ++row) {
        const uint16_t* line = luma + static_cast<size_t>(row) * static_cast<size_t>(stride);
        for (int col = x0; col < x1; ++col) {
            const int v = line[col];
            sum += v;
            sum2 += static_cast<double>(v) * v;
            min_v = std::min(min_v, v);
            max_v = std::max(max_v, v);
            ++n;
        }
    }
    if (n == 0) {
        if (spread) {
            *spread = 0;
        }
        return 0;
    }
    const double mean = sum / n;
    if (spread) {
        *spread = static_cast<double>(max_v - min_v);
    }
    (void)sum2;
    return mean;
}

} // namespace

FrameGeometry frame_geometry(int width, int height)
{
    FrameGeometry g;
    g.width = width;
    g.height = height;
    g.marker_side = std::max(8, height * 40 / 100);
    g.marker_x = (width - g.marker_side) / 2;
    g.marker_y = (height - g.marker_side) / 2;
    g.strip_w = std::max(1, width / 40);
    g.strip_h = std::max(8, height * 4 / 100);
    const int total = g.strip_w * 32;
    g.strip_x = (width - total) / 2;
    g.strip_y = height - g.strip_h - std::max(2, height / 60);
    g.qr_size = std::max(16, height * 18 / 100);
    g.qr_x = std::max(2, height * 2 / 100);
    g.qr_y = g.qr_x;
    g.sweep_x0 = std::max(4, width / 40);
    g.sweep_x1 = std::max(g.sweep_x0 + 8, g.marker_x - width / 40);
    g.sweep_y = g.marker_y + g.marker_side / 2;
    g.fid_size = std::max(12, height * 6 / 100);
    return g;
}

void fill_luma(LumaView view, uint16_t y)
{
    for (int row = 0; row < view.height; ++row) {
        uint16_t* line = view.data + static_cast<size_t>(row) * static_cast<size_t>(view.stride);
        std::fill(line, line + view.width, y);
    }
}

void draw_static_layer(LumaView view)
{
    const FrameGeometry g = frame_geometry(view.width, view.height);
    const int t = std::max(2, g.fid_size / 5);
    const int corners[4][2] = {{0, 0}, {view.width - g.fid_size, 0}, {0, view.height - g.fid_size},
        {view.width - g.fid_size, view.height - g.fid_size}};
    for (const auto& corner : corners) {
        rect(view, corner[0], corner[1], g.fid_size, g.fid_size, kYWhite);
        rect(view, corner[0] + t, corner[1] + t, g.fid_size - 2 * t, g.fid_size - 2 * t, kYBlack);
        rect(view, corner[0] + 2 * t, corner[1] + 2 * t, g.fid_size - 4 * t, g.fid_size - 4 * t, kYWhite);
    }
    frame_rect(view, g.marker_x - 4, g.marker_y - 4, g.marker_side + 8, g.marker_side + 8, 3, kYWhite);
    rect(view, g.sweep_x0, g.sweep_y - 1, g.sweep_x1 - g.sweep_x0, 3, kYWhite);
}

void draw_timing(LumaView view, uint64_t seq, int frame_in_period, int period_frames, bool marker_white)
{
    const FrameGeometry g = frame_geometry(view.width, view.height);
    rect(view, g.marker_x, g.marker_y, g.marker_side, g.marker_side, marker_white ? kYWhite : kYBlack);
    const uint32_t gray = binary_to_gray(static_cast<uint32_t>(seq & 0xFFFFFFu));
    const int pos = frame_in_period & 15;
    uint32_t bits = 0;
    int parity = 0;
    for (int i = 0; i < 24; ++i) {
        const int bit = (gray >> (23 - i)) & 1;
        bits |= static_cast<uint32_t>(bit) << (23 - i);
        parity ^= bit;
    }
    for (int i = 0; i < 4; ++i) {
        const int bit = (pos >> (3 - i)) & 1;
        parity ^= bit;
        (void)bits;
    }
    const int parity_bit = parity & 1; // even parity: this bit equals the xor, so total xor with it is 0? 
    // Store parity_bit such that xor(data) xor parity_bit == 0, i.e. parity_bit == xor(data).
    for (int i = 0; i < 32; ++i) {
        int bit = 0;
        if (i < 24) {
            bit = (gray >> (23 - i)) & 1;
        } else if (i < 28) {
            bit = (pos >> (3 - (i - 24))) & 1;
        } else if (i == 28) {
            bit = parity_bit;
        } else if (i == 29 || i == 31) {
            bit = 1;
        } else {
            bit = 0;
        }
        rect(view, g.strip_x + i * g.strip_w, g.strip_y, g.strip_w, g.strip_h, bit ? kYWhite : kYBlack);
    }
    (void)period_frames;
    // The moving phase dot used to live here. The generator draws the offset dot
    // on this scale (0 at the centre). The analyzer reads the marker and the strip, not this line.
}

uint32_t binary_to_gray(uint32_t value)
{
    return value ^ (value >> 1);
}

uint32_t gray_to_binary(uint32_t gray)
{
    uint32_t binary = gray;
    for (uint32_t mask = gray >> 1; mask != 0; mask >>= 1) {
        binary ^= mask;
    }
    return binary;
}

MarkerReading read_marker(const uint16_t* luma, int width, int height, int stride)
{
    MarkerReading reading;
    const FrameGeometry g = frame_geometry(width, height);
    auto block_mean = [&](int index, double* spread) {
        const int inset_x = std::max(1, g.strip_w / 5);
        const int inset_y = std::max(1, g.strip_h / 5);
        return mean_rect(luma, stride, g.strip_x + index * g.strip_w + inset_x, g.strip_y + inset_y,
            std::max(1, g.strip_w - 2 * inset_x), std::max(1, g.strip_h - 2 * inset_y), width, height, spread);
    };
    double spread_w = 0;
    double spread_b = 0;
    const double white = 0.5 * (block_mean(29, &spread_w) + block_mean(31, &spread_w));
    const double black = block_mean(30, &spread_b);
    reading.contrast = white - black;
    reading.noise = std::max(spread_w, spread_b);
    const double threshold = 0.5 * (white + black);
    int ones = 0;
    uint32_t gray = 0;
    int pos = 0;
    int parity_bit = 0;
    int parity = 0;
    for (int i = 0; i < 29; ++i) {
        const int bit = block_mean(i, nullptr) >= threshold ? 1 : 0;
        if (i < 24) {
            gray |= static_cast<uint32_t>(bit) << (23 - i);
            parity ^= bit;
        } else if (i < 28) {
            pos |= bit << (3 - (i - 24));
            parity ^= bit;
        } else {
            parity_bit = bit;
        }
        ones += bit;
    }
    (void)ones;
    reading.seq_low24 = gray_to_binary(gray) & 0xFFFFFFu;
    reading.frame_pos_mod16 = pos;
    reading.strip_ok = ((parity ^ parity_bit) == 0) && reading.contrast > 80;
    const int inset = g.marker_side / 4;
    double marker_spread = 0;
    const double marker = mean_rect(luma, stride, g.marker_x + inset, g.marker_y + inset, g.marker_side - 2 * inset,
        g.marker_side - 2 * inset, width, height, &marker_spread);
    if (marker_spread > 400 || reading.contrast < 80) {
        reading.state = MarkerReading::State::Invalid;
    } else if (marker >= threshold) {
        reading.state = MarkerReading::State::White;
    } else {
        reading.state = MarkerReading::State::Black;
    }
    const int t = std::max(2, g.fid_size / 5);
    const int cx = t / 2;
    const int ix = t + t / 2;
    const uint16_t outer = luma[static_cast<size_t>(cx) * stride + cx];
    const uint16_t inner = luma[static_cast<size_t>(ix) * stride + ix];
    reading.geometry_ok = outer > threshold && inner < threshold;
    if (!reading.strip_ok || reading.state == MarkerReading::State::Invalid || reading.contrast < 200) {
        reading.quality = "invalid";
    } else if (reading.contrast < 600 || reading.noise > 80 || !reading.geometry_ok) {
        reading.quality = "degraded";
    } else {
        reading.quality = "good";
    }
    return reading;
}

MarkerTracker::MarkerTracker(VideoFormat format, int event_rate)
    : format_(std::move(format))
    , period_(plan_period(format_, event_rate))
{
}

VideoObservation MarkerTracker::push(const uint16_t* luma, int stride, int64_t grain_index, double time_ms, int64_t tai_ns)
{
    VideoObservation obs;
    obs.grain_index = grain_index;
    obs.time_ms = time_ms;
    obs.tai_ns = tai_ns;
    obs.drops_total = drops_;
    obs.duplicates_total = duplicates_;
    const MarkerReading reading = read_marker(luma, format_.width, format_.grain_height(), stride);
    obs.quality = reading.quality;
    obs.contrast = reading.contrast;
    obs.strip_ok = reading.strip_ok;
    obs.marker_white = reading.state == MarkerReading::State::White;
    if (!reading.strip_ok) {
        obs.flags.push_back("strip_invalid");
    }
    if (reading.strip_ok) {
        const uint64_t predicted = static_cast<uint64_t>(std::max<int64_t>(0, grain_index)) / static_cast<uint64_t>(std::max(1, period_.grains));
        uint64_t base = predicted & ~uint64_t{0xFFFFFF};
        uint64_t seq = base | reading.seq_low24;
        auto closer = [&](uint64_t candidate) {
            const auto dist = [](uint64_t a, uint64_t b) { return (a > b) ? a - b : b - a; };
            return dist(candidate, predicted) < dist(seq, predicted);
        };
        if (base >= (uint64_t{1} << 24) && closer((base - (uint64_t{1} << 24)) | reading.seq_low24)) {
            seq = (base - (uint64_t{1} << 24)) | reading.seq_low24;
        }
        if (closer((base + (uint64_t{1} << 24)) | reading.seq_low24)) {
            seq = (base + (uint64_t{1} << 24)) | reading.seq_low24;
        }
        if (last_unwrapped_pos_ >= 0) {
            int delta = -1;
            for (int step = 0; step <= period_.grains + 16; ++step) {
                int pos = last_low_ + step;
                uint64_t seq_at = last_seq_;
                while (pos >= period_.grains) {
                    pos -= period_.grains;
                    ++seq_at;
                }
                if ((pos & 15) == reading.frame_pos_mod16 && (seq_at & 0xFFFFFFu) == (seq & 0xFFFFFFu)) {
                    delta = step;
                    last_low_ = pos;
                    break;
                }
            }
            if (delta < 0) {
                drops_ += 1;
                obs.flags.push_back("video_drop");
                last_low_ = reading.frame_pos_mod16;
            } else if (delta == 0) {
                ++duplicates_;
                obs.flags.push_back("video_dup");
            } else if (delta > 1) {
                drops_ += delta - 1;
                obs.flags.push_back("video_drop");
            }
        } else {
            int guess = reading.frame_pos_mod16;
            if (grain_index >= 0) {
                const int within = static_cast<int>(grain_index % period_.grains);
                if ((within & 15) == reading.frame_pos_mod16) {
                    guess = within;
                }
            }
            last_low_ = guess;
        }
        last_unwrapped_pos_ = grain_index;
        last_seq_ = seq;
        obs.seq = seq;
    }
    obs.drops_total = drops_;
    obs.duplicates_total = duplicates_;
    const bool changed = have_prev_ && reading.state != MarkerReading::State::Invalid && prev_state_ != MarkerReading::State::Invalid
        && reading.state != prev_state_;
    if (changed && reading.strip_ok) {
        obs.event = true;
        obs.seq = last_seq_;
        if (std::llabs(grain_index - last_event_grain_) <= 2 && last_event_grain_ >= 0) {
            // consecutive toggles are normal at the event; near-event drops are flagged by the pairer from counters.
        }
        last_event_grain_ = grain_index;
        for (const auto& flag : obs.flags) {
            if (flag == "video_drop") {
                obs.flags.push_back("video_drop_near");
                break;
            }
        }
    }
    if (reading.state != MarkerReading::State::Invalid) {
        prev_state_ = reading.state;
        have_prev_ = true;
    } else {
        obs.flags.push_back("marker_invalid");
    }
    return obs;
}

size_t v210_line_bytes(int width)
{
    return static_cast<size_t>(((width + 47) / 48) * 128);
}

void pack_v210(const uint16_t* y, const uint16_t* cb, const uint16_t* cr, int width, int height, int y_stride, uint8_t* dst)
{
    const size_t stride = v210_line_bytes(width);
    for (int row = 0; row < height; ++row) {
        uint32_t* out = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(row) * stride);
        const uint16_t* yline = y + static_cast<size_t>(row) * static_cast<size_t>(y_stride);
        int word = 0;
        for (int x = 0; x < width; x += 6) {
            auto Y = [&](int i) { return static_cast<uint32_t>(yline[std::min(i, width - 1)] & 0x3FF); };
            auto C = [&](const uint16_t* plane, int i) {
                if (plane == nullptr) {
                    return static_cast<uint32_t>(kYNeutralChroma);
                }
                const int cx = std::min(i / 2, width / 2 - 1);
                return static_cast<uint32_t>(plane[static_cast<size_t>(row) * static_cast<size_t>(y_stride) / 2 + cx] & 0x3FF);
            };
            // chroma planes are stored at half width with their own stride = y_stride/2 when non-null.
            const uint32_t cb0 = cb ? static_cast<uint32_t>(cb[static_cast<size_t>(row) * (y_stride / 2) + std::min(x / 2, width / 2 - 1)] & 0x3FF)
                                    : kYNeutralChroma;
            const uint32_t cr0 = cr ? static_cast<uint32_t>(cr[static_cast<size_t>(row) * (y_stride / 2) + std::min(x / 2, width / 2 - 1)] & 0x3FF)
                                    : kYNeutralChroma;
            const uint32_t cb1 = cb ? static_cast<uint32_t>(cb[static_cast<size_t>(row) * (y_stride / 2) + std::min((x + 2) / 2, width / 2 - 1)] & 0x3FF)
                                    : kYNeutralChroma;
            const uint32_t cr1 = cr ? static_cast<uint32_t>(cr[static_cast<size_t>(row) * (y_stride / 2) + std::min((x + 2) / 2, width / 2 - 1)] & 0x3FF)
                                    : kYNeutralChroma;
            const uint32_t cb2 = cb ? static_cast<uint32_t>(cb[static_cast<size_t>(row) * (y_stride / 2) + std::min((x + 4) / 2, width / 2 - 1)] & 0x3FF)
                                    : kYNeutralChroma;
            const uint32_t cr2 = cr ? static_cast<uint32_t>(cr[static_cast<size_t>(row) * (y_stride / 2) + std::min((x + 4) / 2, width / 2 - 1)] & 0x3FF)
                                    : kYNeutralChroma;
            out[word++] = (cb0) | (Y(x) << 10) | (cr0 << 20);
            out[word++] = (Y(x + 1)) | (cb1 << 10) | (Y(x + 2) << 20);
            out[word++] = (cr1) | (Y(x + 3) << 10) | (cb2 << 20);
            out[word++] = (Y(x + 4)) | (cr2 << 10) | (Y(x + 5) << 20);
            (void)C;
        }
    }
}

void unpack_v210_luma(const uint8_t* src, int width, int height, uint16_t* y, int y_stride)
{
    const size_t stride = v210_line_bytes(width);
    for (int row = 0; row < height; ++row) {
        const uint32_t* in = reinterpret_cast<const uint32_t*>(src + static_cast<size_t>(row) * stride);
        uint16_t* line = y + static_cast<size_t>(row) * static_cast<size_t>(y_stride);
        int word = 0;
        for (int x = 0; x < width; x += 6) {
            const uint32_t w0 = in[word++];
            const uint32_t w1 = in[word++];
            const uint32_t w2 = in[word++];
            const uint32_t w3 = in[word++];
            auto put = [&](int i, uint32_t value) {
                if (i < width) {
                    line[i] = static_cast<uint16_t>(value & 0x3FF);
                }
            };
            put(x, (w0 >> 10) & 0x3FF);
            put(x + 1, w1 & 0x3FF);
            put(x + 2, (w1 >> 20) & 0x3FF);
            put(x + 3, (w2 >> 10) & 0x3FF);
            put(x + 4, w3 & 0x3FF);
            put(x + 5, (w3 >> 20) & 0x3FF);
        }
    }
}

} // namespace avsync
