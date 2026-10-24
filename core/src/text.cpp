#include "avsync/avsync.hpp"
#include "avsync/dejavu_sans.hpp"

#include <blend2d/blend2d.h>

#include <algorithm>
#include <cmath>

namespace avsync {
namespace {

BLFontFace const& dejavu_face()
{
    static const BLFontFace face = [] {
        BLFontData data;
        BLFontFace created;
        if (data.create_from_data(font::dejavuSans, font::dejavuSansSize) == BL_SUCCESS) {
            created.create_from_data(data, 0);
        }
        return created;
    }();
    return face;
}

int font_px(int scale)
{
    return 8 * std::max(1, scale);
}

bool make_font(int scale, BLFont& font)
{
    const BLFontFace& face = dejavu_face();
    return face.is_valid() && font.create_from_face(face, static_cast<float>(font_px(scale))) == BL_SUCCESS;
}

} // namespace

void draw_text(LumaView view, int x, int y, std::string_view text, int scale, uint16_t y_value)
{
    if (text.empty() || view.data == nullptr) {
        return;
    }
    BLFont font;
    if (!make_font(scale, font)) {
        return;
    }
    const int width = std::max(1, text_px_width(text, scale));
    const int height = std::max(1, text_px_height(scale));
    BLImage image(width, height, BL_FORMAT_A8);
    {
        BLContext ctx(image);
        ctx.clear_all();
        ctx.set_fill_style(BLRgba32(0xFFFFFFFF));
        ctx.fill_utf8_text(BLPoint(0.0, font.metrics().ascent), font, text.data(), text.size());
        ctx.end();
    }
    BLImageData pixels;
    if (image.get_data(&pixels) != BL_SUCCESS || pixels.pixel_data == nullptr) {
        return;
    }
    const auto* base = static_cast<const uint8_t*>(pixels.pixel_data);
    for (int row = 0; row < height; ++row) {
        const int yy = y + row;
        if (yy < 0 || yy >= view.height) {
            continue;
        }
        const uint8_t* src = base + static_cast<std::ptrdiff_t>(row) * pixels.stride;
        uint16_t* dst = view.data + static_cast<size_t>(yy) * static_cast<size_t>(view.stride);
        for (int col = 0; col < width; ++col) {
            if (src[col] < 96) {
                continue;
            }
            const int xx = x + col;
            if (xx >= 0 && xx < view.width) {
                dst[xx] = y_value;
            }
        }
    }
}

int text_px_width(std::string_view text, int scale)
{
    if (text.empty()) {
        return 0;
    }
    BLFont font;
    if (!make_font(scale, font)) {
        return 0;
    }
    BLGlyphBuffer glyphs;
    glyphs.set_utf8_text(text.data(), text.size());
    BLTextMetrics metrics;
    if (font.get_text_metrics(glyphs, metrics) != BL_SUCCESS) {
        return 0;
    }
    return std::max(0, static_cast<int>(std::lround(metrics.advance.x)));
}

int text_px_height(int scale)
{
    BLFont font;
    if (!make_font(scale, font)) {
        return font_px(scale);
    }
    const BLFontMetrics metrics = font.metrics();
    return std::max(1, static_cast<int>(std::ceil(metrics.ascent + metrics.descent)));
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
