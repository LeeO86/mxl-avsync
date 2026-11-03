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
    CHECK(late.y[size_t(geometry.sweep_y-25)*late.width+at_late-10] == static_cast<uint16_t>(145 * 4));
    CHECK(early.y[size_t(geometry.sweep_y-25)*early.width+at_early-10] == static_cast<uint16_t>(81 * 4));
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

#include "avsync/phone_analyzer.hpp"
#include "avsync/phone_generator.hpp"
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

TEST_CASE("mobile static plate omits only the shared animated sync square") {
    for(int rate:{1,2}) {
        PictureInfo info;info.period_ms=1000.0/rate;
        const int period=60/rate;
        for(int pos:{0,1,period-1}) {
            Yuv422p10 animated,plate;animated.resize(1280,720);plate.resize(1280,720);
            render_picture(animated,info,9,pos,period,optical_a_white(pos,2,9));
            info.show_sync_indicator=false;
            render_picture(plate,info,9,pos,period,optical_a_white(pos,2,9));
            info.show_sync_indicator=true;
            const auto dot=sync_indicator(1280,720,pos,period,info.period_ms);
            CHECK(animated.y[(dot.y+dot.side/2)*1280+dot.x+dot.side/2]==kYWhite); // foreground tick wins
            CHECK(animated.y[dot.y*1280+dot.x+dot.side/2]==kYBlack); // centerline extends beyond tick
            CHECK(animated.y[dot.y*1280+dot.x+dot.side/4]==dot.y10);
            if(pos==0)CHECK(dot.kind==SyncDot::Zero);
            else if(pos==1)CHECK(dot.kind==SyncDot::Late);
            else CHECK(dot.kind==SyncDot::Early);
            bool changed=false,outside=false;
            for(int y=0;y<720;++y)for(int x=0;x<1280;++x) {
                const bool differs=animated.y[y*1280+x]!=plate.y[y*1280+x] || animated.cb[y*640+x/2]!=plate.cb[y*640+x/2] || animated.cr[y*640+x/2]!=plate.cr[y*640+x/2];
                changed|=differs;
                // 4:2:2 chroma shares each horizontal pair.
                if(differs&&(x<dot.x-1||x>=dot.x+dot.side+1||y<dot.y||y>=dot.y+dot.side))outside=true;
            }
            CHECK(changed);CHECK_FALSE(outside);
            CHECK(read_marker(plate.y.data(),1280,720,1280).strip_ok);
        }
    }
}

namespace {
// One AVS3 grain as the live generator draws it: QR names the nearest event.
QrPayload avs3_payload(uint64_t index, int grains, int rate, const char* format, uint32_t source = 0xABCDEF12)
{
    QrPayload p;p.timecode=p.qr_flash=p.optical_code=true;p.source_id=source;p.test_id="avs3";p.mode="LIVE";
    p.k=optical_named_seq(index,grains);p.tk=1793300000000000000LL+int64_t(p.k)*(1000000000LL/rate);p.rate=rate;p.format=format;
    return p;
}
void render_avs3(Yuv422p10& image, uint64_t index, int grains, int rate, int chip, const char* format, uint32_t source = 0xABCDEF12)
{
    const auto p=avs3_payload(index,grains,rate,format,source);
    PictureInfo info;info.qr_json=qr_payload_text(p);info.period_ms=1000.0/rate;
    const uint64_t seq=index/grains;const int pos=int(index%grains);
    render_picture(image,info,seq,pos,grains,optical_a_white(pos,chip,seq));
}
} // namespace

TEST_CASE("AVS3 QR payload round trip and burst schedule") {
    QrPayload p=avs3_payload(37,25,2,"1080p50");
    const auto text=qr_payload_text(p);CHECK(text.substr(0,5)=="AVS3:");CHECK(text.size()==59);
    auto back=parse_qr_payload(text);REQUIRE(back);CHECK(back->optical_code);CHECK(back->k==p.k);CHECK(back->tk==p.tk);
    // The QR names event k from half a period before its burst.
    CHECK(optical_named_seq(37,25)==1);CHECK(optical_named_seq(38,25)==2);CHECK(optical_named_seq(62,25)==2);CHECK(optical_named_seq(63,25)==3);
    for(int rate:{1,2})for(double fps:{25.0,50.0,59.94,60.0}) {
        const int chip=optical_chip_grains(fps,rate);const double period=1000.0/rate,chip_ms=chip*1000/fps;
        CHECK(chip_ms>=33);CHECK(optical_span_chips()*chip_ms<period-150); // a pause after every burst
        for(uint64_t seq:{4u,5u}) {
            int changes=0;bool last=false;
            for(int pos=0;pos<int(period*fps/1000);++pos){const bool w=optical_a_white(pos,chip,seq);changes+=w!=last;last=w;}
            CHECK(changes==kOpticalTransitions);CHECK_FALSE(last); // back to rest: A black, B white
        }
    }
    CHECK(optical_chip_grains(50,2)==2);CHECK(optical_chip_grains(25,2)==1);CHECK(optical_chip_grains(50,1)==4);
}

TEST_CASE("AVS3 QR alone anchors a cropped picture and times bursts on the grain") {
    for(int rate:{1,2}) {
        QrFlashTracker tracker;
        const auto g=qr_flash_geometry(1280,720);
        // Crop away all outside corners, bars and the bottom strip.
        const int left=g.qr_x-12,top=g.qr_y-12,cw=g.marker_x+g.marker_side-left+12,ch=g.qr_size+24;
        const int grains=50/rate,chip=optical_chip_grains(50,rate);
        std::vector<uint16_t> crop(size_t(cw)*ch);
        std::vector<VideoObservation> events;
        Yuv422p10 image;image.resize(1280,720);
        for(int frame=0;frame<grains*5;++frame) {
            const double time=frame*20.0;
            render_avs3(image,frame,grains,rate,chip,"1080p50");
            for(int y=0;y<ch;++y)std::copy_n(image.y.data()+size_t(y+top)*1280+left,cw,crop.data()+size_t(y)*cw);
            if(frame%5==0) {
                auto q=decode_qr_signal(crop.data(),cw,ch,cw);REQUIRE(q);REQUIRE(q->payload.optical_code);
                CHECK(q->payload.k==optical_named_seq(frame,grains));tracker.observe(*q,time);
            }
            VideoObservation f;f.time_ms=time;f.have_arrival=true;f.arrival_ms=time+7;f.grain_index=frame;
            tracker.push(crop.data(),cw,ch,cw,f);
            auto out=tracker.take_events();events.insert(events.end(),out.begin(),out.end());
            CHECK(tracker.valid_frame());
        }
        // The first period only fills the field history; every later burst is found.
        REQUIRE(events.size()>=3);
        for(const auto& e:events) {
            CHECK(e.time_ms==doctest::Approx(double(e.seq)*1000.0/rate).epsilon(1e-9));
            CHECK(e.arrival_ms==e.time_ms+7);CHECK(e.grain_index==int64_t(e.seq)*grains);CHECK(e.timecode);
            CHECK(e.set_tai_ns==1793300000000000000LL+int64_t(e.seq)*(1000000000LL/rate));
        }
        CHECK(events.back().seq==4);
    }
}

TEST_CASE("AVS3 rejects static pictures and expired geometry") {
    QrFlashTracker tracker;Yuv422p10 image;image.resize(1280,720);
    render_avs3(image,7*25+20,25,2,2,"1080p50"); // rest state, no burst
    auto q=decode_qr_signal(image.y.data(),1280,720,1280);REQUIRE(q);tracker.observe(*q,0);
    for(int frame=0;frame<40;++frame){VideoObservation f;f.time_ms=frame*20;tracker.push(image.y.data(),1280,720,1280,f);CHECK(tracker.take_events().empty());}
    CHECK_FALSE(tracker.valid_frame()); // 780-ms-old QR geometry cannot keep measuring.
}

TEST_CASE("AVS3 never names a burst with a QR of the wrong parity or another source") {
    for(int mode=0;mode<2;++mode) {
        QrFlashTracker tracker;Yuv422p10 image;image.resize(1280,720);size_t named=0;
        for(int frame=0;frame<25*4;++frame) {
            render_avs3(image,frame,25,2,2,"1080p50");
            if(frame%5==0) {
                auto q=decode_qr_signal(image.y.data(),1280,720,1280);REQUIRE(q);
                // 0: a stuck QR shows event 1 forever. 1: the QR is one event ahead.
                if(mode==0){q->payload.k=1;}else{q->payload.k+=1;}
                tracker.observe(*q,frame*20.0);
            }
            VideoObservation f;f.time_ms=frame*20.0;tracker.push(image.y.data(),1280,720,1280,f);
            // Never a wrong name: burst k sits at k periods.
            for(const auto& e:tracker.take_events()){++named;CHECK(double(e.seq)==doctest::Approx(e.time_ms/500));}
        }
        // A stuck QR names its own burst, then the grid counts on; a shifted QR has the other parity: nothing.
        CHECK(named==(mode==0?3u:0u));
        CHECK(tracker.diagnostics().optical_bursts>=2);
    }
}

TEST_CASE("AVS3 through a simulated 240 fps camera: each burst within half a millisecond") {
    // Source 50p, the camera integrates the shown frames over a full-frame exposure
    // at a free-running 239.48 fps; truth is the grain time of transition 0.
    const int grains=25,chip=optical_chip_grains(50,2),w=640,h=360;
    std::vector<Yuv422p10> frames(grains*5);
    for(size_t n=0;n<frames.size();++n){frames[n].resize(w,h);render_avs3(frames[n],n,grains,2,chip,"720p50");}
    QrFlashTracker tracker;std::vector<double> err;
    const double cam=1000/239.48;std::vector<uint16_t> shot(size_t(w)*h);
    for(double t=1.3;t+cam<frames.size()*20.0-1;t+=cam) {
        // Mean of the source frames weighted by their overlap with [t, t+cam).
        std::vector<double> acc(shot.size(),0);
        for(size_t n=size_t(t/20);n<=size_t((t+cam)/20)&&n<frames.size();++n) {
            const double o=std::min(t+cam,(n+1)*20.0)-std::max(t,n*20.0);if(o<=0)continue;
            for(size_t i=0;i<shot.size();++i)acc[i]+=o*frames[n].y[i];
        }
        for(size_t i=0;i<shot.size();++i)shot[i]=uint16_t(std::lround(acc[i]/cam));
        static int count=0;
        if(count++%24==0){auto q=decode_qr_signal(shot.data(),w,h,w);if(q)tracker.observe(*q,t);}
        VideoObservation f;f.time_ms=t;f.exposure_ms=cam;tracker.push(shot.data(),w,h,w,f);
        for(const auto& e:tracker.take_events())err.push_back(e.time_ms-double(e.seq)*500);
    }
    REQUIRE(err.size()>=3);
    for(double e:err)CHECK(std::abs(e)<.5);
}

TEST_CASE("phone analyzer end to end: 60 Hz phone source, 240 fps camera, +20 ms audio") {
    // Source: the phone generator plan (720p60, 2 events/s). Video reaches the camera 30 ms after
    // its scheduled time, audio 50 ms after: AV +20 ms, latencies 30 / 50 ms on a true clock.
    const int64_t epoch_tai=1793300000000000000LL;const double epoch_host=100;
    PhoneGeneratorPlan plan("phone-e2e",2,epoch_tai,epoch_host);
    PhoneAnalyzer analyzer;
    analyzer.set_device_utc_offset(double(epoch_tai)/1e6-epoch_host*1000-37000);
    const int w=640,h=360;Yuv422p10 frame;frame.resize(1280,720);
    // Render each 60 Hz source frame once, downscaled into the camera picture.
    std::vector<std::vector<uint8_t>> shown;
    const int frames=60*6;
    for(int n=0;n<frames;++n) {
        PictureInfo info;info.period_ms=500;info.qr_json=qr_payload_text(plan.qr(plan.named_seq(n)));
        render_picture(frame,info,1+n/30,n%30,30,plan.a_white(n));
        std::vector<uint8_t> small(size_t(w)*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)small[size_t(y)*w+x]=uint8_t(frame.y[size_t(y*2)*1280+x*2]>>2);
        shown.push_back(std::move(small));
    }
    // Audio: every event's timecode waveform at host_seconds(seq) + 50 ms.
    const int rate=48000;std::vector<float> mic(size_t(rate*7),0.f);
    for(uint64_t seq=1;seq<=12;++seq){
        const auto wave=plan.audio(seq,rate);const size_t at=size_t(std::lround((plan.host_seconds(seq)-epoch_host+.050)*rate));
        for(size_t i=0;i<wave.size()&&at+i<mic.size();++i)mic[at+i]+=wave[i];
    }
    const double cam=1000/239.48,start=epoch_host*1000;size_t audio_at=0;int count=0;std::string last;
    std::vector<uint8_t> shot(size_t(w)*h);
    for(double t=start+3.1;t<start+frames*1000.0/60-40;t+=cam) {
        // Integrate the shown frames over [t, t+cam), with the 30 ms display delay.
        std::vector<double> acc(shot.size(),0);
        const double s0=t-start-30,s1=s0+cam;
        for(int n=std::max(0,int(std::floor(s0*60/1000)));n<=int(s1*60/1000)&&n<frames;++n){
            const double o=std::min(s1,(n+1)*1000.0/60)-std::max(s0,n*1000.0/60);if(o<=0)continue;
            for(size_t i=0;i<acc.size();++i)acc[i]+=o*shown[n][i];
        }
        if(s0<0)continue;
        for(size_t i=0;i<shot.size();++i)shot[i]=uint8_t(std::lround(acc[i]/cam));
        if(count++%24==0){std::vector<uint16_t> wide(shot.size());for(size_t i=0;i<shot.size();++i)wide[i]=uint16_t(shot[i]*4);if(auto q=decode_qr_signal(wide.data(),w,h,w))analyzer.observe_qr(qr_payload_text(q->payload),q->x,q->y,t);}
        analyzer.push_video(shot.data(),w,h,w,t,cam,0);
        // Feed microphone audio up to this time in 10 ms blocks.
        while(audio_at+480<=mic.size()&&start+(audio_at+480)*1000.0/rate<=t){analyzer.push_audio(mic.data()+audio_at,480,start+audio_at*1000.0/rate,rate);audio_at+=480;}
        if(analyzer.report_due()){auto copy=analyzer.snapshot_for_poll();auto text=copy.poll();analyzer.merge_audio(copy);if(!text.empty())last=text;}
    }
    INFO(last);
    REQUIRE(last.find("\"stage\":\"measuring\"")!=std::string::npos);
    auto value=[&](const char* key){const auto at=last.find(std::string("\"")+key+"\":");REQUIRE(at!=std::string::npos);return std::stod(last.substr(at+std::strlen(key)+3));};
    CHECK(std::abs(value("offsetMs")-20)<.6);
    CHECK(std::abs(value("videoLatencyMs")-30)<.6);
    CHECK(std::abs(value("audioLatencyMs")-50)<.3);
    CHECK(value("spreadMs")<1.0);
}

TEST_CASE("AVS2 brand is centered and the moving box passes below white foreground ticks") {
    PictureInfo info;QrPayload q;q.qr_flash=q.optical_code=q.timecode=true;q.test_id="layout";
    info.qr_json=qr_payload_text(q);info.source="Demo title";info.subtitle="Smaller subtitle";
    SourceStill icon;icon.width=icon.height=4;icon.rgba.resize(4*4*4);
    for(size_t i=0;i<icon.rgba.size();i+=4){icon.rgba[i]=255;icon.rgba[i+3]=255;}
    info.app_icon=&icon;
    for(int rate:{1,2})for(int pos:{0,1,60/rate-1}) {
        info.period_ms=1000.0/rate;Yuv422p10 frame,foreground;frame.resize(1280,720);foreground.resize(1280,720);
        render_picture(frame,info,8,pos,60/rate,false);
        render_sync_foreground(foreground,60/rate,info.period_ms,true);
        const auto dot=sync_indicator(1280,720,pos,60/rate,info.period_ms,true);
        int ticks_inside=0;
        for(int y=dot.y;y<dot.y+dot.side;++y)for(int x=dot.x;x<dot.x+dot.side;++x) {
            if(foreground.y[y*1280+x]==kYWhite){CHECK(frame.y[y*1280+x]==kYWhite);++ticks_inside;}
        }
        CHECK(ticks_inside>0);CHECK(frame.y[dot.y*1280+dot.x+dot.side/2]==kYBlack);
        const int icon_side=72,icon_top=18;
        CHECK(frame.cr[(icon_top+icon_side/2)*640+640/2]>800);
        CHECK(frame.cr[(icon_top+icon_side/2)*640+(640-icon_side/2-1)/2]<800);
        const auto decoded=decode_qr_signal(frame.y.data(),1280,720,1280);REQUIRE(decoded);CHECK(decoded->payload.qr_flash);
    }
}

TEST_CASE("live picture gives the same v210 as a full render on every grain")
{
    std::string error;
    SourceClip gif;
    REQUIRE(gif.load(fixture("blink.gif"), error));
    SourceClip png;
    REQUIRE(png.load(fixture("transparent.png"), error));
    struct Case {
        const char* format;
        int grains;
    };
    for (const Case c : {Case{"1080p50", 160}, Case{"1080i25", 120}, Case{"720p50", 80}, Case{"1080p59.94", 80}}) {
        const auto format = parse_video_format(c.format);
        REQUIRE(format);
        const Rate pace = format->grain_rate();
        const int rows = format->grain_height();
        const Period period = plan_period(*format, 2);
        LivePicture live;
        Yuv422p10 full;
        full.resize(format->width, rows);
        std::vector<uint8_t> packed(v210_line_bytes(format->width) * static_cast<size_t>(rows));
        int checked = 0;
        for (int n = 0; n < c.grains; ++n) {
            const uint64_t index = 90000000000ULL + static_cast<uint64_t>(n);
            const int fip = static_cast<int>(index % static_cast<uint64_t>(period.grains));
            const uint64_t seq = index / static_cast<uint64_t>(period.grains);
            const double ms = static_cast<double>(index_to_ns(pace, index)) / 1e6;
            PictureInfo info;
            info.format_line = format->token;
            info.period_ms = period.period_ms;
            // Settings change on the way: offset, title, subtitle, a PNG, then an animated GIF.
            info.offset_line = n < c.grains / 2 ? "offset 0 ms" : "offset 35 ms";
            if (n >= c.grains / 4) info.source = "Studio 3";
            if (n >= c.grains / 3) info.subtitle = "phone 1234";
            if (n >= c.grains / 2 && n < c.grains * 3 / 4) info.source_image = png.at(ms);
            if (n >= c.grains * 3 / 4) info.source_image = gif.at(ms);
            QrPayload payload;
            payload.timecode = payload.qr_flash = payload.optical_code = true;
            payload.mode = "LIVE";
            payload.test_id = "live-picture";
            payload.k = seq;
            payload.tk = index_to_ns(pace, seq * static_cast<uint64_t>(period.grains));
            payload.offset_ms = n < c.grains / 2 ? 0 : 35;
            payload.format = format->token;
            info.qr_json = qr_payload_text(payload);
            const bool white = optical_a_white(fip, optical_chip_grains(1000.0 * period.grains / period.period_ms, 2), seq);
            render_picture(full, info, seq, fip, period.grains, white);
            pack_v210(full.y.data(), full.cb.data(), full.cr.data(), format->width, rows, format->width, packed.data());
            const auto& incremental = live.render(format->width, rows, info, seq, fip, period.grains, white);
            CHECK_MESSAGE(incremental == packed, c.format << " grain " << n);
            ++checked;
        }
        CHECK(checked == c.grains);
    }
    // The legacy layout is drawn in full and must match too.
    const auto format = parse_video_format("1080p50");
    LivePicture live;
    Yuv422p10 full;
    full.resize(format->width, format->height);
    std::vector<uint8_t> packed(v210_line_bytes(format->width) * static_cast<size_t>(format->height));
    PictureInfo info;
    info.format_line = "1080p50";
    info.qr_json = "{\"s\":\"mxl-avsync/1\",\"k\":7}";
    for (int fip = 0; fip < 3; ++fip) {
        render_picture(full, info, 7, fip, 25, fip == 0);
        pack_v210(full.y.data(), full.cb.data(), full.cr.data(), format->width, format->height, format->width, packed.data());
        CHECK(live.render(format->width, format->height, info, 7, fip, 25, fip == 0) == packed);
    }
}

TEST_CASE("v210 region packing matches the full packer and touches only its groups")
{
    for (int width : {1920, 1280, 720, 1366, 64}) {
        const int height = 9;
        std::vector<uint16_t> y(static_cast<size_t>(width * height)), cb(static_cast<size_t>(width / 2 * height)), cr(cb.size());
        uint32_t seed = 3;
        auto next = [&] { seed = seed * 1103515245u + 12345u; return static_cast<uint16_t>((seed >> 8) & 0x3FF); };
        for (auto* plane : {&y, &cb, &cr})
            for (auto& v : *plane) v = next();
        std::vector<uint8_t> full(v210_line_bytes(width) * height, 0xAB), region(full.size(), 0xAB);
        pack_v210(y.data(), cb.data(), cr.data(), width, height, width, full.data());
        pack_v210_region(y.data(), cb.data(), cr.data(), width, width, 0, width, 0, height, region.data());
        CHECK(full == region);
        std::vector<uint8_t> part(full.size(), 0xAB);
        pack_v210_region(y.data(), cb.data(), cr.data(), width, width, 12, 30, 2, 5, part.data());
        for (int row = 0; row < height; ++row)
            for (size_t b = 0; b < v210_line_bytes(width); ++b) {
                const size_t i = static_cast<size_t>(row) * v210_line_bytes(width) + b;
                const bool inside = row >= 2 && row < 5 && b >= 2 * 16 && b < 5 * 16 && b < static_cast<size_t>((width + 5) / 6 * 16);
                CHECK((inside ? part[i] == full[i] : part[i] == 0xAB));
            }
    }
}
