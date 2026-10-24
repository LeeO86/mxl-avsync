#include "picture.hpp"

#include "qrcodegen.hpp"
#include "quirc.h"

#include <algorithm>
#include <cstring>

namespace avsync {
namespace {

void fill_u16(std::vector<uint16_t>& plane, uint16_t value)
{
    std::fill(plane.begin(), plane.end(), value);
}

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

void render_picture(Yuv422p10& frame, const PictureInfo& info, uint64_t seq, int frame_in_period, int period_frames, bool marker_white)
{
    fill_u16(frame.y, kYBackground);
    fill_u16(frame.cb, kYNeutralChroma);
    fill_u16(frame.cr, kYNeutralChroma);
    LumaView view{frame.y.data(), frame.width, frame.height, frame.width};
    draw_static_layer(view);
    draw_timing(view, seq, frame_in_period, period_frames, marker_white);
    const int scale = std::max(1, frame.height / 360);
    draw_text(view, frame.width / 4, scale * 4, "TEST SIGNAL - mxl-avsync", scale, kYWhite);
    draw_text(view, frame.width / 3, frame.height - scale * 14, "TEST SIGNAL", scale, kYWhite);
    int text_x = frame_geometry(frame.width, frame.height).qr_x + frame_geometry(frame.width, frame.height).qr_size + scale * 4;
    int text_y = scale * 16;
    auto line = [&](const std::string& text) {
        if (!text.empty()) {
            draw_text(view, text_x, text_y, text, std::max(1, scale - 1), kYWhite);
            text_y += 10 * std::max(1, scale - 1);
        }
    };
    line(info.title);
    line(info.source);
    line(info.format_line);
    line(info.offset_line);
    line(info.notes);
    line(info.counter);
    line(info.time_line);
    const auto geo = frame_geometry(frame.width, frame.height);
    draw_text(view, geo.marker_x + geo.marker_side + scale * 4, geo.marker_y, info.counter, std::max(1, scale - 1), kYWhite);
    draw_text(view, geo.marker_x + geo.marker_side + scale * 4, geo.marker_y + 12 * scale, info.time_line, std::max(1, scale - 1), kYWhite);
    blit_qr(view, info.qr_json);
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
