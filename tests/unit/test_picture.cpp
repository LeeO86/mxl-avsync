#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "picture.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace avsync;

namespace {

std::string fixture(const char* name)
{
    return std::string(AVSYNC_FIXTURE_DIR) + "/" + name;
}

int square_x(int frame, int period, double period_ms, const FrameGeometry& geometry)
{
    const SyncPlace place = sync_place(frame, period, period_ms);
    const int span = std::max(1, geometry.sweep_x1 - geometry.sweep_x0);
    const double along = (place.position_ms + place.scale_ms) / (2.0 * place.scale_ms);
    return geometry.sweep_x0 + static_cast<int>(std::lround(along * (span - 1)));
}

} // namespace

TEST_CASE("result picture states")
{
    auto ink_rows = [](const Yuv422p10& frame, int y0, int y1, uint16_t background) {
        int rows = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                if (frame.y[static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)] != background) {
                    ++rows;
                    break;
                }
            }
        }
        return rows;
    };
    auto ink_span = [](const Yuv422p10& frame, int y0, int y1, uint16_t background) {
        int left = frame.width;
        int right = -1;
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                if (frame.y[static_cast<size_t>(y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)] != background) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
        }
        return std::pair<int, int>{left, right};
    };
    for (const char* state : {"NO SIGNAL", "WAITING", "NOT ROUTED", "NO EVENTS"}) {
        Yuv422p10 frame;
        frame.resize(640, 360);
        ResultPicture picture;
        picture.state = state;
        picture.grade = "invalid";
        picture.number.clear();
        paint_result_picture(frame, picture);
        const uint16_t background = frame.y[0];
        const int band0 = 360 * 20 / 100;
        const int band1 = 360 * 45 / 100;
        CHECK(ink_rows(frame, band0, band1, background) > 8);
        const auto span = ink_span(frame, band0, band1, background);
        const int mid = (span.first + span.second) / 2;
        CHECK(std::abs(mid - 320) < 80);
    }
    Yuv422p10 live;
    live.resize(640, 360);
    ResultPicture picture;
    picture.number = "+35.0 ms";
    picture.sentence = "audio 35 ms after video";
    picture.grade = "warn";
    picture.state = "RUNNING";
    picture.age = "2 s";
    picture.trend_ms = {0, 10, 35, 35, 20};
    paint_result_picture(live, picture);
    const uint16_t background = live.y[0];
    const int number0 = 360 * 20 / 100;
    const int number1 = 360 * 45 / 100;
    CHECK(ink_rows(live, number0, number1, background) > 8);
    const auto number = ink_span(live, number0, number1, background);
    CHECK(std::abs((number.first + number.second) / 2 - 320) < 80);
    CHECK(ink_rows(live, number1, 360 * 60 / 100, background) > 2);
    CHECK(ink_rows(live, 360 * 2 / 3, 360 - 18, background) > 2);
}

TEST_CASE("result picture contrast, margin, latency, senders and count")
{
    auto paint = [](const ResultPicture& picture) {
        Yuv422p10 frame;
        frame.resize(640, 360);
        paint_result_picture(frame, picture);
        return frame;
    };
    auto differs = [](const Yuv422p10& left, const Yuv422p10& right) {
        return left.y != right.y;
    };
    ResultPicture base;
    base.grade = "pass";
    base.state = "RUNNING";
    base.number = "+0.0 ms";
    base.sentence = "in sync";
    base.latency = "reference: unknown";
    base.senders = "Cam A / Mic 1";
    base.stamp = "1000";
    base.count = "7";
    const Yuv422p10 pass = paint(base);
    const int margin_x = 640 / 20;
    const int margin_y = 360 / 20;
    const uint16_t background = pass.y[0];
    bool border = true;
    bool ink = false;
    int worst = 10000;
    for (int y = 0; y < pass.height; ++y) {
        for (int x = 0; x < pass.width; ++x) {
            const uint16_t sample = pass.y[static_cast<size_t>(y) * static_cast<size_t>(pass.width) + static_cast<size_t>(x)];
            const bool edge = x < margin_x || y < margin_y || x >= pass.width - margin_x || y >= pass.height - margin_y;
            if (edge && sample != background) {
                border = false;
            }
            if (!edge && sample != background) {
                ink = true;
                worst = std::min(worst, std::abs(static_cast<int>(sample) - static_cast<int>(background)));
            }
        }
    }
    CHECK(border);
    CHECK(ink);
    CHECK(worst >= 300);
    for (const char* grade : {"warn", "fail", "invalid"}) {
        ResultPicture pictured = base;
        pictured.grade = grade;
        if (std::string(grade) == "invalid") {
            pictured.number.clear();
            pictured.state = "NO EVENTS";
        }
        const Yuv422p10 frame = paint(pictured);
        const uint16_t fill = frame.y[0];
        int gap = 10000;
        for (uint16_t sample : frame.y) {
            if (sample != fill) {
                gap = std::min(gap, std::abs(static_cast<int>(sample) - static_cast<int>(fill)));
            }
        }
        CHECK(gap >= 300);
    }
    ResultPicture latency = base;
    latency.latency = "video 12 audio 14";
    CHECK(differs(pass, paint(latency)));
    ResultPicture unlabeled = base;
    unlabeled.senders.clear();
    CHECK(differs(pass, paint(unlabeled)));
    ResultPicture counted = base;
    counted.count = "8";
    counted.stamp = "1001";
    CHECK(differs(pass, paint(counted)));
}

TEST_CASE("source stills keep transparency and gif frames follow time")
{
    std::string error;
    SourceClip still;
    CHECK(still.load(fixture("transparent.png"), error));
    CHECK(still.frames.size() == 1);
    bool clear = false;
    bool solid = false;
    const auto& rgba = still.frames[0].rgba;
    for (size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] == 0) {
            clear = true;
        }
        if (rgba[i] == 255) {
            solid = true;
        }
    }
    CHECK(clear);
    CHECK(solid);

    SourceClip gif;
    CHECK(gif.load(fixture("blink.gif"), error));
    CHECK(gif.frames.size() >= 2);
    const SourceStill* first = gif.at(0);
    const SourceStill* later = gif.at(gif.loop_ms * 0.75);
    REQUIRE(first != nullptr);
    REQUIRE(later != nullptr);
    CHECK(first->rgba != later->rgba);
}

TEST_CASE("large qr stays decodable and the sync square moves")
{
    QrPayload payload;
    payload.test_id = "TEST-IDENT";
    payload.k = 4;
    payload.tk = 1000;
    payload.format = "1080p50";
    const std::string json = qr_payload_json(payload);
    std::string error;
    SourceClip clip;
    REQUIRE(clip.load(fixture("transparent.png"), error));

    auto draw = [&](int frame_in_period) {
        Yuv422p10 frame;
        frame.resize(1920, 1080);
        PictureInfo info;
        info.qr_json = json;
        info.source = "STUDIO A";
        info.title = "AV sync";
        info.format_line = "1080p50";
        info.offset_line = "offset 0 ms";
        info.period_ms = 500;
        info.counter = "frame as written by the generator 0000050";
        info.time_line = "t   1000.0 ms";
        info.source_image = clip.at(0);
        render_picture(frame, info, 4, frame_in_period, 25, true);
        return frame;
    };
    const Yuv422p10 zero = draw(0);
    const Yuv422p10 late = draw(12);
    const Yuv422p10 early = draw(13);
    const auto text = decode_qr_text(zero.y.data(), zero.width, zero.height, zero.width);
    CHECK(text.has_value());
    if (text) {
        CHECK(*text == json);
    }
    const MarkerReading reading = read_marker(zero.y.data(), zero.width, zero.height, zero.width);
    CHECK(reading.strip_ok);
    CHECK(reading.seq_low24 == 4);
    CHECK(reading.state == MarkerReading::State::White);

    const FrameGeometry geometry = frame_geometry(1920, 1080);
    const int at_zero = square_x(0, 25, 500, geometry);
    const int at_late = square_x(12, 25, 500, geometry);
    const int at_early = square_x(13, 25, 500, geometry);
    CHECK(at_late > at_zero);
    CHECK(at_early < at_zero);
    CHECK(at_late - at_early > 400);
    const auto sample = [&](const Yuv422p10& frame, int x) {
        return frame.y[static_cast<size_t>(geometry.sweep_y) * static_cast<size_t>(frame.width) + static_cast<size_t>(x)];
    };
    CHECK(sample(zero, at_zero) == kYWhite);
    CHECK(sample(late, at_late) == static_cast<uint16_t>(145 * 4));
    CHECK(sample(early, at_early) == static_cast<uint16_t>(81 * 4));
    CHECK(zero.cb[static_cast<size_t>(geometry.sweep_y) * static_cast<size_t>(zero.width / 2) + static_cast<size_t>(at_zero / 2)] == kYNeutralChroma);
    CHECK(zero.y[static_cast<size_t>(geometry.sweep_y - 18) * static_cast<size_t>(zero.width) + static_cast<size_t>(at_zero)] == kYWhite);

    // A tick every 20 ms across ±250, on a row the centre square does not cover.
    const int tick_row = geometry.sweep_y - 8;
    int tick_runs = 0;
    bool in_tick = false;
    for (int x = geometry.sweep_x0; x < geometry.sweep_x1; ++x) {
        const bool on = zero.y[static_cast<size_t>(tick_row) * static_cast<size_t>(zero.width) + static_cast<size_t>(x)] == kYWhite;
        if (on && !in_tick) {
            ++tick_runs;
        }
        in_tick = on;
    }
    CHECK(tick_runs >= 20);

    // Label boxes sit on the black pad above the glyphs and must not touch.
    std::vector<int> boxes;
    int best_runs = 0;
    for (int row = geometry.sweep_y - 70; row < geometry.sweep_y - 30; ++row) {
        std::vector<int> found;
        bool in_box = false;
        int box_start = 0;
        for (int x = geometry.sweep_x0; x <= geometry.sweep_x1; ++x) {
            const bool on = x < geometry.sweep_x1
                && zero.y[static_cast<size_t>(row) * static_cast<size_t>(zero.width) + static_cast<size_t>(x)] == kYBlack;
            if (on && !in_box) {
                box_start = x;
                in_box = true;
            } else if (!on && in_box) {
                if (x - box_start > 8) {
                    found.push_back(box_start);
                    found.push_back(x);
                }
                in_box = false;
            }
        }
        const int runs = static_cast<int>(found.size() / 2);
        if (runs > best_runs) {
            best_runs = runs;
            boxes = std::move(found);
        }
    }
    CHECK(best_runs >= 7);
    for (size_t i = 2; i + 1 < boxes.size(); i += 2) {
        CHECK(boxes[i] >= boxes[i - 1] + 4);
    }
}
