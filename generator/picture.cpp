#include "picture.hpp"

#include "qrcodegen.hpp"
#include "ReadBarcode.h"

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
    const double frame_ms = period_ms / static_cast<double>(std::max(1, period_frames));
    const int frame_ms_i = std::max(1, static_cast<int>(std::lround(frame_ms)));
    const int half_frames = std::max(1, period_frames / 2);
    const int limit = half_frames * frame_ms_i;
    auto station_x = [&](int ms) {
        const double along = (static_cast<double>(ms) + place.scale_ms) / (2.0 * place.scale_ms);
        return g.sweep_x0 + static_cast<int>(std::lround(std::clamp(along, 0.0, 1.0) * (span - 1)));
    };
    // One tick per frame, from −half a period to +half a period. At 1080p50 that is ±240 ms.
    std::vector<int> stations;
    for (int frame = -half_frames; frame <= half_frames; ++frame) {
        stations.push_back(frame * frame_ms_i);
    }
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
    // Four frames is 80 ms at 1080p50; five frames is 100 ms. Both land on frame times.
    const int frame_steps[] = {4, 5};
    for (int frame_step : frame_steps) {
        const int step = frame_step * frame_ms_i;
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
    const bool has_source = !info.source.empty();
    const bool has_subtitle = !info.subtitle.empty();
    const bool has_image = info.source_image != nullptr && info.source_image->width > 0 && info.source_image->height > 0
        && info.source_image->rgba.size() >= static_cast<size_t>(info.source_image->width * info.source_image->height * 4);
    if (!has_source && !has_subtitle && !has_image) {
        return;
    }
    const int avail_w = right - left;
    const int avail_h = bottom - top;
    if (avail_w < 16 || avail_h < 8) {
        return;
    }
    int scale = std::max(2, frame.height / 70);
    struct Metrics {
        int src_w;
        int src_h;
        int sub_w;
        int sub_h;
        int gap;
        int image;
        int block_w;
        int block_h;
    };
    auto measure = [&](int s) {
        const int pad = std::max(2, s);
        Metrics m;
        m.src_w = has_source ? text_px_width(info.source, s) + pad * 2 : 0;
        m.src_h = has_source ? text_px_height(s) + pad * 2 : 0;
        m.sub_w = has_subtitle ? text_px_width(info.subtitle, s) + pad * 2 : 0;
        m.sub_h = has_subtitle ? text_px_height(s) + pad * 2 : 0;
        m.gap = std::max(4, s);
        const int text_w = std::max(m.src_w, m.sub_w);
        const int text_h = m.src_h + ((m.src_h > 0 && m.sub_h > 0) ? m.gap : 0) + m.sub_h;
        m.image = 0;
        if (has_image) {
            m.image = m.src_h > 0 ? m.src_h : std::max(m.sub_h, std::min(avail_h, std::max(32, frame.height / 12)));
        }
        m.block_w = m.image + ((m.image > 0 && text_w > 0) ? m.gap : 0) + text_w;
        m.block_h = std::max(m.image, text_h);
        return m;
    };
    while (scale > 1) {
        const Metrics m = measure(scale);
        if (m.block_w <= avail_w && m.block_h <= avail_h) {
            break;
        }
        --scale;
    }
    const Metrics m = measure(scale);
    if (m.block_w > avail_w || m.block_h > avail_h) {
        return;
    }
    const int origin_x = left + std::max(0, (avail_w - m.block_w) / 2);
    const int origin_y = top + std::max(0, (avail_h - m.block_h) / 2);
    int text_x = origin_x;
    if (m.image > 0) {
        blit_rgba(frame, origin_x, origin_y, m.image, *info.source_image);
        text_x += m.image + m.gap;
    }
    int text_y = origin_y;
    if (has_source) {
        draw_text_box(frame, text_x, text_y, info.source, scale);
        text_y += m.src_h + (has_subtitle ? m.gap : 0);
    }
    if (has_subtitle) {
        draw_text_box(frame, text_x, text_y, info.subtitle, scale);
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
    const int block_top = geo.marker_y + geo.marker_side + gap;
    const int block_bottom = geo.strip_y - gap;
    const int block_left = geo.fid_size + gap;
    const int block_right = frame.width - geo.fid_size - gap;
    draw_source_ident(frame, info, block_left, block_right, block_top, block_bottom);
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
    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTextMode(ZXing::TextMode::Plain);
    const auto decode_region = [&](int left, int top, int w, int h) -> std::optional<std::string> {
        if (w < 16 || h < 16) return std::nullopt;
        std::vector<uint8_t> gray(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int y = 0; y < h; ++y) {
            const uint16_t* src = luma + static_cast<size_t>(top + y) * static_cast<size_t>(stride) + left;
            uint8_t* dst = gray.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
            for (int x = 0; x < w; ++x) {
                dst[x] = static_cast<uint8_t>(std::min(255, src[x] >> 2));
            }
        }
        const ZXing::ImageView view(gray.data(), w, h, ZXing::ImageFormat::Lum);
        const ZXing::Barcode barcode = ZXing::ReadBarcode(view, options);
        if (!barcode.isValid() || barcode.text().empty()) return std::nullopt;
        return barcode.text();
    };
    if (auto text = decode_region(x0, y0, x1 - x0, y1 - y0)) return text;
    // A scaled/padded multiviewer tile does not share the canvas QR geometry.
    // Keep its original pixels rather than depending on a second rectification.
    if (auto text = decode_region(0, 0, width, height)) return text;
    // Very small resampled modules can defeat the default local binarizer.
    // Retry the same original pixels with ZXing's global histogram reader.
    options.setBinarizer(ZXing::Binarizer::GlobalHistogram);
    return decode_region(0, 0, width, height);
}

namespace {

bool full_picture_state(const std::string& state)
{
    return state == "NO SIGNAL" || state == "WAITING" || state == "NOT ROUTED" || state == "NO EVENTS";
}

int scale_for(std::string_view text, int target_px, int max_width)
{
    int chosen = 1;
    for (int scale = 1; scale <= 40; ++scale) {
        if (text_px_height(scale) > target_px) {
            break;
        }
        if (!text.empty() && text_px_width(text, scale) > max_width) {
            break;
        }
        chosen = scale;
    }
    return chosen;
}

uint16_t text_on(uint16_t background)
{
    return background >= 500 ? 64 : 940;
}

void paint_trend(LumaView view, const std::vector<double>& trend, int margin_x, int margin_y, uint16_t ink)
{
    if (trend.size() < 2) {
        return;
    }
    const int top = std::max(view.height * 2 / 3, margin_y);
    const int bottom = view.height - margin_y;
    const int mid = top + std::max(1, (bottom - top) / 2);
    const int half = std::max(4, (bottom - top) / 2 - 4);
    const int x0_lim = margin_x;
    const int x1_lim = view.width - margin_x;
    for (int x = x0_lim; x < x1_lim; ++x) {
        if (mid >= top && mid < bottom) {
            view.data[static_cast<size_t>(mid) * static_cast<size_t>(view.stride) + static_cast<size_t>(x)] = ink;
        }
    }
    for (size_t i = 1; i < trend.size(); ++i) {
        const int x0 = x0_lim + static_cast<int>((i - 1) * static_cast<size_t>(std::max(1, x1_lim - x0_lim - 1)) / (trend.size() - 1));
        const int x1 = x0_lim + static_cast<int>(i * static_cast<size_t>(std::max(1, x1_lim - x0_lim - 1)) / (trend.size() - 1));
        const auto y_of = [&](double ms) {
            const double clamped = std::max(-100.0, std::min(100.0, ms));
            return mid - static_cast<int>(std::lround(clamped / 100.0 * half));
        };
        const int y0 = y_of(trend[i - 1]);
        const int y1 = y_of(trend[i]);
        const int steps = std::max(1, std::abs(x1 - x0));
        for (int step = 0; step <= steps; ++step) {
            const int x = x0 + (x1 - x0) * step / steps;
            const int y = y0 + (y1 - y0) * step / steps;
            if (x >= x0_lim && x < x1_lim && y >= top && y < bottom) {
                view.data[static_cast<size_t>(y) * static_cast<size_t>(view.stride) + static_cast<size_t>(x)] = ink;
            }
        }
    }
}

} // namespace

void paint_result_picture(Yuv422p10& frame, const ResultPicture& picture)
{
    uint16_t y = 180;
    uint16_t cb = 512;
    uint16_t cr = 512;
    const bool full = full_picture_state(picture.state) || picture.number.empty();
    if (!full && picture.grade == "pass") {
        y = 520;
        cb = 360;
        cr = 400;
    } else if (!full && picture.grade == "warn") {
        y = 640;
        cb = 420;
        cr = 640;
    } else if (!full && picture.grade == "fail") {
        y = 420;
        cb = 480;
        cr = 720;
    }
    std::fill(frame.y.begin(), frame.y.end(), y);
    std::fill(frame.cb.begin(), frame.cb.end(), cb);
    std::fill(frame.cr.begin(), frame.cr.end(), cr);
    if (frame.width < 20 || frame.height < 20) {
        return;
    }
    const int margin_x = std::max(1, frame.width / 20);
    const int margin_y = std::max(1, frame.height / 20);
    const uint16_t ink = text_on(y);
    LumaView view{frame.y.data(), frame.width, frame.height, frame.width};
    const std::string headline = full ? picture.state : picture.number;
    const int number_px = std::max(8, frame.height * 32 / 100);
    const int number_scale = scale_for(headline, number_px, frame.width - 2 * margin_x);
    const int number_w = text_px_width(headline, number_scale);
    const int number_h = text_px_height(number_scale);
    const int number_x = std::max(margin_x, (frame.width - number_w) / 2);
    const int number_y = std::max(margin_y, frame.height * 32 / 100 - number_h / 2);
    draw_text(view, number_x, number_y, headline, number_scale, ink);
    int next_y = number_y + number_h + std::max(4, frame.height / 80);
    if (!full && !picture.sentence.empty()) {
        const int sentence_px = std::max(8, frame.height * 65 / 1000);
        const int sentence_scale = scale_for(picture.sentence, sentence_px, frame.width - 2 * margin_x);
        const int sentence_w = text_px_width(picture.sentence, sentence_scale);
        const int sentence_h = text_px_height(sentence_scale);
        const int sentence_x = std::max(margin_x, (frame.width - sentence_w) / 2);
        if (next_y + sentence_h < frame.height - margin_y) {
            draw_text(view, sentence_x, next_y, picture.sentence, sentence_scale, ink);
            next_y += sentence_h + 4;
        }
    }
    auto small = [&](const std::string& text, int x, int y) {
        if (text.empty() || y < margin_y || y + text_px_height(1) > frame.height - margin_y) {
            return;
        }
        draw_text(view, std::max(margin_x, x), y, text, 1, ink);
    };
    if (!full) {
        small(picture.state, margin_x, margin_y);
        if (!picture.age.empty()) {
            const int age_w = text_px_width(picture.age, 1);
            small(picture.age, frame.width - margin_x - age_w, margin_y);
        }
        const std::string latency = picture.latency.empty() ? "reference: unknown" : picture.latency;
        small(latency, margin_x, next_y);
        next_y += text_px_height(1) + 2;
        small(picture.senders, margin_x, next_y);
        paint_trend(view, picture.trend_ms, margin_x, margin_y, ink);
    }
    const std::string foot = picture.stamp + (picture.count.empty() ? "" : "  n=" + picture.count);
    small(foot, margin_x, frame.height - margin_y - text_px_height(1));
}

} // namespace avsync
