#include "picture.hpp"

#include "qrcodegen.hpp"
#include "quirc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <vector>

namespace avsync {
namespace {

void rect_white(LumaView view, int x, int y, int w, int h)
{
    for (int row = std::max(0, y); row < std::min(view.height, y + h); ++row) {
        uint16_t* line = view.data + static_cast<size_t>(row) * static_cast<size_t>(view.stride);
        for (int col = std::max(0, x); col < std::min(view.width, x + w); ++col) {
            line[col] = kYWhite;
        }
    }
}

void blit_qr(LumaView view, const std::string& json)
{
    if (json.empty()) {
        return;
    }
    const FrameGeometry g = frame_geometry(view.width, view.height);
    const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(json.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = code.getSize();
    const int quiet = 4;
    const int span = modules + quiet * 2;
    const int scale = std::max(1, g.qr_size / span);
    const int drawn = span * scale;
    rect_white(view, g.qr_x, g.qr_y, drawn, drawn);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (!code.getModule(x, y)) {
                continue;
            }
            const int px = g.qr_x + (x + quiet) * scale;
            const int py = g.qr_y + (y + quiet) * scale;
            for (int dy = 0; dy < scale; ++dy) {
                uint16_t* line = view.data + static_cast<size_t>(py + dy) * static_cast<size_t>(view.stride);
                for (int dx = 0; dx < scale; ++dx) {
                    if (px + dx < view.width && py + dy < view.height) {
                        line[px + dx] = kYBlack;
                    }
                }
            }
        }
    }
}

} // namespace

void Yuv422p10::resize(int w, int h)
{
    width = w;
    height = h;
    y.assign(static_cast<size_t>(w * h), kYBackground);
    cb.assign(static_cast<size_t>((w / 2) * h), kYNeutralChroma);
    cr.assign(static_cast<size_t>((w / 2) * h), kYNeutralChroma);
}

struct Y10 {
    uint16_t y;
    uint16_t cb;
    uint16_t cr;
};

Y10 y8(int y, int cb, int cr)
{
    return {static_cast<uint16_t>(y * 4), static_cast<uint16_t>(cb * 4), static_cast<uint16_t>(cr * 4)};
}

void fill_yuv(Yuv422p10& frame, int x, int y, int w, int h, Y10 color)
{
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(frame.width, x + w);
    const int y1 = std::min(frame.height, y + h);
    for (int row = y0; row < y1; ++row) {
        uint16_t* ly = frame.y.data() + static_cast<size_t>(row) * frame.width;
        uint16_t* cb = frame.cb.data() + static_cast<size_t>(row) * (frame.width / 2);
        uint16_t* cr = frame.cr.data() + static_cast<size_t>(row) * (frame.width / 2);
        for (int col = x0; col < x1; ++col) {
            ly[col] = color.y;
            cb[col / 2] = color.cb;
            cr[col / 2] = color.cr;
        }
    }
}

void chroma_neutral(Yuv422p10& frame, int x, int y, int w, int h)
{
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(frame.width, x + w);
    const int y1 = std::min(frame.height, y + h);
    for (int row = y0; row < y1; ++row) {
        uint16_t* cb = frame.cb.data() + static_cast<size_t>(row) * (frame.width / 2);
        uint16_t* cr = frame.cr.data() + static_cast<size_t>(row) * (frame.width / 2);
        for (int col = x0; col < x1; ++col) {
            cb[col / 2] = kYNeutralChroma;
            cr[col / 2] = kYNeutralChroma;
        }
    }
}

void draw_smpte_eg1(Yuv422p10& frame)
{
    // SMPTE EG 1, the same band heights and colours as the usual smptebars test source.
    // Values are the 8-bit limited samples scaled into 10-bit (×4).
    const Y10 rainbow[7] = {
        y8(180, 128, 128), y8(162, 44, 142), y8(131, 156, 44), y8(112, 72, 58),
        y8(84, 184, 198), y8(65, 100, 212), y8(35, 212, 114),
    };
    const Y10 middle[7] = {
        y8(35, 212, 114), y8(19, 128, 128), y8(84, 184, 198), y8(19, 128, 128),
        y8(131, 156, 44), y8(19, 128, 128), y8(180, 128, 128),
    };
    const int width = frame.width;
    const int height = frame.height;
    const int bar_w = std::max(2, width / 7);
    const int top_h = height * 2 / 3;
    const int mid_h = height * 3 / 4 - top_h;
    const int bot_y = top_h + mid_h;
    const int bot_h = height - bot_y;
    for (int i = 0; i < 7; ++i) {
        const int x = i * bar_w;
        const int w = (i == 6) ? width - x : bar_w;
        fill_yuv(frame, x, 0, w, top_h, rainbow[i]);
        fill_yuv(frame, x, top_h, w, mid_h, middle[i]);
    }
    int x = 0;
    const int pulse_w = std::max(2, bar_w * 5 / 4);
    fill_yuv(frame, x, bot_y, pulse_w, bot_h, y8(57, 156, 97));
    x += pulse_w;
    fill_yuv(frame, x, bot_y, pulse_w, bot_h, y8(235, 128, 128));
    x += pulse_w;
    fill_yuv(frame, x, bot_y, pulse_w, bot_h, y8(44, 171, 147));
    x += pulse_w;
    const int black_w = std::max(0, 5 * bar_w - x);
    fill_yuv(frame, x, bot_y, black_w, bot_h, y8(16, 128, 128));
    x += black_w;
    const int pluge_w = std::max(2, bar_w / 3);
    fill_yuv(frame, x, bot_y, pluge_w, bot_h, y8(7, 128, 128));
    x += pluge_w;
    fill_yuv(frame, x, bot_y, pluge_w, bot_h, y8(16, 128, 128));
    x += pluge_w;
    fill_yuv(frame, x, bot_y, pluge_w, bot_h, y8(24, 128, 128));
    x += pluge_w;
    if (x < width) {
        fill_yuv(frame, x, bot_y, width - x, bot_h, y8(16, 128, 128));
    }
}

void draw_text_box(Yuv422p10& frame, int x, int y, std::string_view text, int scale)
{
    if (text.empty()) {
        return;
    }
    const int pad = std::max(2, scale);
    const int tw = text_px_width(text, scale);
    const int th = text_px_height(scale);
    fill_yuv(frame, x, y, tw + pad * 2, th + pad * 2, Y10{kYBlack, kYNeutralChroma, kYNeutralChroma});
    LumaView view{frame.y.data(), frame.width, frame.height, frame.width};
    draw_text(view, x + pad, y + pad, text, scale, kYWhite);
}

void draw_app_icon(Yuv422p10& frame, int x, int y, int size)
{
    if (size < 8) {
        return;
    }
    // Same mark as web/icon.png: dark purple field, white clapper, black board.
    const Y10 purple = y8(59, 161, 142);
    const Y10 ink{kYBlack, kYNeutralChroma, kYNeutralChroma};
    const Y10 paper{kYWhite, kYNeutralChroma, kYNeutralChroma};
    fill_yuv(frame, x, y, size, size, purple);
    const int inset = std::max(2, size * 10 / 64);
    const int clapper_y = y + size * 14 / 64;
    const int clapper_h = std::max(3, size * 16 / 64);
    const int board_y = clapper_y + clapper_h + std::max(1, size / 32);
    const int board_h = std::max(4, size * 26 / 64);
    fill_yuv(frame, x + inset, clapper_y, size - 2 * inset, clapper_h, paper);
    for (int row = 0; row < clapper_h; ++row) {
        for (int col = 0; col < size - 2 * inset; ++col) {
            const double t = static_cast<double>(col) / std::max(1, size - 2 * inset) + static_cast<double>(row) / std::max(1, clapper_h);
            const int band = static_cast<int>(t * 3.0);
            if (band % 2 == 1) {
                fill_yuv(frame, x + inset + col, clapper_y + row, 1, 1, ink);
            }
        }
    }
    fill_yuv(frame, x + inset, board_y, size - 2 * inset, board_h, ink);
    fill_yuv(frame, x + inset, clapper_y + clapper_h, size - 2 * inset, std::max(1, size / 32), paper);
}

void blit_rgba(Yuv422p10& frame, int dst_x, int dst_y, int box, const SourceStill& image)
{
    if (image.width <= 0 || image.height <= 0 || image.rgba.size() < static_cast<size_t>(image.width * image.height * 4) || box <= 0) {
        return;
    }
    const double scale = std::min(static_cast<double>(box) / image.width, static_cast<double>(box) / image.height);
    const int dw = std::max(1, static_cast<int>(std::lround(image.width * scale)));
    const int dh = std::max(1, static_cast<int>(std::lround(image.height * scale)));
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(image.height - 1, static_cast<int>(y / scale));
        const int dy = dst_y + y;
        if (dy < 0 || dy >= frame.height) {
            continue;
        }
        for (int x = 0; x < dw; ++x) {
            const int sx = std::min(image.width - 1, static_cast<int>(x / scale));
            const int dx = dst_x + x;
            if (dx < 0 || dx >= frame.width) {
                continue;
            }
            const uint8_t* p = image.rgba.data() + static_cast<size_t>(sy * image.width + sx) * 4;
            const int alpha = p[3];
            if (alpha == 0) {
                continue;
            }
            const double r = p[0] / 255.0;
            const double g = p[1] / 255.0;
            const double b = p[2] / 255.0;
            const double Y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const double Cb = -0.1146 * r - 0.3854 * g + 0.5 * b;
            const double Cr = 0.5 * r - 0.4542 * g - 0.0458 * b;
            auto lim = [](double v, double bias, double gain) {
                return static_cast<uint16_t>(std::lround(std::clamp(bias + gain * v, 0.0, 1023.0)));
            };
            uint16_t yv = lim(Y, 64, 876);
            uint16_t cb = lim(Cb, 512, 896);
            uint16_t cr = lim(Cr, 512, 896);
            if (alpha < 255) {
                const double a = alpha / 255.0;
                const size_t yi = static_cast<size_t>(dy) * frame.width + dx;
                const size_t ci = static_cast<size_t>(dy) * (frame.width / 2) + dx / 2;
                yv = static_cast<uint16_t>(std::lround(yv * a + frame.y[yi] * (1.0 - a)));
                cb = static_cast<uint16_t>(std::lround(cb * a + frame.cb[ci] * (1.0 - a)));
                cr = static_cast<uint16_t>(std::lround(cr * a + frame.cr[ci] * (1.0 - a)));
            }
            frame.y[static_cast<size_t>(dy) * frame.width + dx] = yv;
            frame.cb[static_cast<size_t>(dy) * (frame.width / 2) + dx / 2] = cb;
            frame.cr[static_cast<size_t>(dy) * (frame.width / 2) + dx / 2] = cr;
        }
    }
}

void draw_sync_scale(Yuv422p10& frame, int frame_in_period, int period_frames, double period_ms)
{
    const SyncPlace place = sync_place(frame_in_period, period_frames, period_ms);
    const FrameGeometry g = frame_geometry(frame.width, frame.height);
    const int span = std::max(1, g.sweep_x1 - g.sweep_x0);
    const int track_h = std::max(6, frame.height / 120);
    const int tick_w = std::max(3, frame.width / 640);
    const Y10 black{kYBlack, kYNeutralChroma, kYNeutralChroma};
    const Y10 white{kYWhite, kYNeutralChroma, kYNeutralChroma};
    fill_yuv(frame, g.sweep_x0, g.sweep_y - track_h / 2, span, track_h, black);
    const int radius = std::max(12, frame.height / 40);
    const int limit = std::max(20, static_cast<int>(std::lround(place.scale_ms)));
    auto station_x = [&](int ms) {
        const double along = (static_cast<double>(ms) + place.scale_ms) / (2.0 * place.scale_ms);
        return g.sweep_x0 + static_cast<int>(std::lround(std::clamp(along, 0.0, 1.0) * (span - 1)));
    };
    // Every 20 ms from the centre out to the scale ends. ±limit is kept when it is not itself a 20 ms step.
    std::vector<int> stations;
    stations.push_back(0);
    for (int ms = 20; ms < limit; ms += 20) {
        stations.push_back(ms);
        stations.push_back(-ms);
    }
    stations.push_back(limit);
    stations.push_back(-limit);
    struct Label {
        int ms;
        int x0;
        int x1;
        int scale;
    };
    std::vector<Label> labels;
    auto pack = [&](int step, int scale, std::vector<Label>& out) {
        out.clear();
        const int pad = std::max(2, scale);
        const int gap = 4;
        std::vector<int> marks;
        marks.push_back(0);
        for (int ms = step; ms < limit; ms += step) {
            marks.push_back(ms);
            marks.push_back(-ms);
        }
        if (std::find(marks.begin(), marks.end(), limit) == marks.end()) {
            marks.push_back(limit);
            marks.push_back(-limit);
        }
        for (int ms : marks) {
            char text[16];
            if (ms == 0) {
                std::snprintf(text, sizeof(text), "0");
            } else {
                std::snprintf(text, sizeof(text), "%+d", ms);
            }
            const int tw = text_px_width(text, scale) + pad * 2;
            if (tw > span) {
                return false;
            }
            const int tx = station_x(ms);
            int lx = tx - tw / 2;
            lx = std::max(g.sweep_x0, std::min(lx, g.sweep_x1 - tw));
            if (tx < lx || tx >= lx + tw) {
                return false;
            }
            out.push_back({ms, lx, lx + tw, scale});
        }
        std::sort(out.begin(), out.end(), [](const Label& a, const Label& b) { return a.x0 < b.x0; });
        for (size_t i = 1; i < out.size(); ++i) {
            if (out[i].x0 < out[i - 1].x1 + gap) {
                return false;
            }
        }
        return !out.empty();
    };
    const int scales[] = {frame.height >= 720 ? 2 : 1, 1};
    const int steps[] = {20, 50, 100};
    for (int step : steps) {
        bool packed = false;
        for (int scale : scales) {
            if (pack(step, scale, labels)) {
                packed = true;
                break;
            }
        }
        if (packed) {
            break;
        }
        labels.clear();
    }
    auto labeled = [&](int ms) {
        for (const auto& label : labels) {
            if (label.ms == ms) {
                return true;
            }
        }
        return false;
    };
    for (const auto& label : labels) {
        const bool present = std::find(stations.begin(), stations.end(), label.ms) != stations.end();
        if (!present) {
            stations.push_back(label.ms);
        }
    }
    for (int ms : stations) {
        const int tx = station_x(ms);
        const int tick_h = (ms == 0) ? track_h * 5 : (labeled(ms) ? track_h * 3 : track_h * 2);
        fill_yuv(frame, tx - tick_w / 2, g.sweep_y - tick_h / 2, tick_w, tick_h, white);
    }
    if (!labels.empty()) {
        const int label_scale = labels.front().scale;
        const int pad = std::max(2, label_scale);
        const int th = text_px_height(label_scale) + pad * 2;
        const int label_y = g.sweep_y - radius - th - 8;
        for (const auto& label : labels) {
            char text[16];
            if (label.ms == 0) {
                std::snprintf(text, sizeof(text), "0");
            } else {
                std::snprintf(text, sizeof(text), "%+d", label.ms);
            }
            draw_text_box(frame, label.x0, label_y, text, label_scale);
        }
    }
    const double along = (place.position_ms + place.scale_ms) / (2.0 * place.scale_ms);
    const int dot = g.sweep_x0 + static_cast<int>(std::lround(std::clamp(along, 0.0, 1.0) * (span - 1)));
    Y10 color = white;
    const SyncDot kind = sync_dot_kind(place.position_ms);
    if (kind == SyncDot::Early) {
        color = y8(81, 90, 240);
    } else if (kind == SyncDot::Late) {
        color = y8(145, 54, 34);
    }
    const int side = radius * 2;
    fill_yuv(frame, dot - radius, g.sweep_y - radius, side, side, color);
    const int word = frame.height >= 720 ? 2 : 1;
    const int word_y = g.sweep_y + radius + 6;
    draw_text_box(frame, g.sweep_x0, word_y, "early", word);
    const int late_w = text_px_width("late", word) + std::max(2, word) * 2;
    draw_text_box(frame, g.sweep_x1 - late_w, word_y, "late", word);
}

const SourceStill* cached_app_icon()
{
    static SourceStill still;
    static int state = 0;
    if (state == 0) {
        state = 1;
#ifdef AVSYNC_ICON_PATH
        SourceClip clip;
        std::string error;
        if (clip.load(AVSYNC_ICON_PATH, error) && !clip.frames.empty()) {
            still = std::move(clip.frames.front());
            state = 2;
        }
#endif
    }
    return state == 2 ? &still : nullptr;
}

void draw_source_ident(Yuv422p10& frame, const PictureInfo& info, int left, int right, int top, int bottom)
{
    const bool has_text = !info.source.empty();
    const bool has_image = info.source_image != nullptr && info.source_image->width > 0 && info.source_image->height > 0
        && info.source_image->rgba.size() >= static_cast<size_t>(info.source_image->width * info.source_image->height * 4);
    if (!has_text && !has_image) {
        return;
    }
    const int avail_w = right - left;
    const int avail_h = bottom - top;
    if (avail_w < 16 || avail_h < 8) {
        return;
    }
    int scale = std::max(2, frame.height / 70);
    auto measure = [&](int s) {
        const int pad = std::max(2, s);
        const int tw = has_text ? text_px_width(info.source, s) + pad * 2 : 0;
        const int th = has_text ? text_px_height(s) + pad * 2 : 0;
        const int gap = (has_text && has_image) ? std::max(4, s) : 0;
        const int ih = has_image ? (has_text ? th : std::min(avail_h, std::max(th, frame.height / 8))) : 0;
        return std::tuple<int, int, int, int>{tw, th, gap, ih};
    };
    while (scale > 1) {
        const auto [tw, th, gap, ih] = measure(scale);
        if (tw + gap + ih <= avail_w && std::max(th, ih) <= avail_h) {
            break;
        }
        --scale;
    }
    const auto [tw, th, gap, ih] = measure(scale);
    if (has_text && tw > avail_w) {
        return;
    }
    int x = left;
    if (has_text) {
        draw_text_box(frame, x, top, info.source, scale);
        x += tw + gap;
    }
    if (has_image && x + 4 < right) {
        blit_rgba(frame, x, top, std::min(ih, right - x), *info.source_image);
    }
}

void render_picture(Yuv422p10& frame, const PictureInfo& info, uint64_t seq, int frame_in_period, int period_frames, bool marker_white)
{
    draw_smpte_eg1(frame);
    LumaView view{frame.y.data(), frame.width, frame.height, frame.width};
    draw_static_layer(view);
    draw_timing(view, seq, frame_in_period, period_frames, marker_white);
    const FrameGeometry geo = frame_geometry(frame.width, frame.height);
    chroma_neutral(frame, 0, 0, geo.fid_size, geo.fid_size);
    chroma_neutral(frame, frame.width - geo.fid_size, 0, geo.fid_size, geo.fid_size);
    chroma_neutral(frame, 0, frame.height - geo.fid_size, geo.fid_size, geo.fid_size);
    chroma_neutral(frame, frame.width - geo.fid_size, frame.height - geo.fid_size, geo.fid_size, geo.fid_size);
    chroma_neutral(frame, geo.marker_x - 4, geo.marker_y - 4, geo.marker_side + 8, geo.marker_side + 8);
    chroma_neutral(frame, geo.strip_x, geo.strip_y, geo.strip_w * 32, geo.strip_h);
    const int scale = std::max(1, frame.height / 360);
    const int small = std::max(1, scale - 1);
    auto place = [&](int x, int y, std::string_view text, int text_scale) {
        draw_text_box(frame, x, y, text, text_scale);
    };
    const std::string banner = "TEST SIGNAL";
    const int banner_scale = std::max(2, frame.height / 270);
    const int banner_pad = std::max(2, banner_scale);
    const int banner_w = text_px_width(banner, banner_scale) + banner_pad * 2;
    const int banner_h = text_px_height(banner_scale) + banner_pad * 2;
    const int icon_px = std::max(48, frame.height / 12);
    const int gap = std::max(8, frame.width / 160);
    const int group_w = banner_w + gap + icon_px;
    int group_x = (frame.width - group_w) / 2;
    group_x = std::max(group_x, geo.fid_size + 4);
    const int qr_right = geo.qr_x + geo.qr_size + gap;
    if (group_x < qr_right) {
        group_x = qr_right;
    }
    const int icon_y = std::max(6, geo.qr_y);
    const int icon_x = group_x + banner_w + gap;
    place(group_x, icon_y + std::max(0, (icon_px - banner_h) / 2), banner, banner_scale);
    if (const SourceStill* icon = cached_app_icon()) {
        blit_rgba(frame, icon_x, icon_y, icon_px, *icon);
    } else {
        draw_app_icon(frame, icon_x, icon_y, icon_px);
    }
    const int ident_top = icon_y + icon_px + gap;
    const int ident_bottom = std::min(geo.marker_y, geo.sweep_y) - 8;
    const int ident_right = frame.width - geo.fid_size - 8;
    draw_source_ident(frame, info, group_x, ident_right, ident_top, ident_bottom);
    const std::string footer = "TEST SIGNAL";
    const int footer_w = text_px_width(footer, banner_scale) + banner_pad * 2;
    const int footer_h = text_px_height(banner_scale) + banner_pad * 2;
    place((frame.width - footer_w) / 2, geo.strip_y - footer_h - 8, footer, banner_scale);
    int text_x = qr_right;
    int text_y = icon_y;
    const int column_right = group_x - gap;
    auto line = [&](const std::string& text) {
        if (text.empty() || text_x >= column_right) {
            return;
        }
        int text_scale = small;
        while (text_scale > 1 && text_px_width(text, text_scale) + 8 > column_right - text_x) {
            --text_scale;
        }
        if (text_px_width(text, text_scale) + 8 > column_right - text_x) {
            return;
        }
        place(text_x, text_y, text, text_scale);
        text_y += text_px_height(text_scale) + std::max(2, text_scale) * 2 + 4;
    };
    line(info.title);
    line(info.format_line);
    line(info.offset_line);
    line(info.notes);
    place(geo.marker_x + geo.marker_side + scale * 4, geo.marker_y, info.counter, small);
    place(geo.marker_x + geo.marker_side + scale * 4, geo.marker_y + text_px_height(small) + 12, info.time_line, small);
    blit_qr(view, info.qr_json);
    const int qr_span = geo.qr_size + scale * 2;
    chroma_neutral(frame, geo.qr_x, geo.qr_y, qr_span, qr_span);
    draw_sync_scale(frame, frame_in_period, period_frames, info.period_ms);
}

std::optional<std::string> decode_qr_text(const uint16_t* luma, int width, int height, int stride)
{
    const FrameGeometry g = frame_geometry(width, height);
    const int margin = std::max(8, g.qr_size / 8);
    const int x0 = std::max(0, g.qr_x - margin);
    const int y0 = std::max(0, g.qr_y - margin);
    const int x1 = std::min(width, g.qr_x + g.qr_size + margin);
    const int y1 = std::min(height, g.qr_y + g.qr_size + margin);
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w < 16 || h < 16) {
        return std::nullopt;
    }
    quirc* qr = quirc_new();
    if (qr == nullptr || quirc_resize(qr, w, h) < 0) {
        quirc_destroy(qr);
        return std::nullopt;
    }
    int qw = 0;
    int qh = 0;
    uint8_t* image = quirc_begin(qr, &qw, &qh);
    for (int y = 0; y < qh; ++y) {
        const uint16_t* src = luma + static_cast<size_t>(y0 + y) * static_cast<size_t>(stride) + x0;
        uint8_t* dst = image + static_cast<size_t>(y) * static_cast<size_t>(qw);
        for (int x = 0; x < qw; ++x) {
            dst[x] = static_cast<uint8_t>(std::min(255, src[x] >> 2));
        }
    }
    quirc_end(qr);
    std::optional<std::string> found;
    const int count = quirc_count(qr);
    for (int i = 0; i < count; ++i) {
        quirc_code code;
        quirc_extract(qr, i, &code);
        quirc_data data;
        if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
            continue;
        }
        found = std::string(reinterpret_cast<char*>(data.payload), static_cast<size_t>(data.payload_len));
        break;
    }
    quirc_destroy(qr);
    return found;
}

} // namespace avsync
