#pragma once

#include "avsync/avsync.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace avsync {

struct PictureInfo {
    std::string title;
    std::string source;
    std::string notes;
    std::string format_line;
    std::string offset_line;
    std::string qr_json;
    std::string counter; // "frame as written by the generator 0000123"
    std::string time_line;
};

struct Yuv422p10 {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> y;
    std::vector<uint16_t> cb;
    std::vector<uint16_t> cr;
    void resize(int w, int h);
};

void render_picture(Yuv422p10& frame, const PictureInfo& info, uint64_t seq, int frame_in_period, int period_frames, bool marker_white);

// Decodes the QR in the top-left of a 10-bit luma plane. Empty if none.
std::optional<std::string> decode_qr_text(const uint16_t* luma, int width, int height, int stride);

} // namespace avsync
