#include "avsync/avsync.hpp"
#include "doctest.h"
#include <algorithm>
#include <cmath>

using namespace avsync;

TEST_CASE("QR preserves exact TAI identity and flash tracking emits only rising events") {
    QrPayload qr;
    qr.test_id = "exact-time";
    qr.mode = "LIVE";
    qr.timecode = true;
    qr.tk = 1792963237123456789LL;
    qr.source_id = 0xf1234567;
    const auto parsed = parse_qr_payload(qr_payload_json(qr));
    REQUIRE(parsed);
    CHECK(parsed->tk == qr.tk);
    CHECK(parsed->source_id == qr.source_id);
    CHECK(parsed->timecode);
    for (int rate : {1, 2}) {
        auto format = *parse_video_format("540p25");
        MarkerTracker tracker(format, rate);
        tracker.configure(rate, true);
        const auto period = tracker.period();
        std::vector<uint16_t> pixels(format.width * format.height);
        int events = 0;
        for (int frame = 0; frame < period.grains * 3; ++frame) {
            LumaView view{pixels.data(), format.width, format.height, format.width};
            fill_luma(view, kYBackground);
            draw_static_layer(view);
            const int position = frame % period.grains;
            draw_timing(view, 1 + frame / period.grains, position, period.grains,
                        flash_white(position, 40));
            const auto observed = tracker.push(pixels.data(), format.width, frame, frame * 40.0);
            if (observed.event) {
                ++events;
                CHECK(position == 0);
            }
        }
        CHECK(events == 3);
    }
}

TEST_CASE("compact timecode has exact protected source identity and bounded quantization") {
    constexpr int64_t video = 1792963237123456789LL;
    for (bool live : {false, true})
        for (int64_t offset : {0LL, -35000000LL, 35000000LL, 1400000000LL}) {
            auto wire = encode_timecode(0x01020304, video, offset, live);
            CHECK(wire.size() == (offset == 0 ? 15 : 17));
            AudioTimecode p;
            REQUIRE(parse_timecode(wire, p));
            CHECK(p.source_id == 0x01020304);
            CHECK(p.live == live);
            CHECK(p.onset);
            CHECK(std::abs(p.video_send_ns - video) <= 7630);
            CHECK(std::abs(p.audio_send_ns - video - offset) <= 38880);
            wire[6] ^= 1;
            CHECK_FALSE(parse_timecode(wire, p));
        }
    CHECK(timecode_duration_ms(17, 1200) == doctest::Approx(330));
    CHECK(timecode_duration_ms(15, 1200) == doctest::Approx(303.333333333));
    CHECK_THROWS(encode_timecode(1, -1, 0, true));
    CHECK_THROWS(encode_timecode(1, video, 2100000000LL, true));
}

TEST_CASE("onset timecode decodes independently at both baud rates and both event rates") {
    for (int baud : {600, 1200})
        for (int rate : {1, 2}) {
            if (baud == 600 && rate == 2)
                continue;
            std::vector<float> signal(48000 * 3, 0);
            for (int i = 0; i < 2; ++i) {
                const auto bytes =
                    encode_timecode(0x01020304, 1700000037000000000LL + i * 1000000000LL / rate,
                                    i ? 35000000 : 0, true);
                const auto wave = timecode_waveform(bytes, baud);
                std::copy(wave.begin(), wave.end(), signal.begin() + 4800 + i * 48000 / rate);
            }
            auto decoded = decode_timecodes(signal.data(), signal.size(), 48000, 1234);
            REQUIRE(decoded.size() == 2);
            for (int i = 0; i < 2; ++i) {
                CHECK(decoded[i].baud == baud);
                CHECK(decoded[i].source_id == 0x01020304);
                CHECK(std::abs(decoded[i].time_ms - (1334 + i * 1000.0 / rate)) < 0.12);
                CHECK(decoded[i].audio_send_ns ==
                      quantize_timecode_ns(1700000037000000000LL + i * 1000000000LL / rate) +
                          (i ? 35000000 : 0));
            }
        }
}

TEST_CASE("stream decoder remembers chirp arrival instead of telegram completion") {
    const auto wave =
        timecode_waveform(encode_timecode(123, 1700000037000000000LL, 35000000, true), 1200);
    std::vector<float> signal(48000, 0);
    std::copy(wave.begin(), wave.end(), signal.begin() + 4800);
    AudioTimecodeStream decoder;
    for (size_t at = 0; at < signal.size(); at += 480) {
        const auto n = std::min(size_t(480), signal.size() - at);
        decoder.push(signal.data() + at, n, 48000, at / 48.0, 9000 + at / 48.0);
        if (at < 4800 + wave.size() - 480)
            CHECK(decoder.scan().empty());
    }
    auto decoded = decoder.scan(true);
    REQUIRE(decoded.size() == 1);
    CHECK(std::abs(decoded[0].time_ms - 100) < 0.12);
    CHECK(std::abs(decoded[0].arrival_ms - 9100) < 0.12);
}

TEST_CASE(
    "timecode identity pairs beyond the old repeating chirp window and rejects other sources") {
    VideoObservation video;
    video.event = true;
    video.seq = 9;
    video.time_ms = 1000;
    video.quality = "good";
    video.timecode = true;
    video.source_id = 123;
    video.event_time_ns = 1700000037000000000LL;
    AudioObservation audio;
    audio.timecode = true;
    audio.source_id = 456;
    audio.event_time_ns = video.event_time_ns;
    audio.audio_send_ns = video.event_time_ns;
    audio.time_ms = 4217;
    audio.quality = "good";
    Pairer pairer(500, {}, {}, "LIVE", "test");
    pairer.observe_video(video, 0);
    pairer.observe_audio(audio);
    CHECK(pairer.poll(5000).empty());
    audio.source_id = 123;
    pairer.observe_audio(audio);
    auto results = pairer.poll(5000);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].av_offset_ms);
    CHECK(*results[0].av_offset_ms == doctest::Approx(3217));
    CHECK(results[0].audio_timecode);
}

TEST_CASE("packet scheduling is block invariant and reports a collision") {
    PlanConfig config;
    config.video = *parse_video_format("1080p50");
    config.timecode_baud = 1200;
    config.duration_ms = 3000;
    config.source_id = 123;
    const auto plan = make_file_plan(config);
    std::vector<float> one(144000), split(one.size());
    AudioTimecodeWriter a, b;
    a.write(plan.events, 0, one.data(), one.size(), 1200);
    for (size_t at = 0; at < split.size(); at += 997)
        b.write(plan.events, at, split.data() + at, std::min(size_t(997), split.size() - at), 1200);
    CHECK(one == split);
    CHECK(a.skipped() == 0);
    CHECK(b.skipped() == 0);
    auto collision = plan.events;
    REQUIRE(collision.size() >= 2);
    collision[1].audio_sample = collision[0].audio_sample + 100;
    AudioTimecodeWriter writer;
    writer.write(collision, 0, one.data(), one.size(), 1200);
    CHECK(writer.skipped() == 1);
}
