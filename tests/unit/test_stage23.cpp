#include "doctest.h"

#include "avsync/avsync.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace avsync;

namespace {

void render_picture(std::vector<uint16_t>& luma, int w, int h, uint64_t seq, int pos, int period, bool white)
{
    luma.assign(static_cast<size_t>(w * h), static_cast<uint16_t>(kYBackground));
    LumaView view{luma.data(), w, h, w};
    draw_static_layer(view);
    draw_timing(view, seq, pos, period, white);
}

void place(std::vector<uint16_t>& canvas, int cw, int ch, const std::vector<uint16_t>& src, int sw, int sh, int ox, int oy, int scale)
{
    canvas.assign(static_cast<size_t>(cw * ch), 180);
    for (int y = 0; y < sh * scale; ++y) {
        for (int x = 0; x < sw * scale; ++x) {
            const int dx = ox + x;
            const int dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) {
                continue;
            }
            canvas[static_cast<size_t>(dy * cw + dx)] = src[static_cast<size_t>((y / scale) * sw + (x / scale))];
        }
    }
}

} // namespace

TEST_CASE("text reads left to right and the sync dot is white only at zero")
{
    auto span = [](std::string_view word) {
        const int width = std::max(8, text_px_width(word, 3));
        const int height = std::max(8, text_px_height(3));
        std::vector<uint16_t> luma(static_cast<size_t>(width * height), 0);
        LumaView view{luma.data(), width, height, width};
        draw_text(view, 0, 0, word, 3, kYWhite);
        int left = width;
        int right = -1;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (luma[static_cast<size_t>(y * width + x)] != kYWhite) {
                    continue;
                }
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        return std::pair<int, int>{left, right};
    };
    const auto tee = span("T");
    const auto tee_dot = span("T.");
    CHECK(tee.second >= tee.first);
    // The added mark of "T." sits to the right of "T".
    CHECK(tee_dot.second > tee.second);
    CHECK(tee_dot.first <= tee.first + 1);
    CHECK(sync_dot_kind(-20) == SyncDot::Early);
    CHECK(sync_dot_kind(0) == SyncDot::Zero);
    CHECK(sync_dot_kind(15) == SyncDot::Late);
}

TEST_CASE("fiducials locate a scaled and offset tile")
{
    constexpr int sw = 320;
    constexpr int sh = 180;
    std::vector<uint16_t> src;
    render_picture(src, sw, sh, 7, 3, 25, true);
    const auto direct = read_marker(src.data(), sw, sh, sw);
    CHECK(direct.strip_ok);
    CHECK(direct.seq_low24 == 7);
    CHECK(direct.state == MarkerReading::State::White);

    std::vector<uint16_t> canvas;
    place(canvas, 640, 400, src, sw, sh, 36, 24, 1);
    const auto pose = locate_picture(canvas.data(), 640, 400, 640);
    CHECK(pose.found);
    if (pose.found) {
        CHECK(pose.origin_x == doctest::Approx(36).epsilon(0.15));
        CHECK(pose.origin_y == doctest::Approx(24).epsilon(0.15));
        CHECK(pose.width == doctest::Approx(sw).epsilon(0.08));
    }
    const auto located = read_located_marker(canvas.data(), 640, 400, 640);
    CHECK(located.located);
    CHECK(located.reading.strip_ok);
    CHECK(located.reading.seq_low24 == 7);
    CHECK(located.reading.state == MarkerReading::State::White);

    std::vector<uint16_t> scaled;
    place(scaled, 800, 500, src, sw, sh, 40, 30, 2);
    const auto scaled_read = read_located_marker(scaled.data(), 800, 500, 800);
    CHECK(scaled_read.located);
    CHECK(scaled_read.reading.seq_low24 == 7);
    CHECK(scaled_read.reading.state == MarkerReading::State::White);
}

TEST_CASE("a picture with no fiducials is not a tile")
{
    std::vector<uint16_t> blank(200 * 120, 400);
    CHECK_FALSE(locate_picture(blank.data(), 200, 120, 200).found);
}

TEST_CASE("rolling shutter row time and calibration expiry")
{
    CHECK(marker_row_time_ms(1000, 0, 10, 100) == doctest::Approx(1000));
    CHECK(marker_row_time_ms(1000, 10, 0, 101) == doctest::Approx(1000));
    CHECK(marker_row_time_ms(1000, 10, 50, 101) == doctest::Approx(1005));
    CalibrationRecord record;
    record.model = "test-phone";
    record.os_version = "1";
    record.events = 20;
    record.offset_ms = 12;
    record.spread_ms = 0.4;
    record.calibrated_unix_ms = 1'000'000;
    const int64_t day = 24LL * 60 * 60 * 1000;
    CHECK(calibration_state(&record, "1", record.calibrated_unix_ms + 10 * day) == CalibrationState::Valid);
    CHECK(calibration_state(&record, "1", record.calibrated_unix_ms + 91 * day) == CalibrationState::Expired);
    CHECK(calibration_state(&record, "2", record.calibrated_unix_ms + day) == CalibrationState::Expired);
    CHECK(calibration_state(nullptr, "1", 0) == CalibrationState::Missing);
}

TEST_CASE("phone session subtracts a valid calibration and withholds the grade colour otherwise")
{
    constexpr int w = 320;
    constexpr int h = 180;
    constexpr int frames = 80;
    constexpr int period = 25;
    PhoneSession session(2, GradeWindows{});
    std::vector<float> audio(static_cast<size_t>(frames * 20 * 48), 0.f);
    ChirpSpec spec;
    for (int frame = 0; frame < frames; ++frame) {
        const uint64_t seq = static_cast<uint64_t>(1 + frame / period);
        const bool white = (seq % 2) == 1;
        std::vector<uint16_t> src;
        render_picture(src, w, h, seq, frame % period, period, white);
        std::vector<uint16_t> canvas;
        place(canvas, 480, 280, src, w, h, 20, 16, 1);
        session.push_video(canvas.data(), 480, 280, 480, frame * 20.0, 0);
        if (frame % period == 0) {
            PlannedEvent event;
            event.chirp_id = static_cast<int>(seq % 4);
            event.audio_centre_sample = (frame * 20.0 + 20.0) * 48.0;
            std::vector<PlannedEvent> events{event};
            synthesise_block(spec, events, 0, audio.data(), audio.size());
        }
    }
    // The loop above overwrites the buffer each event. Rebuild all chirps together.
    std::vector<PlannedEvent> events;
    for (int frame = 0; frame < frames; frame += period) {
        const uint64_t seq = static_cast<uint64_t>(1 + frame / period);
        PlannedEvent event;
        event.chirp_id = static_cast<int>(seq % 4);
        event.audio_centre_sample = (frame * 20.0 + 20.0) * 48.0;
        events.push_back(event);
    }
    std::fill(audio.begin(), audio.end(), 0.f);
    synthesise_block(spec, events, 0, audio.data(), audio.size());
    session.push_audio(audio.data(), audio.size(), 0, 48000);
    const auto raw = session.finish("PHONE");
    CHECK(raw.calibration == "missing");
    CHECK_FALSE(raw.show_grade_colour);
    CHECK_FALSE(raw.events.empty());
    if (!raw.events.empty() && raw.events.front().av_offset_ms) {
        CHECK(*raw.events.front().av_offset_ms == doctest::Approx(20.0).epsilon(0.05));
        CHECK(std::find(raw.events.front().flags.begin(), raw.events.front().flags.end(), "uncalibrated") != raw.events.front().flags.end());
    }

    PhoneSession calibrated(2, GradeWindows{});
    CalibrationRecord record;
    record.model = "test-phone";
    record.os_version = "17";
    record.events = 30;
    record.offset_ms = 20;
    record.spread_ms = 0.3;
    record.calibrated_unix_ms = 10'000;
    calibrated.set_calibration(record, "17", 20'000);
    for (int frame = 0; frame < frames; ++frame) {
        const uint64_t seq = static_cast<uint64_t>(1 + frame / period);
        const bool white = (seq % 2) == 1;
        std::vector<uint16_t> src;
        render_picture(src, w, h, seq, frame % period, period, white);
        calibrated.push_video(src.data(), w, h, w, frame * 20.0, 0);
    }
    calibrated.push_audio(audio.data(), audio.size(), 0, 48000);
    const auto corrected = calibrated.finish("PHONE");
    CHECK(corrected.calibration == "valid");
    CHECK(corrected.show_grade_colour);
    REQUIRE_FALSE(corrected.events.empty());
    REQUIRE(corrected.events.front().av_offset_ms);
    CHECK(*corrected.events.front().av_offset_ms == doctest::Approx(0.0).epsilon(0.05));
    CHECK(corrected.events.front().grade == "pass");
}

TEST_CASE("sync square steps one frame at a time across ±240 ms at 1080p50")
{
    const auto zero = sync_place(0, 25, 500);
    CHECK(zero.scale_ms == doctest::Approx(240));
    CHECK(zero.position_ms == doctest::Approx(0));
    CHECK(sync_dot_kind(zero.position_ms) == SyncDot::Zero);
    const auto late = sync_place(12, 25, 500);
    CHECK(late.position_ms == doctest::Approx(240));
    CHECK(sync_dot_kind(late.position_ms) == SyncDot::Late);
    const auto early = sync_place(13, 25, 500);
    CHECK(early.position_ms == doctest::Approx(-240));
    CHECK(sync_dot_kind(early.position_ms) == SyncDot::Early);
    const auto near = sync_place(24, 25, 500);
    CHECK(near.position_ms == doctest::Approx(-20));
    CHECK(sync_dot_kind(near.position_ms) == SyncDot::Early);
    CHECK(sync_place(1, 25, 500).position_ms == doctest::Approx(20));
    int changes = 0;
    double previous = zero.position_ms;
    for (int frame = 1; frame < 25; ++frame) {
        const double position = sync_place(frame, 25, 500).position_ms;
        CHECK(std::abs(position) <= 240.0 + 1e-6);
        CHECK(std::abs(std::fmod(position, 20.0)) < 1e-6);
        if (std::abs(position - previous) > 1.0) {
            ++changes;
        }
        previous = position;
    }
    CHECK(changes == 24);
    CHECK(sync_place(0, 50, 1000).scale_ms == doctest::Approx(500));
    const auto fast = sync_place(6, 12, 240);
    CHECK(fast.scale_ms == doctest::Approx(120));
    CHECK(fast.position_ms == doctest::Approx(120));

    const auto geo = frame_geometry(1920, 1080);
    CHECK(geo.qr_size >= 1080 * 25 / 100);
    CHECK(geo.qr_x >= geo.fid_size);
    CHECK(geo.qr_x + geo.qr_size <= geo.marker_x - 4);
    CHECK(geo.qr_y + geo.qr_size <= geo.sweep_y - 4);
}
