#include "picture.hpp"

#include "qrcodegen.hpp"
#include "quirc.h"

#include <algorithm>
#include <cstring>

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

void draw_sync_scale(Yuv422p10& frame, double av_offset_ms)
{
    const FrameGeometry g = frame_geometry(frame.width, frame.height);
    const int span = std::max(1, g.sweep_x1 - g.sweep_x0);
    const int track_h = std::max(4, frame.height / 180);
    fill_yuv(frame, g.sweep_x0, g.sweep_y - track_h / 2, span, track_h, Y10{kYBlack, kYNeutralChroma, kYNeutralChroma});
    const int centre = g.sweep_x0 + span / 2;
    fill_yuv(frame, centre - 1, g.sweep_y - track_h, 3, track_h * 3, Y10{kYWhite, kYNeutralChroma, kYNeutralChroma});
    constexpr double kScaleMs = 40.0;
    const double clamped = std::clamp(av_offset_ms, -kScaleMs, kScaleMs);
    const double along = (clamped + kScaleMs) / (2.0 * kScaleMs);
    const int dot = g.sweep_x0 + static_cast<int>(std::lround(along * (span - 1)));
    const int radius = std::max(8, frame.height / 60);
    Y10 color{kYWhite, kYNeutralChroma, kYNeutralChroma};
    const SyncDot kind = sync_dot_kind(av_offset_ms);
    if (kind == SyncDot::Early) {
        color = y8(81, 90, 240);
    } else if (kind == SyncDot::Late) {
        color = y8(145, 54, 34);
    }
    fill_yuv(frame, dot - radius, g.sweep_y - radius, radius * 2, radius * 2, color);
    const int label_scale = std::max(2, frame.height / 540);
    const int label_y = g.sweep_y + radius + 4;
    const char* ticks[] = {"-40", "-20", "0", "+20", "+40"};
    for (int i = 0; i < 5; ++i) {
        const int tx = g.sweep_x0 + span * i / 4;
        const int tw = text_px_width(ticks[i], label_scale);
        draw_text_box(frame, tx - tw / 2, label_y, ticks[i], label_scale);
    }
    draw_text_box(frame, g.sweep_x0, label_y + text_px_height(label_scale) + 8, "early", label_scale);
    const int late_w = text_px_width("late", label_scale) + std::max(2, label_scale) * 2;
    draw_text_box(frame, g.sweep_x1 - late_w, label_y + text_px_height(label_scale) + 8, "late", label_scale);
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
    const std::string banner = "TEST SIGNAL - mxl-avsync";
    const int banner_scale = scale;
    const int banner_w = text_px_width(banner, banner_scale) + std::max(2, banner_scale) * 2;
    place((frame.width - banner_w) / 2, 16, banner, banner_scale);
    const std::string footer = "TEST SIGNAL";
    const int footer_w = text_px_width(footer, banner_scale) + std::max(2, banner_scale) * 2;
    const int footer_h = text_px_height(banner_scale) + std::max(2, banner_scale) * 2;
    place((frame.width - footer_w) / 2, geo.strip_y - footer_h - 8, footer, banner_scale);
    int text_x = geo.qr_x + geo.qr_size + scale * 4;
    int text_y = scale * 16;
    auto line = [&](const std::string& text) {
        if (text.empty()) {
            return;
        }
        place(text_x, text_y, text, small);
        text_y += text_px_height(small) + std::max(2, small) * 2 + 4;
    };
    line(info.title);
    line(info.source);
    line(info.format_line);
    line(info.offset_line);
    line(info.notes);
    line(info.counter);
    line(info.time_line);
    place(geo.marker_x + geo.marker_side + scale * 4, geo.marker_y, info.counter, small);
    place(geo.marker_x + geo.marker_side + scale * 4, geo.marker_y + text_px_height(small) + 12, info.time_line, small);
    blit_qr(view, info.qr_json);
    const int qr_span = geo.qr_size + scale * 2;
    chroma_neutral(frame, geo.qr_x, geo.qr_y, qr_span, qr_span);
    draw_sync_scale(frame, info.av_offset_ms);
    (void)period_frames;
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
