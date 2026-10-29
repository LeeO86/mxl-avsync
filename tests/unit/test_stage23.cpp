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

#include "avsync/clock.hpp"

TEST_CASE("kernel clock maps to sync_state")
{
    KernelClock clock;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unknown");
    clock.read_ok = true;
    clock.unsynced = false;
    clock.esterror_us = 20;
    clock.tai_offset_s = 37;
    auto state = classify_kernel_clock(clock, 100);
    CHECK(state.sync_state == "locked");
    CHECK(state.accuracy_ns == 20000);
    clock.esterror_us = 500;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unlocked");
    clock.esterror_us = 20;
    clock.tai_offset_s = 0;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unlocked");
    clock.tai_offset_s = 37;
    clock.unsynced = true;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unlocked");
}


TEST_CASE("text preserves antialiased coverage, contrast and clipped row padding")
{
    constexpr int width = 96, height = 64, stride = 104;
    for (const uint16_t foreground : {uint16_t(kYWhite), uint16_t(kYBlack)}) {
        const uint16_t background = foreground == kYWhite ? kYBlack : kYWhite;
        std::vector<uint16_t> pixels((height + 2) * stride, 1023);
        uint16_t* start = pixels.data() + stride + 3;
        for (int y = 0; y < height; ++y) std::fill(start + y * stride, start + y * stride + width, background);
        draw_text({start, width, height, stride}, -4, -2, "S", 6, foreground);
        int solid = 0, fringe = 0, untouched = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const uint16_t value = start[y * stride + x];
                CHECK(value >= kYBlack);
                CHECK(value <= kYWhite);
                solid += value == foreground;
                untouched += value == background;
                fringe += value != foreground && value != background;
            }
        }
        CHECK(solid > 100);
        CHECK(fringe > 100);
        CHECK(untouched > 100);
        bool padding_unchanged = true;
        for (int y = 0; y < height + 2; ++y) for (int x = 0; x < stride; ++x) {
            if (y == 0 || y == height + 1 || x < 3 || x >= width + 3)
                padding_unchanged &= pixels[y * stride + x] == 1023;
        }
        CHECK(padding_unchanged);
    }
}

TEST_CASE("kernel unreported PTP errors are unknown accuracy, not a 16 second estimate")
{
    KernelClock clock;
    clock.read_ok = true;
    clock.unsynced = false;
    clock.tai_offset_s = 37;
    clock.esterror_us = clock.maxerror_us = 16000000;
    auto state = classify_kernel_clock(clock, 100);
    CHECK(state.sync_state == "locked");
    CHECK(state.accuracy_ns == -1);
    CHECK(state.reason == "error not reported (PTP discipline)");
    clock.unsynced = true;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unlocked");
    clock.unsynced = false;
    clock.tai_offset_s = 0;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unlocked");
    CHECK(classify_kernel_clock(clock, 100).reason == "TAI offset is not set");
    clock.tai_offset_s = 37;
    clock.maxerror_us = 1000; // only one sentinel must not bypass the NTP limit
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unlocked");
    clock.esterror_us = 200;
    clock.maxerror_us = 16000000;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "unlocked");
    CHECK(classify_kernel_clock(clock, 100).accuracy_ns == 200000);
    clock.esterror_us = 100;
    CHECK(classify_kernel_clock(clock, 100).sync_state == "locked");
    CHECK(classify_kernel_clock(clock, 100).accuracy_ns == 100000);
}

TEST_CASE("timing field tolerates a sparse cursor without accepting mixed flashes")
{
    constexpr int w=640,h=360;
    const auto g=frame_geometry(w,h);
    for(bool white:{false,true}){
        std::vector<uint16_t> pixels;
        render_picture(pixels,w,h,9,0,25,white);
        const auto expected=white?MarkerReading::State::White:MarkerReading::State::Black;
        REQUIRE(read_marker(pixels.data(),w,h,w).state==expected);
        // Well below 5% of the measured field; full-range rejection used to
        // invalidate every frame even though over 99% agreed on its phase.
        for(int y=h/2-3;y<h/2+3;++y)for(int x=w/2-3;x<w/2+3;++x)
            pixels[y*w+x]=white?kYBlack:kYWhite;
        auto reading=read_marker(pixels.data(),w,h,w);
        CHECK(reading.strip_ok);
        CHECK(reading.state==expected);
        CHECK(reading.quality!="invalid");
        CHECK(reading.seq_low24==9);
        // A real mixed/rolling transition remains invalid. No timing may be
        // taken from this half-white/half-black field.
        const int inset=g.marker_side/4;
        for(int y=g.marker_y+inset;y<g.marker_y+g.marker_side-inset;++y)
            for(int x=g.marker_x+inset;x<w/2;++x)pixels[y*w+x]=white?kYBlack:kYWhite;
        CHECK(read_marker(pixels.data(),w,h,w).state==MarkerReading::State::Invalid);
    }
}

TEST_CASE("compact phone timing retains the optical onset while barcode parity recovers")
{
    constexpr int w=320,h=180;
    QrPayload qr;qr.mode="LIVE";qr.timecode=true;qr.source_id=77;qr.k=1;
    qr.tk=1700000037100000000LL;qr.rate=2;qr.format="1080p50";
    PhoneSession clean(2,{}),delayed(2,{}),no_edge(2,{}),skipped(2,{}),bad_jump(2,{});
    clean.observe_signal(qr);delayed.observe_signal(qr);no_edge.observe_signal(qr);skipped.observe_signal(qr);bad_jump.observe_signal(qr);
    std::vector<double> clean_times,delayed_times;
    std::vector<uint64_t> skipped_seqs;uint64_t last_skipped=0;
    uint64_t last_clean=0,last_delayed=0;
    for(int frame=0;frame<400;++frame){
        const int event_frame=frame-20;
        const int position=event_frame<0?99:event_frame%100;
        const uint64_t seq=event_frame<0?0:1+event_frame/100;
        const bool white=event_frame>=0&&position<20;
        std::vector<uint16_t> pixels;render_picture(pixels,w,h,seq,position/4,25,white);
        clean.push_video(pixels.data(),w,h,w,frame*5.0,0);
        if(!(seq==3&&white))skipped.push_video(pixels.data(),w,h,w,frame*5.0,0);
        if(auto event=skipped.latest_video_event();event&&event->seq!=last_skipped){skipped_seqs.push_back(event->seq);last_skipped=event->seq;CHECK(event->time_ms==100+(event->seq-1)*500);}
        std::vector<uint16_t> wrong;render_picture(wrong,w,h,seq==2?3:seq,position/4,25,white);
        bad_jump.push_video(wrong.data(),w,h,w,frame*5.0,0);
        if(auto event=bad_jump.latest_video_event())CHECK(event->seq!=3); // +2 after only one period must fail.
        // Lose only barcode parity for 60ms after the optical edge. Phase and
        // geometry stay visible; validation still requires three valid frames.
        if(white&&position<12){
            auto g=frame_geometry(w,h);
            for(int y=g.strip_y;y<g.strip_y+g.strip_h;++y)
                for(int x=g.strip_x+28*g.strip_w;x<g.strip_x+29*g.strip_w;++x)
                    pixels[y*w+x]=pixels[y*w+x]>500?kYBlack:kYWhite;
        }
        delayed.push_video(pixels.data(),w,h,w,frame*5.0,0);
        if(white)no_edge.push_video(pixels.data(),w,h,w,frame*5.0,0);
        if(auto event=clean.latest_video_event();event&&event->seq!=last_clean){clean_times.push_back(event->time_ms);last_clean=event->seq;}
        if(auto event=delayed.latest_video_event();event&&event->seq!=last_delayed){delayed_times.push_back(event->time_ms);last_delayed=event->seq;}
    }
    REQUIRE(clean_times.size()==3);REQUIRE(delayed_times.size()==3);
    for(size_t i=0;i<3;++i){CHECK(clean_times[i]==600+i*500);CHECK(delayed_times[i]==clean_times[i]);}
    CHECK_FALSE(no_edge.latest_video_event()); // No black/white edge may be invented.
    REQUIRE(skipped_seqs.size()==2);CHECK(skipped_seqs[0]==2);CHECK(skipped_seqs[1]==4);
    CHECK(bad_jump.finish().timing_resets>=1);
}

TEST_CASE("first compact QR discards speculative legacy sequence lock")
{
    PhoneSession session(2,{});
    std::vector<uint16_t> pixels;render_picture(pixels,320,180,9,0,25,true);
    for(int n=0;n<3;++n)session.push_video(pixels.data(),320,180,320,n*5.0,0);
    REQUIRE(session.finish().locked_seq);
    QrPayload qr;qr.timecode=true;qr.source_id=77;
    session.observe_signal(qr);
    CHECK_FALSE(session.finish().locked_seq);
}

TEST_CASE("compact picture lock cannot be acquired from a parity-valid code outside QR identity")
{
    PhoneSession session(2,{});
    QrPayload qr;qr.mode="LIVE";qr.timecode=true;qr.source_id=77;
    qr.k=100;qr.tk=1700000037000000000LL;qr.rate=2;
    session.observe_signal(qr);
    std::vector<uint16_t> pixels;render_picture(pixels,320,180,9000000,0,25,true);
    for(int n=0;n<12;++n)session.push_video(pixels.data(),320,180,320,n*5.0,0);
    const auto rejected=session.finish();
    CHECK_FALSE(rejected.locked_seq);CHECK(rejected.video_events==0);
    CHECK(rejected.sequence_rejected_frames>=3);
    render_picture(pixels,320,180,100,0,25,true);
    for(int n=12;n<15;++n)session.push_video(pixels.data(),320,180,320,n*5.0,0);
    REQUIRE(session.finish().locked_seq);CHECK(*session.finish().locked_seq==100);
}
