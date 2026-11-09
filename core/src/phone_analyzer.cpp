#include "avsync/phone_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace avsync {
namespace {

// Minimal JSON object writer: numbers, strings, booleans and nested raw JSON.
class Json {
public:
    Json& num(const char* key, double v)
    {
        if (!std::isfinite(v)) return *this;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        return raw(key, buf);
    }
    Json& integer(const char* key, long long v) { return raw(key, std::to_string(v)); }
    Json& boolean(const char* key, bool v) { return raw(key, v ? "true" : "false"); }
    Json& str(const char* key, const std::string& v)
    {
        std::string out = "\"";
        for (char c : v) {
            if (c == '"' || c == '\\') out += '\\', out += c;
            else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
            else out += c;
        }
        return raw(key, out + "\"");
    }
    Json& raw(const char* key, const std::string& v)
    {
        body_ += body_.empty() ? "" : ",";
        body_ += "\"" + std::string(key) + "\":" + v;
        return *this;
    }
    std::string text() const { return "{" + body_ + "}"; }

private:
    std::string body_;
};

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}
// Width of the middle 80 % (all values for fewer than 10).
double spread(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    if (v.size() < 10) return v.back() - v.front();
    auto at = [&](double q) { return v[size_t(std::lround(q * double(v.size() - 1)))]; };
    return at(.9) - at(.1);
}
std::string array(const std::vector<std::string>& items)
{
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + items[i];
    return out + "]";
}

constexpr size_t kWindowEvents = 8;  // the reading is the median of the last 8 pairs
constexpr double kWindowMs = 10000;  // ... within the last 10 s

} // namespace

PhoneAnalyzer::PhoneAnalyzer() : session_(2, GradeWindows{}) {}

void PhoneAnalyzer::reset()
{
    const double tai = tai_utc_, wall = wall_offset_;
    const bool have = have_wall_;
    *this = PhoneAnalyzer();
    tai_utc_ = tai, wall_offset_ = wall, have_wall_ = have;
}

void PhoneAnalyzer::set_tai_utc_seconds(double s)
{
    if (std::isfinite(s) && s >= 0 && s <= 100) tai_utc_ = s;
}

void PhoneAnalyzer::set_device_utc_offset(double offset)
{
    if (!std::isfinite(offset)) return;
    if (have_wall_ && std::abs(offset - wall_offset_) > 100) qr_.reset();
    wall_offset_ = offset;
    have_wall_ = true;
}

void PhoneAnalyzer::observe_qr(const std::string& text, const std::array<double, 4>& x, const std::array<double, 4>& y, double capture_ms)
{
    auto payload = parse_qr_payload(text);
    if (!payload || !payload->optical_code || !std::isfinite(capture_ms) || calibration_ || calibrator_) return;
    qr_ = payload;
    qr_ms_ = capture_ms;
    QrDetection q;
    q.payload = *payload;
    q.x = x, q.y = y;
    session_.observe_qr(q, capture_ms);
}

bool PhoneAnalyzer::qr_scan_suggested(double t) const { return !calibration_ && !calibrator_ && session_.qr_scan_suggested(t); }

void PhoneAnalyzer::push_video(const uint8_t* luma, int w, int h, int stride, double pts, double exposure, double readout)
{
    if (!luma || w <= 0 || h <= 0 || !std::isfinite(pts)) return;
    if (origin_ < 0) origin_ = pts;
    last_video_ = pts;
    ++frames_;
    frame_times_.push_back(pts);
    while (frame_times_.size() > 2 && frame_times_.front() < pts - 3000) frame_times_.pop_front();
    if (calibrator_) {
        if (*calibrator_ != CalibratorMode::Camera || calibrator_done_ || flash_frames_.size() > 12000) return;
        // Mean luma of 32 row bands (every 8th pixel of every 4th row).
        constexpr int kBands = 32;
        FlashFrame f{pts, exposure, readout, std::vector<float>(kBands)};
        for (int b = 0; b < kBands; ++b) {
            double sum = 0;
            int count = 0;
            for (int yy = b * h / kBands; yy < (b + 1) * h / kBands; yy += 4)
                for (int xx = 0; xx < w; xx += 8) sum += luma[size_t(yy) * stride + size_t(xx)], ++count;
            f.rows[size_t(b)] = count ? float(sum / count) : 0;
        }
        flash_frames_.push_back(std::move(f));
        return;
    }
    if (calibration_) {
        if (calibration_done_) return;
        // Keep a small grey copy (at most 640 px) for the review.
        const int step = std::max(1, (std::max(w, h) + 639) / 640), sw = w / step, sh = h / step;
        CalibrationFrame f{pts, sw, sh, std::vector<uint8_t>(size_t(sw) * sh)};
        for (int yy = 0; yy < sh; ++yy)
            for (int xx = 0; xx < sw; ++xx) f.luma[size_t(yy) * sw + xx] = luma[size_t(yy) * step * stride + size_t(xx) * step];
        pictures_.push_back(std::move(f));
        const double oldest = spike_ >= 0 ? spike_ - 750 : pts - 750;
        while (!pictures_.empty() && (pictures_.front().time_ms < oldest || pictures_.size() > 800)) pictures_.pop_front();
        finish_calibration(std::min(last_video_, last_audio_));
        return;
    }
    video_width_ = w, video_height_ = h;
    session_.push_video_u8(luma, w, h, stride, pts, exposure, readout);
}

void PhoneAnalyzer::push_audio(const float* mono, size_t n, double pts, int rate)
{
    if (!mono || !n || !std::isfinite(pts) || rate <= 0) return;
    if (origin_ < 0) origin_ = pts;
    rate_ = rate;
    last_audio_ = pts + n * 1000.0 / rate;
    if (!calibration_ && !calibrator_) {
        session_.push_audio(mono, n, pts, rate, true);
        return;
    }
    if (calibration_done_ || calibrator_done_) return;
    if (cal_audio_origin_ < 0) cal_audio_origin_ = pts;
    const size_t at = size_t(std::max(0.0, std::round((pts - cal_audio_origin_) * rate / 1000)));
    if (at > cal_audio_.size() && at < size_t((calibrator_ ? 40 : 18) * rate)) cal_audio_.resize(at, 0);
    if (calibrator_) {
        if (cal_audio_.size() < size_t(40 * rate)) cal_audio_.insert(cal_audio_.end(), mono, mono + n);
        return;
    }
    cal_audio_.insert(cal_audio_.end(), mono, mono + n);
    // A sharp sound: a peak well above the running floor, after a quiet start.
    float peak = 0;
    size_t peak_index = 0;
    double power = 0;
    for (size_t i = 0; i < n; ++i) {
        power += mono[i] * mono[i];
        if (std::abs(mono[i]) > peak) peak = std::abs(mono[i]), peak_index = i;
    }
    const double rms = std::sqrt(power / n);
    if (spike_ < 0 && pts - origin_ > 750 && peak > std::max(.08, floor_ * 8) && peak > rms * 1.8) spike_ = pts + peak_index * 1000.0 / rate;
    if (spike_ < 0) floor_ = .97 * floor_ + .03 * rms;
    finish_calibration(std::min(last_video_, last_audio_));
}

void PhoneAnalyzer::begin_calibration()
{
    reset();
    calibration_ = true;
}

void PhoneAnalyzer::finish_calibration(double now)
{
    if (calibration_done_ || origin_ < 0) return;
    if (spike_ < 0) {
        if (now - origin_ > 12000) calibration_done_ = true, calibration_error_ = "No sharp sound heard. Make one clap after a quiet pause.";
        return;
    }
    if (now < spike_ + 700) return;
    calibration_done_ = true;
    int before = 0, after = 0;
    for (const auto& f : pictures_) (f.time_ms < spike_ ? before : after)++;
    if (before < 2 || after < 2) {
        calibration_error_ = "Not enough camera frames around the sound. Try again.";
        return;
    }
    CalibrationTake take;
    take.sample_rate = rate_;
    take.spike_ms = spike_ - origin_;
    take.fps = frames_ * 1000.0 / std::max(1.0, last_video_ - origin_);
    const double clip_start = std::max(cal_audio_origin_, pictures_.front().time_ms);
    const double clip_end = std::min(last_audio_, pictures_.back().time_ms + 1000.0 / std::max(1.0, take.fps));
    const size_t first = std::min(cal_audio_.size(), size_t(std::max(0.0, std::round((clip_start - cal_audio_origin_) * rate_ / 1000))));
    const size_t last = std::clamp(size_t(std::max(0.0, std::round((clip_end - cal_audio_origin_) * rate_ / 1000))), first, cal_audio_.size());
    take.audio.assign(cal_audio_.begin() + long(first), cal_audio_.begin() + long(last));
    take.audio_start_ms = cal_audio_origin_ + first * 1000.0 / rate_ - origin_;
    for (auto& f : pictures_) {
        f.time_ms -= origin_;
        take.frames.push_back(std::move(f));
    }
    pictures_.clear();
    cal_audio_.clear();
    take_ = std::move(take);
}

void PhoneAnalyzer::begin_calibrator(CalibratorMode mode)
{
    reset();
    calibrator_ = mode;
}

std::string PhoneAnalyzer::poll_calibrator()
{
    constexpr int kWanted = 20;            // pulses (camera) or swaps (display) for a result
    constexpr double kLimitMs = 30000;     // then the result with what there is, or an error
    if (!calibrator_ || calibrator_done_ || cal_audio_origin_ < 0) return {};
    const double now = std::max(last_video_, last_audio_);
    if (calibrator_poll_ >= 0 && now - calibrator_poll_ < 1000) return {};
    calibrator_poll_ = now;
    const bool camera = *calibrator_ == CalibratorMode::Camera;
    const auto sounds = find_sound_onsets(cal_audio_.data(), cal_audio_.size(), rate_, cal_audio_origin_);
    double readout = 0;
    std::vector<FlashOnset> flashes;
    if (camera) flashes = find_flash_onsets(flash_frames_, CalibratorContract::led_pulse_ms, &readout);
    auto run = [&](int min_pairs) {
        if (camera) {
            CameraCalibrationOptions o;
            o.min_pairs = min_pairs;
            return camera_calibration(flashes, sounds, o);
        }
        DisplayCalibrationOptions o;
        o.min_pairs = min_pairs;
        return display_calibration(sounds, o);
    };
    const auto progress = run(1);
    const double elapsed = now - origin_;
    Json out;
    out.str("mode", camera ? "camera" : "display");
    if (progress.pairs < kWanted && elapsed < kLimitMs) {
        size_t clicks = 0;
        for (const auto& s : sounds) clicks += s.click();
        out.str("kind", "calibrator-progress").integer("pairs", progress.pairs).integer("wanted", kWanted)
            .integer("flashes", long(flashes.size())).integer("clicks", long(clicks)).num("elapsedMs", elapsed);
        return out.text();
    }
    calibrator_done_ = true;
    const auto r = run(8);
    if (!r.ok) return Json().str("kind", "error").str("message", r.error).text();
    const double fps = frames_ * 1000.0 / std::max(1.0, last_video_ - origin_);
    out.str("kind", "calibrator").str("method", camera ? "calibrator-pulse" : "calibrator-display")
        .num("offsetMs", r.offset_ms).num("uncertaintyMs", r.uncertainty_ms).num("spreadMs", r.spread_ms)
        .integer("pairs", r.pairs).integer("rejected", r.rejected).num("directionMs", r.direction_ms)
        .num("readoutMs", readout > 0 ? readout : r.readout_ms);
    if (camera) out.num("fps", fps);
    return out.text();
}

std::optional<PhoneAnalyzer::CalibrationTake> PhoneAnalyzer::take_calibration()
{
    auto out = std::move(take_);
    take_.reset();
    return out;
}

bool PhoneAnalyzer::picture(std::array<double, 4>& x, std::array<double, 4>& y) const
{
    const auto& pose = session_.current_picture();
    if (calibration_ || calibrator_ || !pose.found || video_width_ <= 0 || last_audio_ - last_video_ > 300) return false;
    for (int i = 0; i < 4; ++i) x[i] = pose.corner_x[i] / video_width_, y[i] = pose.corner_y[i] / video_height_;
    return true;
}

bool PhoneAnalyzer::report_due() const
{
    const double now = std::max(last_video_, last_audio_);
    return !calibration_ && !calibrator_ && origin_ >= 0 && now - origin_ >= 1500 && now - last_report_ >= 1000;
}

PhoneAnalyzer PhoneAnalyzer::snapshot_for_poll()
{
    PhoneAnalyzer copy = *this;
    last_report_ = std::max(last_video_, last_audio_);
    session_.trim_before(last_report_ - 12000);
    return copy;
}

void PhoneAnalyzer::merge_audio(const PhoneAnalyzer& analyzed)
{
    const auto& a = qr_;
    const auto& b = analyzed.qr_;
    if (bool(a) != bool(b) || (a && b && (a->source_id != b->source_id || a->test_id != b->test_id))) return;
    session_.merge_audio_analysis(analyzed.session_);
}

std::string PhoneAnalyzer::poll()
{
    if (calibration_ || calibrator_ || origin_ < 0) return {};
    const double now = std::max(last_video_, last_audio_);
    last_report_ = now;
    const auto report = session_.finish("PHONE");
    session_.trim_before(now - 12000);

    // The window: the newest valid pairs.
    struct Pair { uint64_t seq; double offset; std::optional<double> video, audio; double time; double raw; std::optional<double> sender; };
    std::vector<Pair> pairs;
    for (const auto& e : report.events) {
        if (!e.av_offset_ms || e.grade == "invalid" || !std::isfinite(*e.av_offset_ms)) continue;
        const double t = std::max(e.video_time_ms, e.audio_time_ms);
        if (t < now - kWindowMs) continue;
        Pair p{e.seq, *e.av_offset_ms, std::nullopt, std::nullopt, t, e.raw_av_offset_ms.value_or(*e.av_offset_ms), e.sender_calibration_ms};
        // Latency vs device UTC: capture time on the device UTC clock minus the source time (TAI).
        if (have_wall_ && qr_ && qr_->mode == "LIVE") {
            if (e.have_set_time) p.video = e.video_time_ms + wall_offset_ + tai_utc_ * 1000 - double(e.set_video_ns) / 1e6;
            if (e.audio_timecode && e.audio_send_ns > 0) p.audio = e.audio_time_ms + wall_offset_ + tai_utc_ * 1000 - double(e.audio_send_ns) / 1e6;
        }
        pairs.push_back(p);
    }
    if (pairs.size() > kWindowEvents) pairs.erase(pairs.begin(), pairs.end() - long(kWindowEvents));

    const double period = qr_ ? 1000.0 / std::max(1, qr_->rate) : 500;
    const bool picture = now - qr_ms_ < 2000;
    const bool fresh_video = now - report.last_video_event_ms <= period + 400;
    bool fresh_audio = false;
    for (const auto& p : report.audio_timecodes) fresh_audio |= now - p.time_ms <= period + 1500;
    const char* stage = !picture ? "no_signal"
        : !fresh_video ? "no_events"
        : !fresh_audio ? "no_audio"
        : pairs.size() < 3 ? "pairing"
        : "measuring";

    Json out;
    out.str("kind", "status").num("timeMs", now).str("stage", stage);
    if (qr_) {
        // AVS4: the sender's own output calibration; only "method" while it is not calibrated.
        const auto& c = qr_->calibration;
        Json calibration;
        calibration.str("method", c.method);
        if (c.calibrated()) {
            calibration.num("offsetMs", c.offset_ms).num("uncertaintyMs", c.uncertainty_ms);
            if (c.day > 0) calibration.str("date", calibration_date(c.day));
        }
        out.raw("signal", Json().integer("sourceId", qr_->source_id).str("testId", qr_->test_id).str("name", qr_->name).integer("rate", qr_->rate)
                              .str("format", qr_->format).boolean("live", qr_->mode == "LIVE").boolean("estimatedSource", qr_->estimated_source)
                              .raw("senderCalibration", calibration.text()).text());
    }
    if (pairs.size() >= 3) {
        std::vector<double> offsets, raw, video, audio;
        for (const auto& p : pairs) {
            offsets.push_back(p.offset);
            raw.push_back(p.raw);
            if (p.video) video.push_back(*p.video);
            if (p.audio) audio.push_back(*p.audio);
        }
        Json reading;
        reading.num("offsetMs", median(offsets)).num("spreadMs", spread(offsets)).integer("events", long(offsets.size()));
        // offsetMs has the sender calibration (AVS4) subtracted; rawOffsetMs is as measured.
        reading.num("rawOffsetMs", median(raw));
        if (pairs.back().sender) reading.num("senderCalibrationMs", *pairs.back().sender);
        if (video.size() >= 3) reading.num("videoLatencyMs", median(video)).num("videoSpreadMs", spread(video));
        if (audio.size() >= 3) reading.num("audioLatencyMs", median(audio)).num("audioSpreadMs", spread(audio));
        out.raw("reading", reading.text());
    }
    std::vector<std::string> recent;
    for (const auto& p : pairs) {
        Json e;
        e.integer("seq", long(p.seq)).num("offsetMs", p.offset).num("ageMs", now - p.time);
        if (p.video) e.num("videoLatencyMs", *p.video);
        if (p.audio) e.num("audioLatencyMs", *p.audio);
        recent.push_back(e.text());
    }
    out.raw("events", array(recent));
    if (have_wall_) out.num("deviceUtcOffsetMs", wall_offset_);
    out.num("taiUtcSeconds", tai_utc_);

    // Diagnostics for the debug screen.
    double fps = 0;
    if (frame_times_.size() > 2) fps = (frame_times_.size() - 1) * 1000.0 / std::max(1.0, frame_times_.back() - frame_times_.front());
    const auto& d = session_.qr_flash_diagnostics();
    std::vector<std::string> trace;
    for (const auto& s : d.samples) trace.push_back(Json().num("t", s.time_ms).num("d", s.level).text());
    // The last audio bursts: start chirp match, level, wrong sync bits (of 56) and whether the packet decoded.
    std::vector<std::string> bursts;
    const auto& attempts = session_.timecode_attempts();
    for (size_t i = attempts.size() > 6 ? attempts.size() - 6 : 0; i < attempts.size(); ++i) {
        const auto& a = attempts[i];
        bursts.push_back(Json().num("t", a.time_ms).num("match", a.correlation).num("dbfs", a.level_dbfs)
                             .integer("syncErrors", a.sync_errors).boolean("ok", a.decoded).boolean("eq", a.equalized).text());
    }
    out.raw("diagnostics", Json().num("fps", fps).integer("frames", frames_).integer("bursts", long(d.optical_bursts))
                               .integer("crossings", long(d.optical_crossings)).integer("rejected", long(d.optical_rejected))
                               .integer("unbound", long(d.optical_unbound)).integer("byGrid", long(d.optical_by_grid))
                               .integer("videoEvents", long(report.video_events)).integer("audioTimecodes", long(report.audio_timecodes.size()))
                               .num("audioPeakDbfs", report.audio_peak_dbfs).num("qrAgeMs", qr_ms_ > -1e29 ? now - qr_ms_ : NAN).raw("audioBursts", array(bursts)).raw("trace", array(trace)).text());
    return out.text();
}

} // namespace avsync
