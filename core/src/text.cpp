#include "avsync/avsync.hpp"

#include <algorithm>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-narrowing"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#endif
#include "font8x8_basic.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace avsync {

void draw_text(LumaView view, int x, int y, std::string_view text, int scale, uint16_t y_value)
{
    if (scale < 1) {
        scale = 1;
    }
    int cursor = x;
    for (unsigned char ch : text) {
        if (ch >= 128) {
            ch = '?';
        }
        for (int row = 0; row < 8; ++row) {
            const unsigned char bits = static_cast<unsigned char>(font8x8_basic[ch][row]);
            for (int col = 0; col < 8; ++col) {
                // font8x8 stores the leftmost pixel in the least significant bit.
                if ((bits & (1u << col)) == 0) {
                    continue;
                }
                const int px = cursor + col * scale;
                const int py = y + row * scale;
                for (int dy = 0; dy < scale; ++dy) {
                    const int yy = py + dy;
                    if (yy < 0 || yy >= view.height) {
                        continue;
                    }
                    uint16_t* line = view.data + static_cast<size_t>(yy) * static_cast<size_t>(view.stride);
                    for (int dx = 0; dx < scale; ++dx) {
                        const int xx = px + dx;
                        if (xx >= 0 && xx < view.width) {
                            line[xx] = y_value;
                        }
                    }
                }
            }
        }
        cursor += 8 * scale + scale;
    }
}

int text_px_width(std::string_view text, int scale)
{
    if (text.empty() || scale < 1) {
        return 0;
    }
    return static_cast<int>(text.size()) * (8 * scale + scale) - scale;
}

int text_px_height(int scale)
{
    return 8 * std::max(1, scale);
}

SyncDot sync_dot_kind(double position_ms)
{
    if (position_ms < -1e-6) {
        return SyncDot::Early;
    }
    if (position_ms > 1e-6) {
        return SyncDot::Late;
    }
    return SyncDot::Zero;
}

SyncPlace sync_place(int frame_in_period, int period_frames, double period_ms)
{
    SyncPlace place;
    if (period_frames < 1) {
        period_frames = 1;
    }
    if (period_ms <= 0) {
        period_ms = 500;
    }
    int frame = frame_in_period % period_frames;
    if (frame < 0) {
        frame += period_frames;
    }
    const double frame_ms = period_ms / static_cast<double>(period_frames);
    const int half = std::max(1, period_frames / 2);
    place.scale_ms = half * frame_ms;
    if (frame == 0) {
        place.position_ms = 0;
    } else if (frame <= half) {
        place.position_ms = frame * frame_ms;
    } else {
        place.position_ms = (frame - period_frames) * frame_ms;
    }
    place.position_ms = std::clamp(place.position_ms, -place.scale_ms, place.scale_ms);
    return place;
}

} // namespace avsync
