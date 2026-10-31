#include "avsync/avsync.hpp"
#include "doctest.h"
#include <algorithm>
#include <cmath>

using namespace avsync;

TEST_CASE("signal reconfiguration discards decoded history before legacy reacquisition") {
    QrPayload qr; qr.test_id="source"; qr.timecode=true;
    PhoneSession phone(2,{}); phone.observe_signal(qr);
    auto wave=timecode_waveform(encode_timecode(123,1700000037000000000LL,0,true),1200);
    std::vector<float> received(48000,0);
    std::copy(wave.begin(),wave.end(),received.begin()+4800);
    phone.push_audio(received.data(),received.size(),0,48000);
    REQUIRE(phone.finish().audio_timecodes.size()==1);
    qr.timecode=false; phone.observe_signal(qr);
    auto empty=phone.finish(); CHECK(empty.audio_timecodes.empty()); CHECK(empty.audio_samples==0);
    std::vector<PlannedEvent> events(1); events[0].chirp_id=1; events[0].audio_centre_sample=4800;
    std::vector<float> legacy(24000); synthesise_block({},events,0,legacy.data(),legacy.size());
    phone.push_audio(legacy.data(),legacy.size(),1000,48000);
    auto recovered=phone.finish(); CHECK(recovered.audio_timecodes.empty()); CHECK(recovered.audio_hits.size()==1);
}

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

#include "avsync/phone_generator.hpp"
TEST_CASE("phone generator uses one scheduled clock for QR, flash and independently decoded audio") {
    constexpr int64_t epoch = 1792963237123456789LL;
    for (int rate : {1,2}) {
        PhoneGeneratorPlan plan("phone-test",rate,epoch,100.125);
        CHECK(plan.frame_at(100.125)==0);
        CHECK(plan.frame_at(100.125+1.0/60)==1);
        CHECK(plan.frame_at(100.125+1.0/rate)==60/rate);
        std::vector<float> stream(48000*3);
        for (int i=0;i<3;++i) {
            const auto qr=plan.qr(i+1);
            CHECK(qr.estimated_source); CHECK(qr.format=="720p60");
            auto parsed=parse_qr_payload(qr_payload_json(qr));
            REQUIRE(parsed); CHECK(parsed->estimated_source);
            CHECK(qr.tk==epoch+int64_t(i)*1000000000/rate);
            CHECK(plan.host_seconds(i+1)==doctest::Approx(100.125+double(i)/rate));
            auto audio=plan.audio(i+1);
            std::copy(audio.begin(),audio.end(),stream.begin()+4800+i*48000/rate);
            VideoObservation video; video.seq=i+1;
            REQUIRE(identify_video(video,qr));
            CHECK(video.estimated_source);
            CHECK(video.event_time_ns==quantize_timecode_ns(qr.tk));
        }
        const auto decoded=decode_timecodes(stream.data(),stream.size(),48000);
        REQUIRE(decoded.size()==3);
        for(int i=0;i<3;++i) {
            CHECK(decoded[i].estimated_source); CHECK(decoded[i].live);
            CHECK(decoded[i].time_ms==doctest::Approx(100+1000.0*i/rate).epsilon(.0001));
            CHECK(decoded[i].video_send_ns==quantize_timecode_ns(plan.qr(i+1).tk));
            CHECK(decoded[i].audio_send_ns==decoded[i].video_send_ns);
        }
    }
    // Phone-clock identity must survive the telephone-band codec too.
    for (bool alaw : {false,true}) {
        PhoneGeneratorPlan phone("phone-g711",2,epoch,100);
        std::vector<float> channel(8000*2,0);
        for(int i=0;i<3;++i) {
            const auto wave=phone.audio(i+1,8000);
            std::copy(wave.begin(),wave.end(),channel.begin()+800+i*4000);
        }
        const auto bytes=alaw ? encode_alaw(channel.data(),channel.size()) : encode_ulaw(channel.data(),channel.size());
        if(alaw) decode_alaw(bytes.data(),bytes.size(),channel.data());
        else decode_ulaw(bytes.data(),bytes.size(),channel.data());
        const auto heard=decode_timecodes(channel.data(),channel.size(),8000);
        REQUIRE(heard.size()==3);
        for(int i=0;i<3;++i) { CHECK(heard[i].estimated_source); CHECK(heard[i].video_send_ns==quantize_timecode_ns(phone.qr(i+1).tk)); }
    }
    CHECK_THROWS(PhoneGeneratorPlan("unsafe\"",2,epoch,1));
    CHECK_THROWS(PhoneGeneratorPlan("phone",4,epoch,1));
    CHECK_THROWS(encode_timecode(1,epoch,0,false,true));
}

TEST_CASE("phone source latency stays explicitly estimated even with receiver clock lock") {
    for (auto state : {"locked","unlocked","unknown"}) {
        ReferenceInfo ref; ref.type="tai-ptp"; ref.sync_state=state;
        Pairer pairer(500,{},ref,"LIVE","phone");
        auto qr=PhoneGeneratorPlan("phone",2,1700000037000000000LL,100).qr(1);
        VideoObservation video; video.event=true;video.seq=1;video.time_ms=1000;video.quality="good";
        video.have_arrival=true;video.arrival_ms=1700000037120.0;video.flags={"optical_capture"};
        REQUIRE(identify_video(video,qr)); pairer.observe_video(video,0);
        AudioTimecode packet; REQUIRE(parse_timecode(encode_timecode(qr.source_id,qr.tk,0,true,true),packet));
        packet.time_ms=1035;packet.arrival_ms=1700000037155.0;packet.correlation=.99;
        pairer.observe_audio(audio_observation(packet));
        auto events=pairer.poll(2000); REQUIRE(events.size()==1);
        const auto& e=events[0]; REQUIRE(e.av_offset_ms);CHECK(*e.av_offset_ms==35);
        CHECK(e.latency_estimated);CHECK(e.reference.sync_state==state);
        REQUIRE(e.video_latency_ms);CHECK(*e.video_latency_ms==120);
        REQUIRE(e.audio_latency_ms);CHECK(*e.audio_latency_ms==155);
        CHECK(e.latency_uncertainty_status=="unknown");CHECK(e.uncertainty_status=="unknown");
    }
}

TEST_CASE("AVS2 compact payload preserves full identities and source clock semantics") {
    QrPayload p;p.timecode=p.qr_flash=true;p.source_id=0xFEDCBA98;p.test_id="presentation-name";
    p.mode="LIVE";p.estimated_source=true;p.k=9007199254740993ULL;p.tk=1793300000000000001LL;p.offset_ms=-3217.0625;p.rate=2;p.format="720p60";
    auto text=qr_payload_text(p);CHECK(text.size()==59);CHECK(text.starts_with("AVS2:"));
    auto decoded=parse_qr_payload(text);REQUIRE(decoded);CHECK(decoded->qr_flash);CHECK(decoded->timecode);
    CHECK(decoded->k==p.k);CHECK(decoded->tk==p.tk);CHECK(decoded->source_id==p.source_id);CHECK(decoded->offset_ms==p.offset_ms);
    CHECK(decoded->estimated_source);CHECK(decoded->format=="720p60");CHECK(decoded->rate==2);
    CHECK_FALSE(parse_qr_payload(text+"0"));text[5]='F';CHECK_FALSE(parse_qr_payload(text));
}

TEST_CASE("AVS2 chirp correlation is the best sub-sample match: bit exact is 1, noise lowers it") {
    const auto wave = timecode_waveform(encode_timecode(123, 1700000037000000000LL, 0, true), 1200, 48000, -12);
    uint32_t seed = 7;
    auto noise = [&] { seed = seed * 1664525u + 1013904223u; return (double(seed >> 8) / double(1 << 24) - 0.5) * 2.0; };
    for (double level : {0.0, 0.08}) {
        // Every 48 kHz phase of the 8 kHz detector grid (2.0.2 reported 0.80 at phase 0).
        for (int phase = 0; phase < 6; ++phase) {
            std::vector<float> x(48000, 0.f);
            const int start = 12000 + phase;
            for (size_t i = 0; i < wave.size(); ++i) x[size_t(start) + i] = wave[i];
            for (auto& v : x) v += float(level * noise());
            const auto packets = decode_timecodes(x.data(), x.size(), 48000, 0);
            REQUIRE(packets.size() == 1);
            CHECK(std::abs(packets[0].time_ms - start / 48.0) < 0.03); // timing as before
            if (level == 0.0) {
                CHECK(packets[0].correlation >= 0.99);
                CHECK(audio_observation(packets[0]).quality == "good");
            } else {
                CHECK(packets[0].correlation < 0.99);
                CHECK(packets[0].correlation > 0.5);
            }
        }
    }
}

TEST_CASE("legacy chirps keep their correlation and timing") {
    AudioDetectConfig cfg;
    cfg.period_ms = 500;
    AudioDetector detector(cfg);
    PlannedEvent event;
    event.chirp_id = 2;
    for (double centre : {24000.0, 24000.25, 24000.5}) {
        event.audio_centre_sample = centre;
        std::vector<float> x(48000);
        synthesise_block(cfg.chirp, {event}, 0, x.data(), x.size());
        const auto hits = detector.detect(x.data(), x.size(), 0);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].chirp_id == 2);
        CHECK(hits[0].correlation >= 0.99);
        CHECK(hits[0].quality == "good");
        CHECK(std::abs(hits[0].sample_pos - centre) < 0.1);
    }
}

#include "avsync/two_way.hpp"

namespace {

// Two simulated phones A and B in one room. World time = A's clock; B's clock = world + theta.
struct Room {
    double d = 2.0, d_aa = 0.05, d_bb = 0.08;
    double out_a = 12.0, in_a = 8.0, out_b = 25.0, in_b = 5.5, theta = 137.25;
    double t_a = 1000.0, t_b = 1600.0; // emissions: A at t_a (A clock), B at t_b (world)
    uint32_t id_a = 11, id_b = 22;
};

void place(std::vector<float>& x, const std::vector<float>& wave, double onset_ms)
{
    const auto start = static_cast<size_t>(std::llround(onset_ms * 48.0));
    for (size_t i = 0; i < wave.size() && start + i < x.size(); ++i) x[start + i] += wave[i];
}

// The two recordings, each on its own device's clock.
std::pair<std::vector<float>, std::vector<float>> record(const Room& r, double noise)
{
    const double c = kSpeedOfSoundMs / 1000.0; // m/ms
    // Each packet carries its emission time on the emitter's own clock.
    const auto pa = timecode_waveform(encode_timecode(r.id_a, static_cast<int64_t>(r.t_a * 1e6), 0, true), 1200, 48000, -12);
    const auto pb = timecode_waveform(encode_timecode(r.id_b, static_cast<int64_t>((r.t_b + r.theta) * 1e6), 0, true), 1200, 48000, -12);
    std::vector<float> a(48000 * 3, 0.f), b(48000 * 3, 0.f);
    place(a, pa, r.t_a + r.out_a + r.d_aa / c + r.in_a);
    place(a, pb, r.t_b + r.out_b + r.d / c + r.in_a);
    place(b, pa, r.t_a + r.out_a + r.d / c + r.in_b + r.theta);
    place(b, pb, r.t_b + r.out_b + r.d_bb / c + r.in_b + r.theta);
    uint32_t seed = 5;
    for (auto* x : {&a, &b})
        for (auto& v : *x) {
            seed = seed * 1664525u + 1013904223u;
            v += static_cast<float>(noise * (double(seed >> 8) / double(1 << 24) - 0.5) * 2.0);
        }
    return {a, b};
}

} // namespace

TEST_CASE("two-way acoustic ranging recovers distance and clock offset from two recordings") {
    for (double noise : {0.0, 0.02}) {
        Room r;
        const auto [rec_a, rec_b] = record(r, noise);
        const auto a = onsets_from_recording(rec_a.data(), rec_a.size(), 48000, r.id_a, r.id_b);
        const auto b = onsets_from_recording(rec_b.data(), rec_b.size(), 48000, r.id_b, r.id_a);
        REQUIRE(a);
        REQUIRE(b);
        const auto t = two_way_acoustic(*a, *b, r.d_aa, r.d_bb);
        CHECK(std::abs(t.distance_m - r.d) < 0.02); // 2 cm
        // Acoustics alone give theta plus the input-latency difference, never theta alone.
        CHECK(std::abs(t.clock_offset_plus_input_skew_ms - (r.theta + r.in_b - r.in_a)) < 0.05);
    }
}

TEST_CASE("cross runs give the round trip; a trusted clock plus one reference latency split it per device") {
    Room r;
    const auto [rec_a, rec_b] = record(r, 0.0);
    const auto pa_on_a = decode_timecodes(rec_a.data(), rec_a.size(), 48000, 0);
    const auto pb_on_b = decode_timecodes(rec_b.data(), rec_b.size(), 48000, 0);
    auto find = [](const std::vector<AudioTimecode>& packets, uint32_t id) {
        for (const auto& p : packets) if (p.source_id == id) return p;
        FAIL("packet missing");
        return AudioTimecode{};
    };
    // Arrival (receiver clock) minus the emission time the packet carries (sender clock).
    const auto a_on_b = find(pb_on_b, r.id_a), b_on_a = find(pa_on_a, r.id_b);
    const auto a_on_a = find(pa_on_a, r.id_a), b_on_b = find(pb_on_b, r.id_b);
    CrossInput in;
    in.m_ab_ms = a_on_b.time_ms - a_on_b.audio_send_ns / 1e6;
    in.m_ba_ms = b_on_a.time_ms - b_on_a.audio_send_ns / 1e6;
    in.loopback_a_ms = a_on_a.time_ms - a_on_a.audio_send_ns / 1e6;
    in.loopback_b_ms = b_on_b.time_ms - b_on_b.audio_send_ns / 1e6;
    in.self_distance_a_m = r.d_aa;
    in.self_distance_b_m = r.d_bb;
    const OwnRecording oa{a_on_a.time_ms, b_on_a.time_ms}, ob{b_on_b.time_ms, a_on_b.time_ms};
    in.distance_m = two_way_acoustic(oa, ob, r.d_aa, r.d_bb).distance_m;
    const auto unknown = device_cross(in);
    CHECK(std::abs(unknown.round_trip_ms - (r.out_a + r.in_a + r.out_b + r.in_b)) < 0.1);
    CHECK(std::abs(unknown.theta_plus_asymmetry_ms - (r.theta + ((r.out_a + r.in_b) - (r.out_b + r.in_a)) / 2)) < 0.05);
    CHECK(std::abs(unknown.consistency_ms) < 0.1);
    CHECK(!unknown.split);
    const auto known = device_cross(in, r.theta, r.in_a);
    REQUIRE(known.split);
    CHECK(std::abs(known.split->out_a_ms - r.out_a) < 0.05);
    CHECK(std::abs(known.split->in_b_ms - r.in_b) < 0.05);
    CHECK(std::abs(known.split->out_b_ms - r.out_b) < 0.1);
}
