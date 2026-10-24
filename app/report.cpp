#include "report.hpp"

#include <sstream>

namespace avsync {
namespace {

nlohmann::json opt_num(const std::optional<double>& value)
{
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

} // namespace

nlohmann::json measurement_to_json(const Measurement& m)
{
    const bool live = m.mode == "LIVE";
    nlohmann::json j = {
        {"schema", kEventLogSchema},
        {"test_id", m.test_id},
        {"event_id", m.event_id},
        {"seq", m.seq},
        {"mode", m.mode},
        {"expected_av_offset_ms", m.expected_av_offset_ms},
        {"grade", m.grade},
        {"graded_on", m.graded_on},
        {"video_quality", m.video_quality},
        {"audio_quality", m.audio_quality},
        {"clipped", m.clipped},
        {"flags", m.flags},
        {"reference", {{"type", m.reference.type}, {"domain", m.reference.domain}, {"sync_state", m.reference.sync_state},
                          {"accuracy_ns", m.reference.accuracy}}},
    };
    if (m.have_video) {
        j["video_event_time"] = live ? nlohmann::json(m.video_tai_ns) : nlohmann::json(m.video_time_ms);
    }
    if (m.have_audio) {
        j["audio_event_time"] = live ? nlohmann::json(m.audio_tai_ns) : nlohmann::json(m.audio_time_ms);
    }
    j["av_offset_ms"] = opt_num(m.av_offset_ms);
    j["deviation_ms"] = opt_num(m.deviation_ms);
    if (m.av_offset_arrival_ms) {
        j["av_offset_arrival_ms"] = *m.av_offset_arrival_ms;
    }
    if (m.video_latency_ms) {
        j["video_latency_ms"] = *m.video_latency_ms;
    }
    if (m.audio_latency_ms) {
        j["audio_latency_ms"] = *m.audio_latency_ms;
    }
    if (m.video_index_shift_ms) {
        j["video_index_shift_ms"] = *m.video_index_shift_ms;
    }
    if (m.audio_index_shift_ms) {
        j["audio_index_shift_ms"] = *m.audio_index_shift_ms;
    }
    if (m.uncertainty_status == "known" && m.uncertainty_ms) {
        j["uncertainty_ms"] = *m.uncertainty_ms;
    } else {
        j["uncertainty_status"] = "unknown";
    }
    if (m.correlation) {
        j["correlation"] = *m.correlation;
    }
    if (m.audio_level_dbfs) {
        j["audio_level_dbfs"] = *m.audio_level_dbfs;
    }
    if (!m.reason.empty()) {
        j["reason"] = m.reason;
    }
    return j;
}

nlohmann::json statistics_to_json(const SeriesStats& stats)
{
    auto field = [](const std::optional<double>& value, const std::string& status) {
        nlohmann::json j = value ? nlohmann::json(*value) : nlohmann::json(nullptr);
        (void)status;
        return j;
    };
    return {
        {"valid", stats.valid},
        {"discarded", stats.discarded},
        {"reasons", stats.reasons},
        {"min", opt_num(stats.min)},
        {"max", opt_num(stats.max)},
        {"median", opt_num(stats.median)},
        {"mean", opt_num(stats.mean)},
        {"stddev", opt_num(stats.stddev)},
        {"p5", field(stats.p5, stats.p5_status)},
        {"p95", field(stats.p95, stats.p95_status)},
        {"p99", field(stats.p99, stats.p99_status)},
        {"p5_status", stats.p5 ? "ok" : stats.p5_status},
        {"p95_status", stats.p95 ? "ok" : stats.p95_status},
        {"p99_status", stats.p99 ? "ok" : stats.p99_status},
        {"series", stats.series},
        {"outlier_seqs", stats.outlier_seqs},
        {"dropout_seqs", stats.dropout_seqs},
        {"excluding_outliers", {{"min", opt_num(stats.min_wo)}, {"max", opt_num(stats.max_wo)}, {"median", opt_num(stats.median_wo)},
                                    {"mean", opt_num(stats.mean_wo)}, {"stddev", opt_num(stats.stddev_wo)}}},
    };
}

nlohmann::json result_document(const std::string& test_id, const std::string& mode, const std::string& graded_on,
    const ReferenceInfo& reference, const nlohmann::json& stream, const std::vector<Measurement>& events)
{
    const auto bundle = compute_stats(events);
    nlohmann::json list = nlohmann::json::array();
    for (const auto& event : events) {
        list.push_back(measurement_to_json(event));
    }
    std::string sentence;
    if (bundle.offset.median) {
        sentence = describe_offset(*bundle.offset.median).line;
    }
    return {
        {"schema", kResultSchema},
        {"test_id", test_id},
        {"mode", mode},
        {"graded_on", graded_on},
        {"reference", {{"type", reference.type}, {"domain", reference.domain}, {"sync_state", reference.sync_state},
                          {"accuracy_ns", reference.accuracy}}},
        {"stream", stream},
        {"summary", {{"sentence", sentence}, {"valid", bundle.offset.valid}, {"discarded", bundle.offset.discarded},
                        {"median_av_offset_ms", opt_num(bundle.offset.median)}}},
        {"statistics", {{"window", statistics_to_json(bundle.offset)}, {"deviation", statistics_to_json(bundle.deviation)},
                           {"video_latency", statistics_to_json(bundle.video_latency)},
                           {"audio_latency", statistics_to_json(bundle.audio_latency)}}},
        {"events", list},
    };
}

std::string human_report(const nlohmann::json& result)
{
    const auto& stats = result["statistics"]["window"];
    const auto& dev = result["statistics"]["deviation"];
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(1);
    os << "  test " << result.value("test_id", "") << " · " << result.value("mode", "") ;
    if (result.contains("stream")) {
        const auto& stream = result["stream"];
        os << " · " << stream.value("format", "") << " · " << stream.value("audio", "");
    }
    os << " · " << stats.value("valid", 0) << " events\n";
    if (stats["median"].is_number()) {
        const double median = stats["median"].get<double>();
        const auto words = describe_offset(median);
        os << "  AV offset   median " << words.number << " (" << words.sentence << ")";
        if (stats["p5"].is_number()) {
            os << "   p5 " << (stats["p5"].get<double>() >= 0 ? "+" : "") << stats["p5"].get<double>();
        }
        if (stats["p95"].is_number()) {
            os << "  p95 " << (stats["p95"].get<double>() >= 0 ? "+" : "") << stats["p95"].get<double>();
        }
        if (stats["stddev"].is_number()) {
            os << "  σ " << stats["stddev"].get<double>();
        }
        os << "\n";
    }
    double expected = 0;
    if (!result["events"].empty() && result["events"][0].contains("expected_av_offset_ms")) {
        expected = result["events"][0]["expected_av_offset_ms"].get<double>();
    }
    std::string grade = "invalid";
    if (dev["median"].is_number() || stats["median"].is_number()) {
        GradeWindows windows;
        const double graded = result.value("graded_on", "offset") == "deviation" && dev["median"].is_number()
            ? dev["median"].get<double>()
            : stats["median"].get<double>();
        grade = grade_value(graded, windows);
    }
    os << "  expected    " << (expected >= 0 ? "+" : "") << expected << " ms";
    if (dev["median"].is_number()) {
        os << " → deviation " << (dev["median"].get<double>() >= 0 ? "+" : "") << dev["median"].get<double>() << " ms";
    }
    os << "                   grade " << grade << "\n";
    int missing = 0;
    int duplicate = 0;
    int ambiguous = 0;
    double corr = 0;
    int corr_n = 0;
    double level = 0;
    bool clipped = false;
    for (const auto& event : result["events"]) {
        const std::string reason = event.value("reason", "");
        if (reason == "missing_audio" || reason == "missing_video") {
            ++missing;
        }
        if (event.contains("flags")) {
            for (const auto& flag : event["flags"]) {
                if (flag == "duplicate") {
                    ++duplicate;
                }
                if (flag == "ambiguous") {
                    ++ambiguous;
                }
            }
        }
        if (event.contains("correlation") && event["correlation"].is_number()) {
            corr += event["correlation"].get<double>();
            ++corr_n;
        }
        if (event.contains("audio_level_dbfs") && event["audio_level_dbfs"].is_number()) {
            level += event["audio_level_dbfs"].get<double>();
        }
        clipped = clipped || event.value("clipped", false);
    }
    const int outliers = stats["outlier_seqs"].is_array() ? static_cast<int>(stats["outlier_seqs"].size()) : 0;
    os << "  events      " << stats.value("valid", 0) << " valid · " << missing << " missing · " << duplicate
       << " duplicate · " << ambiguous << " ambiguous · " << outliers << " outliers\n";
    os << "  audio       correlation " << (corr_n ? corr / corr_n : 0) << " · level " << (corr_n ? level / corr_n : 0)
       << " dBFS · " << (clipped ? "clipping" : "no clipping") << "\n";
    return os.str();
}

} // namespace avsync
