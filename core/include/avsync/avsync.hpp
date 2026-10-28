#pragma once

// avsync-core public API. No MXL, NMOS, FFmpeg or GUI.
// Time: positive av_offset_ms means audio is late (SPECIFICATION §3.2).

#include "avsync/timecode.hpp"
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avsync {

inline constexpr int kYBlack = 64;
inline constexpr int kYWhite = 940;
inline constexpr int kYBackground = 502;
inline constexpr int kYNeutralChroma = 512;

inline constexpr char kCoreVersion[] = "1.0.1";
inline constexpr char kAppVersion[] = "1.0.1";
inline constexpr char kMxlRevision[] = "218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7";
inline constexpr char kQrSchema[] = "mxl-avsync/1";
inline constexpr char kManifestSchema[] = "mxl-avsync-manifest/1";
inline constexpr char kResultSchema[] = "mxl-avsync-result/1";
inline constexpr char kEventLogSchema[] = "mxl-avsync-event-log/1";

struct Rate {
    int64_t num = 50;
    int64_t den = 1;
};

// Same mapping as the MXL C API (index 0 at the SMPTE ST 2059 epoch).
int64_t index_to_ns(Rate rate, uint64_t index);
uint64_t ns_to_index(Rate rate, uint64_t timestamp_ns);

struct VideoFormat {
    std::string token;
    int width = 1920;
    int height = 1080;
    Rate frame{};
    bool interlaced = false; // top field first when true

    Rate grain_rate() const;
    int grain_height() const { return interlaced ? height / 2 : height; }
};

std::optional<VideoFormat> parse_video_format(std::string_view token);
std::string format_token(const VideoFormat& fmt);

struct Period {
    int grains = 25;
    double period_ms = 500;
    double true_rate_hz = 2;
};

Period plan_period(const VideoFormat& video, int event_rate);

struct PlanConfig {
    VideoFormat video{};
    int event_rate = 2;
    int sample_rate = 48000;
    double offset_ms = 0;
    std::vector<double> sweep;
    double first_event_ms = 1000;
    double duration_ms = 60000;
    bool live = false;
    int chirp_ms = 30;
    int timecode_baud = 0; // 0: legacy centre / alternating marker; 600 or 1200: v3 onset / flash
    uint32_t source_id = 0;
    double chirp_low_hz = 700;
    double chirp_high_hz = 2500;
    double chirp_level_dbfs = -12;
};

struct PlannedEvent {
    std::string event_id;
    uint64_t seq = 0;
    int chirp_id = 0;
    int marker_phase = 0;
    uint64_t video_grain = 0;
    double video_timeline_ms = 0;
    int64_t video_tai_ns = 0;
    double audio_timeline_ms = 0;
    int64_t audio_tai_ns = 0;
    double audio_centre_sample = 0; // may be fractional in LIVE
    int64_t audio_sample = 0;       // nearest sample (FILE)
    double expected_av_offset_ms = 0;
    double rounding_residual_us = 0;
    int frame_in_period = 0;
    bool timecode = false;
    std::vector<uint8_t> audio_packet;
};

struct EventPlan {
    PlanConfig config;
    Period period;
    std::vector<PlannedEvent> events;
    std::string canonical; // deterministic bytes for the same config
};

EventPlan make_file_plan(const PlanConfig& config);
PlannedEvent live_event(const PlanConfig& config, uint64_t seq);
std::string event_id_for(uint64_t seq);

// ---- chirps (§6.1) -------------------------------------------------------

struct ChirpSpec {
    int sample_rate = 48000;
    double duration_ms = 30;
    double low_hz = 700;
    double high_hz = 2500;
    double level_dbfs = -12;
};

struct ChirpBand {
    double f0 = 0;
    double f1 = 0;
};

ChirpBand chirp_band(int chirp_id, double low_hz, double high_hz);
int chirp_length(const ChirpSpec& spec);

// Pure function of the absolute sample index. Centre may be fractional.
float chirp_sample(const ChirpSpec& spec, int chirp_id, double centre_sample, double sample_index);
void synthesise_block(const ChirpSpec& spec, const std::vector<PlannedEvent>& events, uint64_t start, float* dst, size_t count);

struct ChirpCheck {
    int length = 0;
    double peak = 0;
    double peak_dbfs = 0;
    bool clipped = false;
    double centre_time_ms = 0;
    double energy_low = 0;
    double energy_high = 0;
};

ChirpCheck inspect_chirp(const ChirpSpec& spec, int chirp_id);

// ---- audio detection (§6.2) ----------------------------------------------

struct AudioHit {
    double time_ms = 0;
    double sample_pos = 0; // absolute sample index of the centre, fractional
    int chirp_id = 0;
    double correlation = 0;
    double level_dbfs = -120;
    bool clipped = false;
    std::string quality; // good, degraded, invalid
    bool duplicate = false;
    bool ambiguous = false;
};

struct AudioDetectConfig {
    ChirpSpec chirp{};
    double threshold = 0.5;
    double sidelobe_ratio = 1.4;
    double ambiguity_ratio = 1.12;
    double period_ms = 500;
    double timeline_origin_ms = 0; // time of sample 0
};

class AudioDetector {
public:
    explicit AudioDetector(AudioDetectConfig config);
    // samples are one channel, contiguous, starting at absolute sample `origin`.
    std::vector<AudioHit> detect(const float* samples, size_t count, uint64_t origin_sample) const;
    const AudioDetectConfig& config() const { return config_; }

private:
    AudioDetectConfig config_;
    int length_ = 0;
    double ref_norm_[4]{};
    std::vector<float> refs_[4];
};

// ---- picture geometry, marker draw and detect (§5) -----------------------

struct FrameGeometry {
    int width = 1920;
    int height = 1080;
    int marker_x = 0;
    int marker_y = 0;
    int marker_side = 0;
    int strip_x = 0;
    int strip_y = 0;
    int strip_w = 0;
    int strip_h = 0;
    int qr_x = 0;
    int qr_y = 0;
    int qr_size = 0;
    int sweep_x0 = 0;
    int sweep_x1 = 0;
    int sweep_y = 0;
    int fid_size = 0;
};

FrameGeometry frame_geometry(int width, int height);

struct LumaView {
    uint16_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

void fill_luma(LumaView view, uint16_t y);
void draw_static_layer(LumaView view);
void draw_timing(LumaView view, uint64_t seq, int frame_in_period, int period_frames, bool marker_white);
void draw_text(LumaView view, int x, int y, std::string_view text, int scale, uint16_t y_value);
int text_px_width(std::string_view text, int scale);
int text_px_height(int scale);

// Visual sync scale. Negative is audio early (left, red), zero is white, positive is audio late (right, green).
enum class SyncDot { Early, Zero, Late };
SyncDot sync_dot_kind(double position_ms);

// Where the sync square is during one event. Each step is one frame.
// At 1080p50 and 2/s that is 20 ms, so the window is ±240 ms and the square
// crosses it once per 25-frame period.
struct SyncPlace {
    double scale_ms = 250;
    double position_ms = 0;
};
SyncPlace sync_place(int frame_in_period, int period_frames, double period_ms);
struct SyncIndicator {
    int x=0, y=0, side=0;
    SyncDot kind=SyncDot::Zero;
    uint16_t y10=940, cb10=512, cr10=512;
};
SyncIndicator sync_indicator(int width, int height, int frame, int period_frames, double period_ms);


struct MarkerReading {
    enum class State { Black, White, Invalid };
    State state = State::Invalid;
    bool strip_ok = false;
    uint32_t seq_low24 = 0;
    int frame_pos_mod16 = 0;
    double contrast = 0;
    double noise = 0;
    bool geometry_ok = false;
    std::string quality; // good, degraded, invalid
};

MarkerReading read_marker(const uint16_t* luma, int width, int height, int stride);

// Stage 2: find the test picture in a scaled or cropped frame (a multiviewer tile,
// or a phone camera image) using the four corner fiducials.
struct PicturePose {
    bool found = false;
    double origin_x = 0;
    double origin_y = 0;
    double width = 0;
    double height = 0;
    double corner_x[4] = {};
    double corner_y[4] = {};
    double fiducial_size = 0;
};

PicturePose locate_picture(const uint16_t* luma, int width, int height, int stride);
// Sample the located picture into a raster and read the marker on that raster.
struct LocatedMarker {
    bool located = false;
    PicturePose pose;
    MarkerReading reading;
    int width = 0;
    int height = 0;
    std::vector<uint16_t> canonical;
};
LocatedMarker read_located_marker(const uint16_t* luma, int width, int height, int stride);
LocatedMarker read_located_marker_at_pose(const uint16_t* luma, int width, int height, int stride, const PicturePose& pose, int canonical_width = 0);

// Row time inside one rolling-shutter frame. readout_ms is the full-frame readout; 0 is global shutter.
double marker_row_time_ms(double frame_start_ms, double readout_ms, int row, int height);

// Stage 3 calibration. A record expires after 90 days or an OS change.
struct CalibrationRecord {
    std::string model;
    std::string os_version;
    double offset_ms = 0; // capture bias: subtract from the raw AV offset
    double spread_ms = 0;
    int events = 0;
    std::string method = "led-piezo";
    int64_t calibrated_unix_ms = 0;
};
enum class CalibrationState { Missing, Expired, Valid };
CalibrationState calibration_state(const CalibrationRecord* record, std::string_view os_version, int64_t now_unix_ms);
const char* calibration_state_name(CalibrationState state);

struct VideoObservation {
    bool event = false;
    uint64_t seq = 0;
    int64_t grain_index = 0;
    double time_ms = 0;
    int64_t tai_ns = 0;
    std::string quality;
    std::vector<std::string> flags;
    double contrast = 0;
    bool marker_white = false;
    int drops_total = 0;
    int duplicates_total = 0;
    bool strip_ok = false;
    bool have_arrival = false;
    double arrival_ms = 0;
    bool timecode = false;
    bool estimated_source = false;
    uint32_t source_id = 0;
    int64_t event_time_ns = 0;
    bool have_set_time = false;
    int64_t set_tai_ns = 0; // generator T_k from the QR, when that code names this event
    bool have_grain_time = false;
    int64_t grain_tai_ns = 0; // MXL timestamp of this grain, from its index
};

class MarkerTracker {
public:
    explicit MarkerTracker(VideoFormat format, int event_rate);
    // time_ms is the grain's index time on the timeline in use (file ms or TAI ms).
    VideoObservation push(const uint16_t* luma, int stride, int64_t grain_index, double time_ms, int64_t tai_ns = 0);
    int drops() const { return drops_; }
    int duplicates() const { return duplicates_; }
    const Period& period() const { return period_; }
    void configure(int event_rate, bool flash);

private:
    VideoFormat format_;
    Period period_;
    bool have_prev_ = false;
    bool flash_ = false;
    uint64_t emitted_seq_ = ~uint64_t{0};
    MarkerReading::State prev_state_ = MarkerReading::State::Invalid;
    int64_t last_unwrapped_pos_ = -1;
    int last_low_ = 0;
    uint64_t last_seq_ = 0;
    int drops_ = 0;
    int duplicates_ = 0;
    int64_t last_event_grain_ = -100000;
};

uint32_t binary_to_gray(uint32_t value);
uint32_t gray_to_binary(uint32_t gray);

// v210 pack of a limited-range 4:2:2 picture. chroma planes are optional (neutral if null).
size_t v210_line_bytes(int width);
void pack_v210(const uint16_t* y, const uint16_t* cb, const uint16_t* cr, int width, int height, int y_stride,
    uint8_t* dst);
void unpack_v210_luma(const uint8_t* src, int width, int height, uint16_t* y, int y_stride);

// ---- QR payload (§5.6) ---------------------------------------------------

struct QrPayload {
    std::string schema = kQrSchema;
    std::string test_id;
    std::string mode = "FILE";
    uint64_t k = 0;
    int64_t tk = 0;
    bool sweep = false;
    double offset_ms = 0;
    int rate = 2;
    std::string format = "1080p50";
    bool estimated_source = false; // QR clk=phone-utc
    bool timecode = false;
    uint32_t source_id = 0;
};

bool identify_video(VideoObservation& video, const QrPayload& payload);
bool flash_white(int frame_in_period, double grain_ms);
std::string qr_payload_json(const QrPayload& payload);
std::optional<QrPayload> parse_qr_payload(std::string_view json);

// ---- pairing, grading, statistics, uncertainty (§4, §8) ------------------

struct ReferenceInfo {
    std::string type = "none"; // tai-ptp, file, none
    std::string domain;
    std::string sync_state = "unknown";
    std::string accuracy = "unknown"; // decimal ns, or "unknown"
};

struct AudioObservation {
    bool timecode = false;
    bool estimated_source = false;
    uint32_t source_id = 0;
    int64_t event_time_ns = 0;
    int64_t audio_send_ns = 0;
    int chirp_id = 0;
    double time_ms = 0;
    int64_t tai_ns = 0;
    double arrival_ms = 0;
    bool have_arrival = false;
    bool have_grain_time = false;
    int64_t grain_tai_ns = 0; // MXL timestamp of the chirp's sample index
    double correlation = 0;
    double level_dbfs = -120;
    bool clipped = false;
    std::string quality;
    bool duplicate = false;
    bool ambiguous = false;
};

AudioObservation audio_observation(const AudioTimecode& packet);

struct Measurement {
    bool latency_estimated = false;
    bool audio_timecode = false;
    uint32_t source_id = 0;
    int64_t audio_send_ns = 0;
    std::string test_id;
    std::string event_id;
    uint64_t seq = 0;
    std::string mode = "FILE";
    bool have_video = false;
    bool have_audio = false;
    double video_time_ms = 0;
    double audio_time_ms = 0;
    int64_t video_tai_ns = 0;
    int64_t audio_tai_ns = 0;
    double video_arrival_ms = 0;
    double audio_arrival_ms = 0;
    bool have_arrival = false;
    int64_t set_video_ns = 0; // T_k when known (LIVE)
    bool have_set_time = false;
    std::optional<double> av_offset_ms;
    double expected_av_offset_ms = 0;
    std::optional<double> deviation_ms;
    std::optional<double> av_offset_arrival_ms;
    std::optional<double> video_latency_ms;
    std::optional<double> audio_latency_ms;
    std::optional<double> video_index_shift_ms;
    std::optional<double> audio_index_shift_ms;
    // Content time minus the MXL timestamp of the grain or sample. Both times are on the grain.
    std::optional<double> video_content_vs_grain_ms;
    std::optional<double> audio_content_vs_grain_ms;
    std::optional<double> content_vs_grain_av_ms;
    // Present only with a locked clock, next to the arrival fields.
    std::optional<double> video_content_vs_grain_vs_latency_ms;
    std::optional<double> audio_content_vs_grain_vs_latency_ms;
    std::optional<double> video_content_vs_grain_vs_index_shift_ms;
    std::optional<double> audio_content_vs_grain_vs_index_shift_ms;
    std::optional<double> content_vs_grain_av_vs_arrival_ms;
    std::string grade = "invalid";
    std::string graded_on = "offset";
    std::optional<double> uncertainty_ms;
    std::string uncertainty_status = "unknown";
    std::string latency_uncertainty_status = "unknown";
    std::string video_quality = "invalid";
    std::string audio_quality = "invalid";
    std::optional<double> correlation;
    std::optional<double> audio_level_dbfs;
    bool clipped = false;
    std::vector<std::string> flags;
    std::string reason;
    ReferenceInfo reference;
};

struct GradeWindows {
    double pass_min_ms = -5;
    double pass_max_ms = 15;
    double warn_min_ms = -40;
    double warn_max_ms = 60;
};

bool windows_valid(const GradeWindows& w);
std::string grade_value(double value_ms, const GradeWindows& windows);

struct OffsetWords {
    std::string number;   // "+35.2 ms"
    std::string sentence; // "audio 35 ms after video"
    std::string line;     // "+35.2 ms — audio 35 ms after video"
};

OffsetWords describe_offset(double ms);

// Phone measurement on one clock. Video frames may contain a scaled or offset test picture.
struct PhoneReport {
    std::vector<Measurement> events;
    std::vector<AudioHit> audio_hits;
    std::vector<AudioTimecode> audio_timecodes;
    size_t video_events = 0;
    size_t audio_samples = 0;
    double audio_peak_dbfs = -120;
    int phase_rejected_frames = 0;
    int sequence_rejected_frames = 0;
    int timing_resets = 0;
    int pending_frames = 0;
    std::optional<uint32_t> decoded_seq;
    std::optional<uint32_t> locked_seq;
    double last_valid_frame_ms = -1e30;
    double last_video_event_ms = -1e30;
    std::string calibration = "missing"; // valid, missing, expired
    bool show_grade_colour = false;
    double uncertainty_ms = 0;
};

class PhoneSession {
public:
    PhoneSession(int event_rate, GradeWindows windows);
    void observe_signal(const QrPayload& payload);
    void set_calibration(const CalibrationRecord& record, std::string_view os_version, int64_t now_unix_ms);
    void push_video(const uint16_t* luma, int width, int height, int stride, double frame_start_ms, double readout_ms);
    void push_audio(const float* samples, size_t count, double start_ms, int sample_rate);
    PhoneReport finish(const std::string& test_id = "PHONE");
    void trim_before(double time_ms);
    int located_frames() const { return located_frames_; }
    int valid_frames() const { return valid_frames_; }
    const PicturePose& current_picture() const { return current_picture_; }
    bool picture_code_confirmed() const { return picture_code_confirmed_; }
    std::optional<uint32_t> confirmed_picture_seq() const { return confirmed_picture_seq_; }
    double confirmed_picture_time_ms() const { return confirmed_picture_time_ms_; }
    std::optional<VideoObservation> latest_video_event() const {
        return videos_.empty() ? std::nullopt : std::optional<VideoObservation>(videos_.back());
    }

private:
    std::optional<QrPayload> signal_;
    AudioTimecodeStream timecode_stream_;
    std::vector<AudioTimecode> timecodes_;
    PicturePose cached_pose_;
    PicturePose current_picture_;
    bool picture_code_confirmed_ = false;
    std::optional<uint32_t> confirmed_picture_seq_;
    double confirmed_picture_time_ms_ = -1e30;
    double pose_time_ms_ = -1e30;
    int located_frames_ = 0;
    int valid_frames_ = 0;
    int phase_rejected_frames_ = 0;
    int sequence_rejected_frames_ = 0;
    int timing_resets_ = 0;
    std::optional<uint32_t> decoded_seq_;
    double last_valid_frame_ms_ = -1e30;
    int event_rate_;
    GradeWindows windows_;
    CalibrationState calibration_state_ = CalibrationState::Missing;
    CalibrationRecord calibration_{};
    std::optional<uint32_t> locked_seq_;
    uint64_t unwrapped_seq_ = 0;
    uint32_t pending_seq_ = 0;
    int pending_frames_ = 0;
    double pending_time_ms_ = 0;
    double last_seen_ms_ = -1e30;
    double last_transition_ms_ = 0;
    bool have_transition_ = false;
    std::vector<VideoObservation> videos_;
    std::vector<float> audio_;
    double audio_origin_ms_ = 0;
    bool audio_set_ = false;
    int audio_rate_ = 48000;
};

struct SeriesStats {
    int valid = 0;
    int discarded = 0;
    std::map<std::string, int> reasons;
    std::optional<double> min, max, median, mean, stddev;
    std::optional<double> p5, p95, p99;
    std::string p5_status = "insufficient_samples";
    std::string p95_status = "insufficient_samples";
    std::string p99_status = "insufficient_samples";
    std::vector<double> series;
    std::vector<uint64_t> outlier_seqs;
    std::vector<uint64_t> dropout_seqs;
    std::optional<double> min_wo, max_wo, median_wo, mean_wo, stddev_wo;
};

struct StatsBundle {
    SeriesStats offset;
    SeriesStats deviation;
    SeriesStats video_latency;
    SeriesStats audio_latency;
};

StatsBundle compute_stats(const std::vector<Measurement>& events);

struct UncertaintyInput {
    double correlation = 0;
    bool have_correlation = false;
    bool frame_rate_converted = false;
    bool drop_or_dup_near = false;
    double frame_ms = 20;
    bool have_reference = false;
    std::string sync_state = "unknown";
    double wakeup_ms = 1;
    double batch_ms = 0;
    bool live = false;
};

struct Uncertainty {
    std::optional<double> offset_ms;
    std::string offset_status = "unknown";
    std::string latency_status = "unknown";
};

Uncertainty estimate_uncertainty(const UncertaintyInput& in);

// Audio timing half-width (ms) from the M0 correlation table. Updated from measured p99.
double audio_uncertainty_ms(double correlation);

class Pairer {
public:
    Pairer(double period_ms, GradeWindows windows, ReferenceInfo reference, std::string mode, std::string test_id);
    void set_expected_offset(double ms) { default_offset_ms_ = ms; }
    void set_reference(ReferenceInfo reference) { reference_ = std::move(reference); }
    void observe_video(const VideoObservation& video, double expected_offset_ms);
    void observe_audio(const AudioObservation& audio);
    // Close events whose audio window has ended (now_ms on the same timeline).
    std::vector<Measurement> poll(double now_ms);
    std::vector<Measurement> finish();
    const std::vector<Measurement>& results() const { return done_; }

private:
    struct PendingVideo {
        VideoObservation video;
        double expected_offset_ms = 0;
        bool resolved = false;
    };
    Measurement make_invalid(const PendingVideo& pending, const std::string& reason, const std::vector<std::string>& flags);
    Measurement pair_one(const PendingVideo& pending, const AudioObservation* audio, const std::string& reason);
    double period_ms_;
    GradeWindows windows_;
    ReferenceInfo reference_;
    std::string mode_;
    std::string test_id_;
    double default_offset_ms_ = 0;
    std::vector<PendingVideo> videos_;
    std::vector<AudioObservation> audios_;
    std::vector<Measurement> done_;
};

std::string current_grade(const std::vector<Measurement>& events, int last_n, const GradeWindows& windows, double now_ms,
    double stale_s);

// G.711 table codec used by the robustness tests and the file-mode transcode path.
std::vector<uint8_t> encode_ulaw(const float* samples, size_t count);
std::vector<uint8_t> encode_alaw(const float* samples, size_t count);
void decode_ulaw(const uint8_t* bytes, size_t count, float* dst);
void decode_alaw(const uint8_t* bytes, size_t count, float* dst);

// Linear resampler used by unit tests (the file tools use libswresample).
std::vector<float> resample_linear(const float* samples, size_t count, int from_rate, int to_rate);

} // namespace avsync
