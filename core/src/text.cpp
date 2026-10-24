#include "avsync/avsync.hpp"

#include <algorithm>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-narrowing"
#endif
#include "font8x8_basic.h"
#if defined(__clang__)
#pragma clang diagnostic pop
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
                if ((bits & (1u << (7 - col))) == 0) {
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

} // namespace avsync
