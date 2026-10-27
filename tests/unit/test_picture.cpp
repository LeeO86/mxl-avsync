#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "picture.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace avsync;

TEST_CASE("full signal stays readable with lifted camera blacks and mild optical softening") {
    Yuv422p10 picture; picture.resize(1280,720);
    QrPayload payload; payload.test_id="camera-exposure"; payload.k=17; payload.tk=1000;
    PictureInfo info; info.qr_json=qr_payload_json(payload); info.title="TEST SIGNAL";
    info.source="Optical fixture"; info.subtitle="Exposure and softening"; info.period_ms=500;
    render_picture(picture,info,17,0,25,true);
    for(bool soften:{false,true}) {
        constexpr int width=960,height=540,left=80,top=40;
        std::vector<uint16_t> raw(width*height,300), pixels;
        for(int y=0;y<360;++y)for(int x=0;x<640;++x)
            raw[(y+top)*width+x+left]=uint16_t(170+picture.y[(y*2)*1280+x*2]*0.65);
        pixels=raw;
        if(soften)for(int y=1;y<height-1;++y)for(int x=1;x<width-1;++x){
            int sum=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)sum+=raw[(y+dy)*width+x+dx];
            pixels[y*width+x]=uint16_t(sum/9);
        }
        const auto located=read_located_marker(pixels.data(),width,height,width);
        REQUIRE(located.located);
        CHECK(std::abs(located.pose.origin_x-left)<=6);
        CHECK(std::abs(located.pose.origin_y-top)<=6);
        CHECK(located.reading.strip_ok);
        CHECK(located.reading.seq_low24==17);
        CHECK(located.reading.state==MarkerReading::State::White);
    }
}

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

TEST_CASE("locator keeps the full rendered tile corners instead of QR and text squares")
{
    PictureInfo info;
    info.qr_json = R"({"schema":"mxl-avsync-qr/1","id":"TEST-stage2","seq":7,"tk":1000000000,"offset_ms":0,"format":"720p50","source":"STUDIO A","title":"AV sync","event_rate":2})";
    info.title = "AV sync";
    info.source = "STUDIO A";
    info.format_line = "720p50";
    info.offset_line = "offset 0 ms";
    info.counter = "frame 0000050";
    info.time_line = "t 1000.0 ms";
    info.period_ms = 500;
    for (uint64_t sequence : {1, 7, 8}) {
        const bool white = sequence % 2 != 0;
        Yuv422p10 frame;
        frame.resize(1280, 720);
        render_picture(frame, info, sequence, 0, 25, white);
        // Same 640x360 tile and (80,40) pad as stage2.sh, with deterministic
        // 2x2 averaging here. The integration test also covers ProRes encoding.
        std::vector<uint16_t> canvas(960 * 540, kYBlack);
        for (int y = 0; y < 360; ++y) {
            for (int x = 0; x < 640; ++x) {
                const size_t p = size_t(y * 2) * frame.width + x * 2;
                canvas[size_t(y + 40) * 960 + x + 80] =
                    (unsigned(frame.y[p]) + frame.y[p + 1] + frame.y[p + frame.width] + frame.y[p + frame.width + 1]) / 4;
            }
        }
        const auto located = read_located_marker(canvas.data(), 960, 540, 960);
        REQUIRE(located.located);
        CHECK(std::abs(located.pose.origin_x - 80) <= 4);
        CHECK(std::abs(located.pose.origin_y - 40) <= 4);
        CHECK(std::abs(located.pose.width - 640) <= 5);
        CHECK(std::abs(located.pose.height - 360) <= 5);
        const double expected[4][2] = {{80, 40}, {720, 40}, {720, 400}, {80, 400}};
        for (int corner = 0; corner < 4; ++corner) {
            CHECK(std::abs(located.pose.corner_x[corner] - expected[corner][0]) <= 4);
            CHECK(std::abs(located.pose.corner_y[corner] - expected[corner][1]) <= 4);
        }
        CHECK(located.width == 640);
        CHECK(located.height == 360);
        CHECK(located.reading.strip_ok);
        CHECK(located.reading.seq_low24 == sequence);
        CHECK(located.reading.state == (white ? MarkerReading::State::White : MarkerReading::State::Black));
    }
}

TEST_CASE("coded QR survives a scaled tile outside the canvas QR region")
{
    QrPayload payload;
    payload.test_id = "TEST-stage2";
    payload.mode = "FILE";
    payload.k = 1;
    payload.tk = 1000000000;
    payload.format = "720p50";
    payload.rate = 1;
    payload.timecode = true;
    payload.source_id = 1880207804;
    const auto json = qr_payload_json(payload);
    PictureInfo info;
    info.qr_json = json;
    Yuv422p10 frame;
    frame.resize(1280, 720);
    render_picture(frame, info, 1, 0, 50, true);
    std::vector<uint16_t> canvas(960 * 540, kYBlack);
    for (int y = 0; y < 360; ++y) {
        for (int x = 0; x < 640; ++x) {
            const size_t p = size_t(y * 2) * frame.width + x * 2;
            canvas[size_t(y + 40) * 960 + x + 80] =
                (unsigned(frame.y[p]) + frame.y[p + 1] + frame.y[p + frame.width] + frame.y[p + frame.width + 1]) / 4;
        }
    }
    const auto decoded = decode_qr_text(canvas.data(), 960, 540, 960);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == json);
    const auto parsed = parse_qr_payload(*decoded);
    REQUIRE(parsed.has_value());
    CHECK(parsed->timecode);
    CHECK(parsed->source_id == payload.source_id);
    CHECK(parsed->rate == 1);
}

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
    // Antialiased fringes approach the background; require strong foreground ink.
    int strongest = 0;
    for (int y = 0; y < pass.height; ++y) {
        for (int x = 0; x < pass.width; ++x) {
            const uint16_t sample = pass.y[static_cast<size_t>(y) * static_cast<size_t>(pass.width) + static_cast<size_t>(x)];
            const bool edge = x < margin_x || y < margin_y || x >= pass.width - margin_x || y >= pass.height - margin_y;
            if (edge && sample != background) {
                border = false;
            }
            if (!edge && sample != background) {
                ink = true;
                strongest = std::max(strongest, std::abs(static_cast<int>(sample) - static_cast<int>(background)));
            }
        }
    }
    CHECK(border);
    CHECK(ink);
    CHECK(strongest >= 300);
    for (const char* grade : {"warn", "fail", "invalid"}) {
        ResultPicture pictured = base;
        pictured.grade = grade;
        if (std::string(grade) == "invalid") {
            pictured.number.clear();
            pictured.state = "NO EVENTS";
        }
        const Yuv422p10 frame = paint(pictured);
        const uint16_t fill = frame.y[0];
        int gap = 0;
        for (uint16_t sample : frame.y) {
            if (sample != fill) {
                gap = std::max(gap, std::abs(static_cast<int>(sample) - static_cast<int>(fill)));
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
        // The first complete row is the boxes' top padding. Maximizing black
        // runs can instead select holes inside antialiased glyphs below it.
        if (runs >= 7) {
            best_runs = runs;
            boxes = std::move(found);
            break;
        }
    }
    CHECK(best_runs >= 7);
    for (size_t i = 2; i + 1 < boxes.size(); i += 2) {
        CHECK(boxes[i] >= boxes[i - 1] + 4);
    }
}

#include "avsync/picture_reader.hpp"
TEST_CASE("live picture reader keeps direct path and reacquires a moving camera tile after loss") {
    PictureReader reader;
    CHECK_FALSE(reader.read(nullptr,16,16,16,0).located);
    Yuv422p10 picture;picture.resize(1280,720);
    PictureInfo info; info.title="TEST SIGNAL";
    render_picture(picture,info,5,0,30,true);
    auto direct=reader.read(picture.y.data(),1280,720,1280,0);
    REQUIRE(direct.located);CHECK(direct.canonical.empty());CHECK(direct.reading.seq_low24==5);
    constexpr int w=960,h=540;
    std::vector<uint16_t> tile(w*h,150);
    for (int step=0;step<4;++step) {
        std::fill(tile.begin(),tile.end(),150);
        if(step!=2) for(int y=0;y<360;++y)for(int x=0;x<640;++x)
            tile[(y+40+step*5)*w+x+80+step*8]=picture.y[y*2*1280+x*2];
        auto found=reader.read(tile.data(),w,h,w,1000+step*600);
        if(step==2) { CHECK_FALSE(found.located);continue; }
        REQUIRE(found.located);REQUIRE_FALSE(found.canonical.empty());
        CHECK(found.reading.strip_ok);CHECK(found.reading.seq_low24==5);
        CHECK(found.reading.state==MarkerReading::State::White);
    }
}

#include "avsync/phone_generator.hpp"
TEST_CASE("bounded optical search refines phone tile borders in 1080p and 4K cameras") {
    PhoneGeneratorPlan plan("linux-phone-1",1,1793000000000000000LL,1000);
    Yuv422p10 frame; frame.resize(1280,720);
    PictureInfo info; info.title="TEST SIGNAL"; info.source="Synthetic phone camera";
    info.qr_json=qr_payload_json(plan.qr(1)); info.period_ms=1000;
    render_picture(frame,info,1,0,60,true);
    for(int scale:{1,2}) {
        const int width=1920*scale, height=1080*scale;
        std::vector<uint16_t> camera(size_t(width)*height,240);
        for(int y=0;y<540*scale;++y)for(int x=0;x<960*scale;++x)
            camera[size_t(y+180*scale)*width+x+240*scale]=frame.y[size_t(y*4/(3*scale))*1280+x*4/(3*scale)];
        PictureReader reader;
        for(double now:{0.0,20.0,600.0}) {
            const auto found=reader.read(camera.data(),width,height,width,now);
            REQUIRE(found.located); REQUIRE_FALSE(found.canonical.empty());
            CHECK(found.reading.geometry_ok); CHECK(found.reading.strip_ok);
            CHECK(found.reading.seq_low24==1);
            CHECK(found.reading.state==MarkerReading::State::White);
        }
        std::fill(camera.begin(),camera.end(),240);
        CHECK_FALSE(reader.read(camera.data(),width,height,width,800).located);
    }
}
