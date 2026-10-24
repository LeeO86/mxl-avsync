#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "picture.hpp"

#include <cmath>
#include <string>

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
}
