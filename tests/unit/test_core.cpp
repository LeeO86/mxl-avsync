#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "avsync/avsync.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>

using namespace avsync;

namespace {

VideoFormat fmt50()
{
    return *parse_video_format("1080p50");
}

std::vector<float> render_audio(const EventPlan& plan, uint64_t start, size_t count)
{
    ChirpSpec spec;
    spec.sample_rate = plan.config.sample_rate;
    spec.duration_ms = plan.config.chirp_ms;
    spec.low_hz = plan.config.chirp_low_hz;
    spec.high_hz = plan.config.chirp_high_hz;
    spec.level_dbfs = plan.config.chirp_level_dbfs;
    std::vector<float> samples(count);
    synthesise_block(spec, plan.events, start, samples.data(), count);
    return samples;
}

void render_grain(std::vector<uint16_t>& luma, const VideoFormat& fmt, uint64_t seq, int frame_in_period, int period, bool white)
{
    luma.assign(static_cast<size_t>(fmt.width * fmt.grain_height()), kYBackground);
    LumaView view{luma.data(), fmt.width, fmt.grain_height(), fmt.width};
    draw_static_layer(view);
    draw_timing(view, seq, frame_in_period, period, white);
}

struct TimingRow {
    std::string name;
    int hits = 0;
    int expected = 0;
    double p50 = 0;
    double p99 = 0;
    double max_abs = 0;
};

TimingRow measure_shift(const std::vector<float>& samples, const EventPlan& plan, int rate)
{
    AudioDetectConfig cfg;
    cfg.chirp.level_dbfs = plan.config.chirp_level_dbfs;
    cfg.period_ms = plan.period.period_ms;
    AudioDetector detector(cfg);
    const auto hits = detector.detect(samples.data(), samples.size(), 0);
    std::vector<double> errors;
    for (const auto& event : plan.events) {
        const double expected = event.audio_centre_sample;
        const AudioHit* best = nullptr;
        double best_dist = 1e9;
        for (const auto& hit : hits) {
            if (hit.chirp_id != event.chirp_id || hit.quality == "invalid") {
                continue;
            }
            const double dist = std::abs(hit.sample_pos - expected);
            if (dist < best_dist) {
                best = &hit;
                best_dist = dist;
            }
        }
        if (best != nullptr && best_dist < rate * plan.period.period_ms * 0.001) {
            errors.push_back((best->sample_pos - expected) * 1000.0 / rate);
        }
    }
    TimingRow row;
    row.hits = static_cast<int>(errors.size());
    row.expected = static_cast<int>(plan.events.size());
    if (!errors.empty()) {
        std::sort(errors.begin(), errors.end(), [](double a, double b) { return std::abs(a) < std::abs(b); });
        row.p50 = std::abs(errors[errors.size() / 2]);
        row.p99 = std::abs(errors[std::min(errors.size() - 1, errors.size() * 99 / 100)]);
        row.max_abs = std::abs(errors.back());
    }
    return row;
}

std::vector<float> gaussian(size_t n, double rms, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, rms);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        out[i] = static_cast<float>(dist(rng));
    }
    return out;
}

} // namespace

TEST_CASE("event plan ids, grid, offset, sweep and determinism")
{
    PlanConfig config;
    config.video = fmt50();
    config.duration_ms = 60000;
    config.event_rate = 2;
    config.offset_ms = 0;
    const auto plan = make_file_plan(config);
    CHECK(plan.events.size() == 118);
    CHECK(plan.events.front().event_id == "EVT-000001");
    CHECK(plan.events.front().seq == 1);
    CHECK(plan.events.front().chirp_id == 1);
    CHECK(plan.events.front().video_grain == 50);
    CHECK(plan.events.front().video_timeline_ms == doctest::Approx(1000.0));
    CHECK(plan.events.front().audio_sample == 48000);
    CHECK(plan.events.front().rounding_residual_us == doctest::Approx(0.0));
    CHECK(plan.period.grains == 25);
    CHECK(plan.canonical == make_file_plan(config).canonical);

    config.offset_ms = 35;
    const auto late = make_file_plan(config);
    CHECK(late.events.front().expected_av_offset_ms == doctest::Approx(35.0));
    CHECK(late.events.front().audio_sample == 48000 + 35 * 48);

    config.offset_ms = 0;
    config.sweep = {-40, -20, 0, 20, 40};
    const auto sweep = make_file_plan(config);
    CHECK(sweep.events[0].expected_av_offset_ms == doctest::Approx(-40));
    CHECK(sweep.events[4].expected_av_offset_ms == doctest::Approx(40));
    CHECK(sweep.events[5].expected_av_offset_ms == doctest::Approx(-40));

    const auto p5994 = plan_period(*parse_video_format("1080p59.94"), 2);
    CHECK(p5994.grains == 30);
    CHECK(p5994.period_ms == doctest::Approx(500.5).epsilon(0.001));

    const auto p25 = plan_period(*parse_video_format("1080p25"), 2);
    CHECK(p25.grains == 13);

    const auto inter = plan_period(*parse_video_format("1080i25"), 2);
    CHECK(inter.grains % 2 == 0);
    PlanConfig interlaced = config;
    interlaced.video = *parse_video_format("1080i25");
    interlaced.sweep.clear();
    interlaced.duration_ms = 4000;
    const auto ifile = make_file_plan(interlaced);
    CHECK(!ifile.events.empty());
    for (const auto& event : ifile.events) {
        CHECK(event.video_grain % 2 == 0);
    }

    const auto live = live_event(config, 3459211852ULL);
    CHECK(live.seq == 3459211852ULL);
    CHECK(live.rounding_residual_us == 0);
    CHECK(live.event_id == "EVT-3459211852");
    CHECK(live.video_grain == 3459211852ULL * 25ULL);
}

TEST_CASE("chirp duration, level, centre and sample-index purity")
{
    ChirpSpec spec;
    for (int id = 0; id < 4; ++id) {
        const auto check = inspect_chirp(spec, id);
        CHECK(check.length == 1440);
        CHECK(check.peak_dbfs == doctest::Approx(-12).epsilon(0.05));
        CHECK_FALSE(check.clipped);
        const double centre = (check.length - 1) * 0.5;
        const float mid = chirp_sample(spec, id, centre, centre);
        CHECK(std::abs(mid) <= std::pow(10.0, spec.level_dbfs / 20.0) + 1e-4);
    }
    PlanConfig config;
    config.video = fmt50();
    config.duration_ms = 3000;
    const auto plan = make_file_plan(config);
    std::vector<float> whole = render_audio(plan, 0, 48000 * 3);
    std::vector<float> a(2000), b(2000);
    synthesise_block(ChirpSpec{}, plan.events, 47000, a.data(), a.size());
    synthesise_block(ChirpSpec{}, plan.events, 48000, b.data(), b.size());
    for (size_t i = 0; i < 1000; ++i) {
        CHECK(a[1000 + i] == doctest::Approx(b[i]));
        CHECK(whole[47000 + i] == doctest::Approx(a[i]));
    }
    // A fractional centre does not move when the block split changes.
    const double centre = 1000.4;
    float left[16]{};
    float right[16]{};
    PlannedEvent event;
    event.chirp_id = 0;
    event.audio_centre_sample = centre;
    std::vector<PlannedEvent> events{event};
    synthesise_block(ChirpSpec{}, events, 990, left, 16);
    synthesise_block(ChirpSpec{}, events, 1000, right, 16);
    for (int i = 0; i < 6; ++i) {
        CHECK(left[10 + i] == doctest::Approx(right[i]));
    }
}

TEST_CASE("sign convention and grading windows")
{
    CHECK(describe_offset(35).sentence == "audio 35 ms after video");
    CHECK(describe_offset(35).number.front() == '+');
    CHECK(describe_offset(-20).sentence == "audio 20 ms before video");
    CHECK(describe_offset(0).sentence.find("in sync") != std::string::npos);
    GradeWindows windows;
    CHECK(windows_valid(windows));
    CHECK(grade_value(-5.0, windows) == "pass");
    CHECK(grade_value(15.0, windows) == "pass");
    CHECK(grade_value(15.1, windows) == "warn");
    CHECK(grade_value(60.0, windows) == "warn");
    CHECK(grade_value(60.1, windows) == "fail");
    CHECK(grade_value(-40.0, windows) == "warn");
    CHECK(grade_value(-40.1, windows) == "fail");
    windows.pass_min_ms = 1;
    CHECK_FALSE(windows_valid(windows));
}

TEST_CASE("gray code, marker, drops and a half-transition frame")
{
    CHECK(gray_to_binary(binary_to_gray(0)) == 0);
    CHECK(gray_to_binary(binary_to_gray(24)) == 24);
    CHECK(gray_to_binary(binary_to_gray(0xABCDE)) == 0xABCDE);
    const auto fmt = fmt50();
    std::vector<uint16_t> luma;
    render_grain(luma, fmt, 24, 3, 25, true);
    const auto reading = read_marker(luma.data(), fmt.width, fmt.grain_height(), fmt.width);
    CHECK(reading.strip_ok);
    CHECK(reading.seq_low24 == 24);
    CHECK(reading.frame_pos_mod16 == 3);
    CHECK(reading.state == MarkerReading::State::White);
    CHECK(reading.quality == "good");
    CHECK(reading.geometry_ok);

    MarkerTracker tracker(fmt, 2);
    int events = 0;
    for (int frame = 0; frame < 80; ++frame) {
        const int period = 25;
        const uint64_t seq = static_cast<uint64_t>(1 + frame / period);
        const int pos = frame % period;
        const bool white = (seq % 2) == 1;
        render_grain(luma, fmt, seq, pos, period, white);
        // The file plan's first event is not at frame 0; this synthetic run still toggles every period.
        auto obs = tracker.push(luma.data(), fmt.width, frame, frame * 20.0);
        if (obs.event) {
            ++events;
            CHECK(obs.seq == seq);
        }
    }
    CHECK(events >= 2);
    CHECK(tracker.drops() == 0);

    render_grain(luma, fmt, 4, 0, 25, true);
    // Half the marker is the other colour: a transition frame is invalid.
    const auto geo = frame_geometry(fmt.width, fmt.grain_height());
    for (int y = geo.marker_y; y < geo.marker_y + geo.marker_side / 2; ++y) {
        for (int x = geo.marker_x; x < geo.marker_x + geo.marker_side; ++x) {
            luma[static_cast<size_t>(y * fmt.width + x)] = kYBlack;
        }
    }
    const auto mixed = read_marker(luma.data(), fmt.width, fmt.grain_height(), fmt.width);
    CHECK(mixed.state == MarkerReading::State::Invalid);

    // A duplicated frame increases the duplicate count.
    MarkerTracker dups(fmt, 2);
    for (int frame = 0; frame < 10; ++frame) {
        const int shown = (frame == 5) ? 4 : frame;
        render_grain(luma, fmt, 1, shown % 16, 25, true);
        dups.push(luma.data(), fmt.width, frame, frame * 20.0);
    }
    CHECK(dups.duplicates() >= 1);

    MarkerTracker drops(fmt, 2);
    for (int frame = 0; frame < 12; ++frame) {
        const int shown = frame < 6 ? frame : frame + 2;
        render_grain(luma, fmt, 1, shown & 15, 25, false);
        drops.push(luma.data(), fmt.width, frame, frame * 20.0);
    }
    CHECK(drops.drops() >= 2);
}

TEST_CASE("legacy event seen late after skipped grains takes its time from the strip")
{
    const auto fmt = fmt50();
    std::vector<uint16_t> luma;
    const int period = 25;
    for (int skip : {0, 1, 3, 6}) {
        MarkerTracker tracker(fmt, 2);
        std::vector<VideoObservation> events;
        for (int frame = 0; frame < 3 * period; ++frame) {
            const uint64_t seq = static_cast<uint64_t>(1 + frame / period);
            const int pos = frame % period;
            // The reader passed over the first `skip` grains of the second period (a stalled analyzer).
            if (frame >= period && frame < period + skip) continue;
            render_grain(luma, fmt, seq, pos, period, (seq % 2) == 1);
            auto obs = tracker.push(luma.data(), fmt.width, frame, frame * 20.0, static_cast<int64_t>(frame) * 20000000);
            if (obs.event) events.push_back(obs);
        }
        const auto it = std::find_if(events.begin(), events.end(), [&](const VideoObservation& e) { return e.seq == 2; });
        REQUIRE(it != events.end());
        CHECK(it->time_ms == doctest::Approx(period * 20.0)); // the event frame, not the first frame seen
        CHECK(it->tai_ns == static_cast<int64_t>(period) * 20000000);
        const bool corrected = std::find(it->flags.begin(), it->flags.end(), "event_time_from_strip") != it->flags.end();
        CHECK(corrected == (skip > 0));
    }
}

TEST_CASE("live video schedule commits ahead and skips only after four grains")
{
    const Rate rate{50, 1};
    const auto at = [&](uint64_t k) { return static_cast<uint64_t>(index_to_ns(rate, k)); };
    const uint64_t ms = 1000000ULL;
    const uint64_t lead = 20 * ms;
    // Start: the first grain whose commit time (T_k - lead) is still ahead.
    auto slot = next_video_slot(rate, 0, at(100) + 3 * ms, lead);
    CHECK(slot.index == 102);
    CHECK(slot.skipped == 0);
    // On time: the next grain, rendered a whole grain ahead of its commit time.
    slot = next_video_slot(rate, 101, at(101) - lead + 12 * ms, lead);
    CHECK(slot.index == 102);
    CHECK(slot.skipped == 0);
    // A 35 ms scheduling delay (past half a grain, which rounding to the current index lost) loses no grain.
    slot = next_video_slot(rate, 101, at(102) + 15 * ms, lead);
    CHECK(slot.index == 102);
    CHECK(slot.skipped == 0);
    // Up to four grains behind its timestamp the writer catches up without skipping.
    slot = next_video_slot(rate, 101, at(105) + 19 * ms, lead);
    CHECK(slot.index == 102);
    CHECK(slot.skipped == 0);
    // More than four grains behind: continue at the first grain whose commit time is ahead.
    slot = next_video_slot(rate, 101, at(106) + 1 * ms, lead);
    CHECK(slot.index == 108);
    CHECK(slot.skipped == 6);
    // Lead 0 commits at T_k.
    slot = next_video_slot(rate, 0, at(100) + 3 * ms, 0);
    CHECK(slot.index == 101);
    // The clock stepped back: start again, nothing counted as skipped.
    slot = next_video_slot(rate, 1000, at(100), lead);
    CHECK(slot.index == 101);
    CHECK(slot.skipped == 0);
    // 59.94: the same rule on a fractional grid.
    const Rate ntsc{60000, 1001};
    slot = next_video_slot(ntsc, 0, static_cast<uint64_t>(index_to_ns(ntsc, 500)), 0);
    CHECK(slot.index == 500);
}

TEST_CASE("qr payload schema bounds")
{
    QrPayload payload;
    payload.test_id = "TEST-2026-0001";
    payload.mode = "LIVE";
    payload.k = 3459211852ULL;
    payload.tk = 1729673412000000000LL;
    payload.offset_ms = 0;
    payload.rate = 2;
    payload.format = "1080p50";
    const auto json = qr_payload_json(payload);
    CHECK(json.size() <= 200);
    const auto parsed = parse_qr_payload(json);
    REQUIRE(parsed);
    CHECK(parsed->k == payload.k);
    CHECK(parsed->tk == payload.tk);
    CHECK(parsed->mode == "LIVE");
    CHECK(parsed->format == "1080p50");
    payload.sweep = true;
    const auto sweep = qr_payload_json(payload);
    CHECK(sweep.find("\"sweep\"") != std::string::npos);
    CHECK(parse_qr_payload("{\"s\":\"nope\"}") == std::nullopt);
}

TEST_CASE("pairing range, missing and grading of the median")
{
    GradeWindows windows;
    ReferenceInfo reference;
    reference.type = "file";
    Pairer pairer(500, windows, reference, "FILE", "TEST");
    for (int i = 1; i <= 12; ++i) {
        VideoObservation video;
        video.event = true;
        video.seq = static_cast<uint64_t>(i);
        video.time_ms = 1000.0 * i;
        video.quality = "good";
        pairer.observe_video(video, 0);
        if (i != 4) {
            AudioObservation audio;
            audio.chirp_id = i % 4;
            audio.time_ms = video.time_ms + ((i == 7) ? 1500.0 : 35.0);
            audio.correlation = 0.97;
            audio.quality = "good";
            audio.level_dbfs = -12;
            pairer.observe_audio(audio);
        }
    }
    pairer.finish();
    int missing = 0;
    int out = 0;
    int paired = 0;
    for (const auto& event : pairer.results()) {
        if (event.reason == "missing_audio") {
            ++missing;
        } else if (event.reason == "out_of_range") {
            ++out;
        } else if (event.av_offset_ms) {
            ++paired;
            if (event.seq != 7) {
                CHECK(*event.av_offset_ms == doctest::Approx(35.0).epsilon(0.01));
                CHECK(event.grade == "warn");
            }
        }
    }
    CHECK(missing == 1);
    CHECK(out == 1);
    CHECK(paired >= 10);
    std::vector<Measurement> series;
    for (int i = 0; i < 10; ++i) {
        Measurement m;
        m.grade = "pass";
        m.av_offset_ms = (i == 9) ? 80.0 : 1.0;
        m.deviation_ms = *m.av_offset_ms;
        m.video_time_ms = 1000.0 * (i + 1);
        m.graded_on = "offset";
        series.push_back(m);
    }
    CHECK(current_grade(series, 10, windows, 10000, 5) == "pass");
    series.back().video_time_ms = 0;
    CHECK(current_grade(series, 10, windows, 20000, 5) == "invalid");
}

TEST_CASE("locked ptp fills latency and an unknown clock does not")
{
    GradeWindows windows;
    auto one = [&windows](ReferenceInfo reference) {
        Pairer pairer(500, windows, reference, "LIVE", "TEST");
        VideoObservation video;
        video.event = true;
        video.seq = 4;
        video.time_ms = 5000;
        video.tai_ns = 5000000000LL;
        video.quality = "good";
        video.have_arrival = true;
        video.arrival_ms = 5012;
        video.have_set_time = true;
        video.set_tai_ns = 5000000000LL;
        pairer.observe_video(video, 0);
        AudioObservation audio;
        audio.chirp_id = 0;
        audio.time_ms = 5000;
        audio.tai_ns = 5000000000LL;
        audio.have_arrival = true;
        audio.arrival_ms = 5015;
        audio.correlation = 0.99;
        audio.quality = "good";
        audio.level_dbfs = -12;
        pairer.observe_audio(audio);
        pairer.finish();
        REQUIRE(pairer.results().size() == 1);
        return pairer.results().front();
    };
    ReferenceInfo locked;
    locked.type = "tai-ptp";
    locked.sync_state = "locked";
    const Measurement live = one(locked);
    REQUIRE(live.video_latency_ms);
    REQUIRE(live.audio_latency_ms);
    REQUIRE(live.video_index_shift_ms);
    REQUIRE(live.audio_index_shift_ms);
    REQUIRE(live.av_offset_arrival_ms);
    CHECK(*live.video_latency_ms == doctest::Approx(12.0));
    CHECK(*live.audio_latency_ms == doctest::Approx(15.0));
    CHECK(*live.video_index_shift_ms == doctest::Approx(0.0));
    CHECK(*live.audio_index_shift_ms == doctest::Approx(0.0));
    CHECK(*live.av_offset_arrival_ms == doctest::Approx(3.0));
    ReferenceInfo unknown;
    unknown.type = "tai-ptp";
    unknown.sync_state = "unknown";
    const Measurement dark = one(unknown);
    CHECK(!dark.video_latency_ms);
    CHECK(!dark.audio_latency_ms);
    CHECK(!dark.video_index_shift_ms);
    CHECK(!dark.audio_index_shift_ms);
    CHECK(!dark.av_offset_arrival_ms);
}

TEST_CASE("chain latency subtracts the source emit offsets")
{
    CHECK(video_emit_offset_ms(20) == doctest::Approx(-20.0));
    CHECK(video_emit_offset_ms(-3) == doctest::Approx(0.0));
    CHECK(audio_emit_offset_ms(20) == doctest::Approx(-25.0));
    Measurement m;
    m.video_latency_ms = -16.0;
    m.audio_latency_ms = -24.0;
    SourceEmitOffsets emit;
    apply_source_emit(m, emit);
    CHECK(!m.video_chain_latency_ms);
    CHECK(!m.audio_chain_latency_ms);
    emit.video_ms = -20.0;
    emit.audio_ms = -25.0;
    apply_source_emit(m, emit);
    REQUIRE(m.video_chain_latency_ms);
    REQUIRE(m.audio_chain_latency_ms);
    CHECK(*m.video_chain_latency_ms == doctest::Approx(4.0));
    CHECK(*m.audio_chain_latency_ms == doctest::Approx(1.0));
    CHECK(*m.video_source_emit_offset_ms == doctest::Approx(-20.0));
    // No latency (clock not locked): nothing to subtract from.
    Measurement dark;
    apply_source_emit(dark, emit);
    CHECK(!dark.video_chain_latency_ms);
    CHECK(!dark.video_source_emit_offset_ms);
    // Estimated (phone) latency is never turned into a chain latency.
    Measurement phone;
    phone.latency_estimated = true;
    phone.video_latency_ms = 80.0;
    apply_source_emit(phone, emit);
    CHECK(!phone.video_chain_latency_ms);
}

TEST_CASE("absolute latency is never negative: 0 within its uncertainty, else a mismatch flag")
{
    SourceEmitOffsets emit;
    emit.video_ms = -20.0;
    emit.audio_ms = -25.0;
    Measurement m;
    m.video_latency_ms = -16.0; // chain +4
    m.audio_latency_ms = -26.0; // chain -1, inside 1.5 ms + clock
    apply_source_emit(m, emit, 0.004);
    REQUIRE(m.video_latency_abs_ms);
    REQUIRE(m.audio_latency_abs_ms);
    CHECK(*m.video_latency_abs_ms == doctest::Approx(4.0));
    CHECK(*m.video_latency_abs_uncertainty_ms == doctest::Approx(1.004));
    CHECK(*m.audio_latency_abs_ms == doctest::Approx(0.0));
    CHECK(*m.audio_latency_abs_uncertainty_ms == doctest::Approx(1.504));
    CHECK(*m.audio_chain_latency_ms == doctest::Approx(-1.0)); // the raw value stays for diagnostics
    CHECK(m.latency_abs_clock_accuracy_known);
    CHECK(std::find(m.flags.begin(), m.flags.end(), "emit_offset_mismatch") == m.flags.end());
    // Far below 0: the entered offset (or the clock) is wrong. No absolute value, a flag instead.
    Measurement wrong;
    wrong.video_latency_ms = -40.0; // chain -20
    wrong.audio_latency_ms = -24.0;
    apply_source_emit(wrong, emit, std::nullopt);
    CHECK(!wrong.video_latency_abs_ms);
    REQUIRE(wrong.audio_latency_abs_ms);
    CHECK(*wrong.audio_latency_abs_uncertainty_ms == doctest::Approx(1.5)); // clock accuracy unknown
    CHECK(!wrong.latency_abs_clock_accuracy_known);
    CHECK(std::count(wrong.flags.begin(), wrong.flags.end(), "emit_offset_mismatch") == 1);
    // Nothing entered: no absolute latency.
    Measurement none;
    none.video_latency_ms = 12.0;
    apply_source_emit(none, SourceEmitOffsets{}, 0.004);
    CHECK(!none.video_latency_abs_ms);
    CHECK(!none.video_chain_latency_ms);
}

TEST_CASE("a path that re-stamps by a constant shows it in content vs grain for both essences")
{
    // A gateway maps the content 480 ms later (video) and 476.5 ms later (audio, moved onto its video phase).
    Pairer pairer(500, GradeWindows{}, ReferenceInfo{}, "LIVE", "TEST");
    VideoObservation video;
    video.event = true;
    video.timecode = true;
    video.source_id = 7;
    video.event_time_ns = 5000000000LL;
    video.seq = 10;
    video.time_ms = 5480;
    video.quality = "good";
    video.have_set_time = true;
    video.set_tai_ns = 5000000000LL;
    video.have_grain_time = true;
    video.grain_tai_ns = 5480000000LL;
    pairer.observe_video(video, 0);
    AudioObservation audio;
    audio.timecode = true;
    audio.source_id = 7;
    audio.event_time_ns = 5000000000LL;
    audio.audio_send_ns = 5000000000LL;
    audio.time_ms = 5476.5;
    audio.have_grain_time = true;
    audio.grain_tai_ns = 5476500000LL;
    audio.correlation = 1.0;
    audio.quality = "good";
    pairer.observe_audio(audio);
    pairer.finish();
    REQUIRE(pairer.results().size() == 1);
    const auto& m = pairer.results().front();
    REQUIRE(m.av_offset_ms);
    CHECK(*m.av_offset_ms == doctest::Approx(-3.5));
    CHECK(*m.video_content_vs_grain_ms == doctest::Approx(-480.0));
    CHECK(*m.audio_content_vs_grain_ms == doctest::Approx(-476.5)); // was 0: the detected time, not the packet's
    CHECK(*m.content_vs_grain_av_ms == doctest::Approx(-3.5));      // was -480
    CHECK(m.grade == "pass");
}

TEST_CASE("content vs grain and index shift agree behind a constant re-stamp: their check reads 0")
{
    ReferenceInfo reference;
    reference.type = "tai-ptp";
    reference.sync_state = "locked";
    Pairer pairer(500, GradeWindows{}, reference, "LIVE", "TEST");
    VideoObservation video;
    video.event = true;
    video.timecode = true;
    video.source_id = 7;
    video.event_time_ns = 5000000000LL;
    video.seq = 10;
    video.time_ms = 5480;
    video.quality = "good";
    video.have_set_time = true;
    video.set_tai_ns = 5000000000LL;
    video.have_grain_time = true;
    video.grain_tai_ns = 5480000000LL;
    video.have_arrival = true;
    video.arrival_ms = 5482;
    pairer.observe_video(video, 0);
    AudioObservation audio;
    audio.timecode = true;
    audio.source_id = 7;
    audio.event_time_ns = 5000000000LL;
    audio.audio_send_ns = 5000000000LL;
    audio.time_ms = 5476.5;
    audio.have_grain_time = true;
    audio.grain_tai_ns = 5476500000LL;
    audio.have_arrival = true;
    audio.arrival_ms = 5478;
    audio.correlation = 1.0;
    audio.quality = "good";
    pairer.observe_audio(audio);
    pairer.finish();
    REQUIRE(pairer.results().size() == 1);
    const auto& m = pairer.results().front();
    CHECK(*m.video_index_shift_ms == doctest::Approx(480.0));
    CHECK(*m.video_content_vs_grain_ms == doctest::Approx(-480.0));
    CHECK(*m.video_content_vs_grain_vs_index_shift_ms == doctest::Approx(0.0)); // was -960
    CHECK(*m.audio_index_shift_ms == doctest::Approx(476.5));
    CHECK(*m.audio_content_vs_grain_vs_index_shift_ms == doctest::Approx(0.0));
}

TEST_CASE("content versus grain does not need a locked clock")
{
    GradeWindows windows;
    auto run = [&windows](const char* sync) {
        ReferenceInfo reference;
        reference.type = "tai-ptp";
        reference.sync_state = sync;
        Pairer pairer(500, windows, reference, "LIVE", "TEST");
        VideoObservation video;
        video.event = true;
        video.seq = 4;
        video.time_ms = 5000;
        video.quality = "good";
        video.have_arrival = true;
        video.arrival_ms = 5012;
        video.have_set_time = true;
        video.set_tai_ns = 5000000000LL;
        video.have_grain_time = true;
        video.grain_tai_ns = 5001000000LL;
        pairer.observe_video(video, 0);
        AudioObservation audio;
        audio.chirp_id = 0;
        audio.time_ms = 5000;
        audio.have_arrival = true;
        audio.arrival_ms = 5015;
        audio.have_grain_time = true;
        audio.grain_tai_ns = 4998000000LL;
        audio.correlation = 0.99;
        audio.quality = "good";
        audio.level_dbfs = -12;
        pairer.observe_audio(audio);
        pairer.finish();
        REQUIRE(pairer.results().size() == 1);
        return pairer.results().front();
    };
    const Measurement unknown = run("unknown");
    REQUIRE(unknown.video_content_vs_grain_ms);
    REQUIRE(unknown.audio_content_vs_grain_ms);
    REQUIRE(unknown.content_vs_grain_av_ms);
    CHECK(*unknown.video_content_vs_grain_ms == doctest::Approx(-1.0));
    CHECK(*unknown.audio_content_vs_grain_ms == doctest::Approx(2.0));
    CHECK(*unknown.content_vs_grain_av_ms == doctest::Approx(-3.0));
    CHECK(!unknown.video_latency_ms);
    CHECK(!unknown.audio_latency_ms);
    CHECK(!unknown.video_content_vs_grain_vs_latency_ms);
    CHECK(!unknown.content_vs_grain_av_vs_arrival_ms);
    const Measurement locked = run("locked");
    REQUIRE(locked.video_latency_ms);
    REQUIRE(locked.video_content_vs_grain_vs_latency_ms);
    REQUIRE(locked.audio_content_vs_grain_vs_latency_ms);
    REQUIRE(locked.video_content_vs_grain_vs_index_shift_ms);
    REQUIRE(locked.audio_content_vs_grain_vs_index_shift_ms);
    REQUIRE(locked.content_vs_grain_av_vs_arrival_ms);
    CHECK(*locked.video_content_vs_grain_vs_latency_ms == doctest::Approx(-1.0 - 12.0));
    CHECK(*locked.content_vs_grain_av_vs_arrival_ms == doctest::Approx(-3.0 - 3.0));
}

TEST_CASE("statistics withhold percentiles and keep outliers")
{
    std::vector<Measurement> events;
    for (int i = 0; i < 25; ++i) {
        Measurement m;
        m.seq = static_cast<uint64_t>(i + 1);
        m.grade = "pass";
        m.av_offset_ms = (i == 24) ? 400.0 : 10.0;
        m.deviation_ms = *m.av_offset_ms;
        m.video_time_ms = i * 500.0;
        events.push_back(m);
    }
    const auto stats = compute_stats(events);
    CHECK(stats.offset.valid == 25);
    CHECK(stats.offset.p5_status == "ok");
    CHECK(stats.offset.p99_status == "insufficient_samples");
    CHECK_FALSE(stats.offset.p99.has_value());
    CHECK_FALSE(stats.offset.outlier_seqs.empty());
    CHECK(stats.offset.mean_wo.has_value());
    CHECK(*stats.offset.mean_wo < *stats.offset.mean);
    events.resize(10);
    const auto few = compute_stats(events);
    CHECK(few.offset.p5_status == "insufficient_samples");
    CHECK_FALSE(few.offset.p5.has_value());
}

TEST_CASE("clean chirp timing is within 0.1 ms and ids do not cross")
{
    PlanConfig config;
    config.video = fmt50();
    config.duration_ms = 8000;
    config.first_event_ms = 1000;
    const auto plan = make_file_plan(config);
    auto samples = render_audio(plan, 0, 48000 * 8);
    const auto row = measure_shift(samples, plan, 48000);
    INFO("hits " << row.hits << " p50 " << row.p50 << " p99 " << row.p99 << " max " << row.max_abs);
    CHECK(row.hits == row.expected);
    CHECK(row.p99 <= 0.1);
    std::cout << "clean p50_ms=" << row.p50 << " p99_ms=" << row.p99 << " max_ms=" << row.max_abs << "\n";
    AudioDetectConfig cfg;
    cfg.period_ms = plan.period.period_ms;
    AudioDetector detector(cfg);
    const auto hits = detector.detect(samples.data(), samples.size(), 0);
    int wrong = 0;
    for (const auto& event : plan.events) {
        const AudioHit* best = nullptr;
        double best_dist = 1e9;
        for (const auto& hit : hits) {
            const double dist = std::abs(hit.sample_pos - event.audio_centre_sample);
            if (dist < best_dist) {
                best = &hit;
                best_dist = dist;
            }
        }
        if (best == nullptr || best->chirp_id != event.chirp_id) {
            ++wrong;
        }
    }
    CHECK(wrong == 0);
}

namespace {

double rms_of(const std::vector<float>& samples)
{
    double sum = 0;
    size_t n = 0;
    for (float s : samples) {
        if (std::abs(s) > 1e-6) {
            sum += static_cast<double>(s) * s;
            ++n;
        }
    }
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0;
}

void biquad_bandpass(std::vector<float>& samples, int rate)
{
    auto apply = [&](double freq, bool highpass) {
        const double w0 = 2.0 * 3.14159265358979323846 * freq / rate;
        const double alpha = std::sin(w0) / (2.0 * 0.707);
        const double cosw = std::cos(w0);
        double b0, b1, b2, a0, a1, a2;
        if (highpass) {
            b0 = (1 + cosw) / 2;
            b1 = -(1 + cosw);
            b2 = (1 + cosw) / 2;
        } else {
            b0 = (1 - cosw) / 2;
            b1 = 1 - cosw;
            b2 = (1 - cosw) / 2;
        }
        a0 = 1 + alpha;
        a1 = -2 * cosw;
        a2 = 1 - alpha;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (float& sample : samples) {
            const double x0 = sample;
            const double y0 = (b0 / a0) * x0 + (b1 / a0) * x1 + (b2 / a0) * x2 - (a1 / a0) * y1 - (a2 / a0) * y2;
            x2 = x1;
            x1 = x0;
            y2 = y1;
            y1 = y0;
            sample = static_cast<float>(y0);
        }
    };
    apply(300, true);
    apply(3400, false);
}

TimingRow run_case(const std::string& name, std::vector<float> samples, const EventPlan& plan)
{
    auto row = measure_shift(samples, plan, plan.config.sample_rate);
    row.name = name;
    const double rate = row.expected ? 100.0 * row.hits / row.expected : 0;
    std::cout << "robust " << name << " detect=" << rate << "% p50=" << row.p50 << " p99=" << row.p99 << " max=" << row.max_abs << "\n";
    return row;
}

} // namespace

TEST_CASE("1080p drawing cost")
{
    const auto fmt = fmt50();
    std::vector<uint16_t> luma(static_cast<size_t>(fmt.width * fmt.height), kYBackground);
    LumaView view{luma.data(), fmt.width, fmt.height, fmt.width};
    const auto t0 = std::chrono::steady_clock::now();
    constexpr int frames = 30;
    for (int i = 0; i < frames; ++i) {
        std::fill(luma.begin(), luma.end(), static_cast<uint16_t>(kYBackground));
        draw_static_layer(view);
        draw_timing(view, static_cast<uint64_t>(1 + i / 25), i % 25, 25, (i / 25) % 2 == 0);
        draw_text(view, 40, 40, "TEST SIGNAL - mxl-avsync", 3, kYWhite);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    std::cout << "draw_1080p_ms=" << ms << "\n";
    CHECK(ms < 8.0);
}

TEST_CASE("audio robustness table")
{
    PlanConfig config;
    config.video = fmt50();
    config.duration_ms = 6000;
    const auto plan = make_file_plan(config);
    const size_t n = 48000 * 6;
    auto clean = render_audio(plan, 0, n);
    const double signal = rms_of(clean);
    auto level_case = [&](double dbfs) {
        PlanConfig cfg = config;
        cfg.chirp_level_dbfs = dbfs;
        const auto local = make_file_plan(cfg);
        return run_case("level " + std::to_string(dbfs), render_audio(local, 0, n), local);
    };
    const auto l40 = level_case(-40);
    const auto l1 = level_case(-1);
    CHECK(l40.hits == l40.expected);
    CHECK(l1.hits == l1.expected);

    auto noisy = [&](double snr_db) {
        auto mixed = clean;
        const double noise = signal / std::pow(10.0, snr_db / 20.0);
        std::mt19937 rng(static_cast<uint32_t>(1000 + snr_db));
        std::normal_distribution<double> dist(0.0, noise);
        for (float& s : mixed) {
            s = static_cast<float>(std::clamp(static_cast<double>(s) + dist(rng), -1.0, 1.0));
        }
        return run_case("snr " + std::to_string(snr_db), mixed, plan);
    };
    const auto snr20 = noisy(20);
    const auto snr10 = noisy(10);
    const auto snr0 = noisy(0);
    CHECK(snr20.hits == snr20.expected);
    CHECK(static_cast<double>(snr10.hits) / snr10.expected >= 0.99);

    auto compressed = clean;
    float env = 0;
    for (float& s : compressed) {
        env = std::max(std::abs(s), env * 0.99f);
        if (env > 0.02f) {
            s *= 0.02f / env;
        }
    }
    const auto agc = run_case("agc", compressed, plan);
    CHECK(agc.hits == agc.expected);

    auto filtered = clean;
    biquad_bandpass(filtered, 48000);
    const auto band = run_case("bandpass", filtered, plan);
    CHECK(static_cast<double>(band.hits) / band.expected >= 0.9);

    auto down = resample_linear(clean.data(), clean.size(), 48000, 44100);
    auto back = resample_linear(down.data(), down.size(), 44100, 48000);
    if (back.size() < n) {
        back.resize(n);
    } else {
        back.resize(n);
    }
    const auto resampled = run_case("resample 44.1", back, plan);
    CHECK(static_cast<double>(resampled.hits) / resampled.expected >= 0.9);

    auto ulaw = encode_ulaw(clean.data(), clean.size());
    std::vector<float> from_ulaw(clean.size());
    decode_ulaw(ulaw.data(), ulaw.size(), from_ulaw.data());
    const auto mu = run_case("g711 ulaw", from_ulaw, plan);
    auto alaw = encode_alaw(clean.data(), clean.size());
    std::vector<float> from_alaw(clean.size());
    decode_alaw(alaw.data(), alaw.size(), from_alaw.data());
    const auto a = run_case("g711 alaw", from_alaw, plan);
    CHECK(static_cast<double>(mu.hits) / mu.expected >= 0.99);
    CHECK(static_cast<double>(a.hits) / a.expected >= 0.99);

    auto gapped = clean;
    for (const auto& event : plan.events) {
        const int centre = static_cast<int>(std::llround(event.audio_centre_sample));
        for (int i = centre - 240; i < centre + 240; ++i) {
            if (i >= 0 && i < static_cast<int>(gapped.size())) {
                gapped[static_cast<size_t>(i)] = 0;
            }
        }
    }
    run_case("gap 10ms", gapped, plan);

    ChirpSpec spec;
    std::vector<PlannedEvent> extra = plan.events;
    for (const auto& event : plan.events) {
        PlannedEvent other = event;
        other.chirp_id = (event.chirp_id + 1) % 4;
        other.audio_centre_sample += 48000.0 * 0.040;
        extra.push_back(other);
    }
    std::vector<float> close(n);
    synthesise_block(spec, extra, 0, close.data(), close.size());
    run_case("chirps 40ms apart", close, plan);
    CHECK(snr0.hits >= 0);
}

TEST_CASE("live pairer reports audio no video claimed and keeps bounded history")
{
    Pairer pairer(500, GradeWindows{}, ReferenceInfo{}, "LIVE", "t");
    pairer.set_live_stream(true);
    CHECK(pairer.poll(0).empty());
    auto audio_for = [](uint64_t k) {
        AudioObservation a;
        a.timecode = true;
        a.source_id = 9;
        a.event_time_ns = static_cast<int64_t>(k) * 500000000LL;
        a.audio_send_ns = a.event_time_ns;
        a.time_ms = static_cast<double>(k) * 500.0;
        a.quality = "good";
        a.correlation = 1.0;
        return a;
    };
    size_t paired = 0, missing = 0;
    for (uint64_t k = 1; k <= 400; ++k) {
        pairer.observe_audio(audio_for(k));
        if (k % 10 != 0) { // every tenth event: the flash was never seen
            VideoObservation v;
            v.event = true;
            v.timecode = true;
            v.source_id = 9;
            v.event_time_ns = static_cast<int64_t>(k) * 500000000LL;
            v.seq = k;
            v.time_ms = static_cast<double>(k) * 500.0;
            v.quality = "good";
            pairer.observe_video(v, 0);
        }
        for (const auto& m : pairer.poll(static_cast<double>(k) * 500.0 + 100.0)) {
            if (m.reason == "missing_video") {
                ++missing;
                CHECK(m.seq % 10 == 0);
            } else if (m.av_offset_ms) {
                ++paired;
            }
        }
    }
    for (const auto& m : pairer.poll(400 * 500.0 + 20000.0)) {
        if (m.reason == "missing_video") ++missing;
    }
    CHECK(paired == 360);
    CHECK(missing == 40);
    CHECK(pairer.results().empty());
}
