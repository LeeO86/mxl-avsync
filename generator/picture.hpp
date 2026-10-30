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
    std::string subtitle;
    std::string notes;
    std::string format_line;
    std::string offset_line;
    std::string qr_json;
    std::string counter; // "frame as written by the generator 0000123"
    std::string time_line;
    double av_offset_ms = 0;
    double media_ms = 0;
    double period_ms = 500;
    bool show_sync_indicator = true; // Native display may animate this as a separate layer.
    const struct SourceStill* source_image = nullptr;
    const struct SourceStill* app_icon = nullptr;
};

struct SourceStill {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Decoded source pictures are kept small: each frame is scaled down at load to fit
// kSourceStillMaxSide (the drawn box is about frame height / 12, 90 px at 1080 lines), and the
// whole clip may hold at most kSourceClipMaxBytes of RGBA, so a tiny file cannot expand to GiB.
constexpr int kSourceStillMaxSide = 256;
constexpr size_t kSourceClipMaxBytes = 64u * 1024u * 1024u;
constexpr int kSourceClipMaxFrames = 600;
constexpr size_t kSourceImageMaxFileBytes = 16u * 1024u * 1024u;

struct SourceClip {
    std::vector<SourceStill> frames;
    std::vector<double> start_ms;
    double loop_ms = 0;
    bool over_limit = false; // load() refused the picture for its size, frame count or decoded bytes
    size_t decoded_bytes = 0;
    bool load(const std::string& path, std::string& error);
    // Frame of an animated GIF at this media time. A still always returns its only picture.
    const SourceStill* at(double media_ms) const;
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

std::optional<QrDetection> decode_qr_signal(const uint16_t* luma, int width, int height, int stride);

void render_sync_foreground(Yuv422p10& frame, int period_frames, double period_ms, bool qr_flash = true);

struct ResultPicture {
    std::string number;
    std::string sentence;
    std::string grade;
    std::string state;
    std::string age;
    std::vector<double> trend_ms;
    std::string latency = "reference: unknown";
    std::string senders;
    std::string stamp;
    std::string count;
};

void paint_result_picture(Yuv422p10& frame, const ResultPicture& picture);

} // namespace avsync
