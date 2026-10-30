#include "service.hpp"

#include "http.hpp"
#include "ptp.hpp"
#include <iostream>
#include "paths.hpp"
#include "picture.hpp"
#include "report.hpp"
#include "uuid.hpp"

#ifdef AVSYNC_WITH_MXL
#include "domain.hpp"
#include "engine.hpp"
#include "preview.hpp"
#endif
#ifdef AVSYNC_WITH_NMOS
#include "node.hpp"
#include "route.hpp"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace avsync {
namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop = true; }

double measurement_now_ms()
{
#ifdef AVSYNC_WITH_MXL
    return static_cast<double>(tai_now()) / 1e6;
#else
    return 0;
#endif
}

struct Runtime {
    Config config;
    std::mutex mu;
    bool stopped = false;
    bool media_ready = false;
    bool media_live = true;
    bool nmos_registered = false;
    uint64_t nmos_activations = 0;
    std::string input_state = "not_routed";
    std::vector<Measurement> events;
    int events_epoch = 0;
    double generator_events = 0;
    uint64_t timecode_skipped = 0;
    double output_video = 0;
    double output_audio = 0;
    double late_video = 0;
    double late_audio = 0;
    double resyncs = 0;
    std::string media_detail;
    std::string video_flow;
    std::string audio_flow;
    std::string result_flow;
    std::string result_domain;
    int grain_rows = 0;
    int frame_width = 0;
    int frame_height = 0;
    bool interlaced_flow = false;
    double frame_rate_fps = 0;
    std::string source_image_path;
    int source_image_frames = 0;
    std::shared_ptr<const SourceClip> source_clip; // decoded once per upload, drawn by the live generator
    PtpReading clock_status;
    bool clock_synchronized = false;
    double clock_esterror_s = 0;
    long clock_tai_offset_s = 0;
    std::chrono::steady_clock::time_point heartbeat = std::chrono::steady_clock::now();
};

nlohmann::json clock_status_json(const Runtime& rt)
{
    const auto& clock = rt.clock_status;
    return {{"sync_state", clock.sync_state}, {"accuracy_ns", clock.accuracy_ns},
        {"sync_source", clock.sync_source}, {"reason", clock.reason},
        {"gm_present", clock.gm_present}, {"gm_identity", clock.gm_identity},
        {"master_offset_ns", clock.master_offset_ns}, {"port_state", clock.port_state},
        {"clock_synchronized", rt.clock_synchronized}, {"tai_offset_s", rt.clock_tai_offset_s}};
}

HttpResponse http_status_alias(Runtime& rt);
std::map<std::string, std::string> widget_headers(const Config& config);
std::string widget_html(Runtime& rt, const std::string& id, bool latency, bool trend);

std::string web_root()
{
    if (const char* env = std::getenv("AVSYNC_WEB_ROOT")) {
        return env;
    }
#ifdef AVSYNC_WEB_DIR
    return AVSYNC_WEB_DIR;
#else
    return "web";
#endif
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string read_binary(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

HttpResponse json_response(const nlohmann::json& body, int status = 200)
{
    return HttpResponse{status, "application/json", body.dump(), {}};
}

HttpResponse icon_png(const char* name)
{
    const auto path = std::filesystem::path(web_root()) / name;
    if (!std::filesystem::exists(path)) return json_response({{"error", "icon missing"}}, 404);
    return HttpResponse{200, "image/png", read_binary(path), {}};
}

std::string metrics_text(Runtime& rt)
{
    std::lock_guard lock(rt.mu);
    const bool gen = rt.config.role == "generator";
    const bool ana = rt.config.role == "analyzer";
    std::ostringstream os;
    os << "# HELP mxl_avsync_info Build information.\n# TYPE mxl_avsync_info gauge\n";
    os << "mxl_avsync_info{version=\"" << kAppVersion << "\",core_version=\"" << kCoreVersion << "\",mxl_revision=\"" << kMxlRevision
       << "\",role=\"" << rt.config.role << "\"} 1\n";
    os << "# TYPE mxl_avsync_generator_active gauge\nmxl_avsync_generator_active " << (gen && !rt.stopped && rt.media_ready ? 1 : 0) << "\n";
    os << "# TYPE mxl_avsync_generator_audio_muted gauge\nmxl_avsync_generator_audio_muted " << (gen && !rt.config.audio_enable ? 1 : 0) << "\n";
    os << "# TYPE mxl_avsync_timecode_packets_skipped_total counter\nmxl_avsync_timecode_packets_skipped_total " << rt.timecode_skipped << "\n";
    os << "# TYPE mxl_avsync_generator_events_total counter\nmxl_avsync_generator_events_total " << rt.generator_events << "\n";
    os << "# TYPE mxl_avsync_output_frames_total counter\n";
    os << "mxl_avsync_output_frames_total{essence=\"video\"} " << rt.output_video << "\n";
    os << "mxl_avsync_output_frames_total{essence=\"audio\"} " << rt.output_audio << "\n";
    os << "# TYPE mxl_avsync_output_frames_late_total counter\n";
    os << "mxl_avsync_output_frames_late_total{essence=\"video\"} " << rt.late_video << "\n";
    os << "mxl_avsync_output_frames_late_total{essence=\"audio\"} " << rt.late_audio << "\n";
    os << "# TYPE mxl_avsync_output_frames_missed_total counter\n";
    os << "mxl_avsync_output_frames_missed_total{essence=\"video\"} 0\n";
    os << "mxl_avsync_output_frames_missed_total{essence=\"audio\"} 0\n";
    os << "# TYPE mxl_avsync_input_state gauge\n";
    for (const char* state : {"waiting", "no_signal", "not_routed", "running", "format_mismatch"}) {
        const int on = ana && rt.input_state == state ? 1 : 0;
        os << "mxl_avsync_input_state{essence=\"video\",state=\"" << state << "\"} " << on << "\n";
        os << "mxl_avsync_input_state{essence=\"audio\",state=\"" << state << "\"} " << on << "\n";
    }
    os << "# TYPE mxl_avsync_input_resyncs_total counter\n";
    os << "mxl_avsync_input_resyncs_total{essence=\"video\"} " << rt.resyncs << "\nmxl_avsync_input_resyncs_total{essence=\"audio\"} " << rt.resyncs << "\n";
    os << "# TYPE mxl_avsync_events_total counter\n";
    for (const char* result : {"paired", "missing_audio", "missing_video", "duplicate", "ambiguous", "out_of_range"}) {
        int count = 0;
        if (ana) {
            for (const auto& event : rt.events) {
                if ((result == std::string("paired") && event.av_offset_ms && event.grade != "invalid")
                    || event.reason == result) {
                    ++count;
                }
            }
        }
        os << "mxl_avsync_events_total{result=\"" << result << "\"} " << count << "\n";
    }
    std::string grade = "invalid";
    if (ana) {
        grade = current_grade(rt.events, rt.config.grade_events,
            GradeWindows{rt.config.pass_min_ms, rt.config.pass_max_ms, rt.config.warn_min_ms, rt.config.warn_max_ms},
            measurement_now_ms(), rt.config.stale_s);
    }
    os << "# TYPE mxl_avsync_grade gauge\n";
    for (const char* name : {"pass", "warn", "fail", "invalid"}) {
        os << "mxl_avsync_grade{grade=\"" << name << "\"} " << (ana && grade == name ? 1 : 0) << "\n";
    }
    double offset = 0;
    if (!rt.events.empty() && rt.events.back().av_offset_ms) {
        offset = *rt.events.back().av_offset_ms;
    }
    os << "# TYPE mxl_avsync_av_offset_ms gauge\n";
    os << "# TYPE mxl_avsync_av_offset_deviation_ms gauge\n";
    os << "mxl_avsync_av_offset_ms " << offset << "\nmxl_avsync_av_offset_deviation_ms 0\n";
    os << "# TYPE mxl_avsync_av_offset_seconds histogram\n";
    {
        const double edges[] = {-0.5, -0.2, -0.1, -0.06, -0.04, -0.015, -0.005, 0.005, 0.015, 0.04, 0.06, 0.1, 0.2, 0.5};
        const char* labels[] = {"-0.5", "-0.2", "-0.1", "-0.06", "-0.04", "-0.015", "-0.005", "0.005", "0.015", "0.04", "0.06", "0.1", "0.2", "0.5"};
        int cumulative[14] = {};
        double sum = 0;
        int count = 0;
        if (ana) {
            for (const auto& event : rt.events) {
                if (!event.av_offset_ms || event.grade == "invalid") {
                    continue;
                }
                const double seconds = *event.av_offset_ms / 1000.0;
                sum += seconds;
                ++count;
                for (int i = 0; i < 14; ++i) {
                    if (seconds <= edges[i]) {
                        ++cumulative[i];
                    }
                }
            }
        }
        for (int i = 0; i < 14; ++i) {
            os << "mxl_avsync_av_offset_seconds_bucket{le=\"" << labels[i] << "\"} " << cumulative[i] << "\n";
        }
        os << "mxl_avsync_av_offset_seconds_bucket{le=\"+Inf\"} " << count << "\n";
        os << "mxl_avsync_av_offset_seconds_sum " << sum << "\nmxl_avsync_av_offset_seconds_count " << count << "\n";
    }
    os << "# TYPE mxl_avsync_video_drops_total counter\n";
    os << "# TYPE mxl_avsync_video_duplicates_total counter\n";
    os << "mxl_avsync_video_drops_total 0\nmxl_avsync_video_duplicates_total 0\n";
    double correlation = 0;
    double level_dbfs = 0;
    if (ana) {
        for (auto it = rt.events.rbegin(); it != rt.events.rend(); ++it) {
            if (it->correlation) {
                correlation = *it->correlation;
            }
            if (it->audio_level_dbfs) {
                level_dbfs = *it->audio_level_dbfs;
            }
            if (it->correlation && it->audio_level_dbfs) {
                break;
            }
        }
    }
    os << "# TYPE mxl_avsync_audio_correlation gauge\n";
    os << "# TYPE mxl_avsync_audio_level_dbfs gauge\n";
    os << "mxl_avsync_audio_correlation " << correlation << "\nmxl_avsync_audio_level_dbfs " << level_dbfs << "\n";
    os << "# TYPE mxl_avsync_reference_sync gauge\n";
    for (const char* state : {"locked", "unlocked", "unknown"}) {
        const int on = ana && rt.clock_status.sync_state == state ? 1 : 0;
        os << "mxl_avsync_reference_sync{type=\"tai-ptp\",state=\"" << state << "\"} " << on << "\n";
    }
    os << "# TYPE mxl_avsync_clock_synchronized gauge\nmxl_avsync_clock_synchronized " << (rt.clock_synchronized ? 1 : 0) << "\n";
    os << "# TYPE mxl_avsync_clock_esterror_seconds gauge\nmxl_avsync_clock_esterror_seconds " << rt.clock_esterror_s << "\n";
    os << "# TYPE mxl_avsync_clock_tai_offset_seconds gauge\nmxl_avsync_clock_tai_offset_seconds " << rt.clock_tai_offset_s << "\n";
    os << "# TYPE mxl_avsync_clock_source gauge\n";
    for (const char* source : {"kernel", "ptp4l", "url"}) {
        os << "mxl_avsync_clock_source{sync_source=\"" << source << "\"} "
           << (rt.clock_status.sync_source == source ? 1 : 0) << "\n";
    }
    os << "# TYPE mxl_avsync_ptp_gm_present gauge\nmxl_avsync_ptp_gm_present "
       << (rt.clock_status.gm_present ? 1 : 0) << "\n";
    os << "# TYPE mxl_avsync_ptp_master_offset_seconds gauge\nmxl_avsync_ptp_master_offset_seconds "
       << static_cast<double>(rt.clock_status.master_offset_ns) / 1e9 << "\n";
    // Identity is locally formatted hex, never an arbitrary metric-label string.
    os << "# TYPE mxl_avsync_ptp_gm_info gauge\nmxl_avsync_ptp_gm_info{identity=\""
       << rt.clock_status.gm_identity << "\"} " << (rt.clock_status.gm_present ? 1 : 0) << "\n";
    os << "# TYPE mxl_avsync_nmos_registry_up gauge\n";
    os << "mxl_avsync_nmos_registry_up " << (rt.nmos_registered ? 1 : 0) << "\n";
    os << "# TYPE mxl_avsync_nmos_activations_total counter\n";
    os << "mxl_avsync_nmos_activations_total " << rt.nmos_activations << "\n";
    return os.str();
}

#ifndef AVSYNC_WITH_NMOS
nlohmann::json nmos_node(const Config& config)
{
    const std::string node = uuid_v5("mxl-avsync/" + config.seed() + "/node");
    const std::string device = uuid_v5("mxl-avsync/" + config.seed() + "/device");
    const std::string host = config.nmos_host_address.empty() ? "127.0.0.1" : config.nmos_host_address;
    return {{"id", node}, {"version", "0:0"}, {"label", config.label()}, {"description", "mxl-avsync"},
        {"href", "http://" + host + ":" + std::to_string(config.nmos_port) + "/"},
        {"hostname", config.host_id}, {"api", {{"versions", {"v1.3"}}, {"endpoints", {{{"host", host}, {"port", config.nmos_port}, {"protocol", "http"}}}}}},
        {"caps", nlohmann::json::object()}, {"services", nlohmann::json::array()},
        {"clocks", {{{"name", "clk0"}, {"ref_type", "internal"}}}},
        {"interfaces", {{{"name", "eth0"}, {"chassis_id", nullptr}, {"port_id", nullptr}}}}};
    (void)device;
}
#endif

void install_web(HttpServer& http, Runtime& rt)
{
    http.add("GET", "/livez", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        const auto age = std::chrono::steady_clock::now() - rt.heartbeat;
        const bool media_live = rt.media_live;
        const bool heartbeat_ok = age < std::chrono::seconds(5);
        const bool ok = heartbeat_ok && media_live;
        return json_response({{"status", ok ? "ok" : (media_live ? "stale" : "domain")}}, ok ? 200 : 503);
    });
    http.add("GET", "/readyz", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        const bool want_registry = rt.config.nmos_enable && !rt.config.nmos_registry_address.empty();
        // Controllers may wait for deployment readiness before connecting receivers.
        // Route and signal health belong in status/metrics, outside this gate.
        const bool process_up = !g_stop && rt.media_live
            && std::chrono::steady_clock::now() - rt.heartbeat < std::chrono::seconds(5);
        const bool outputs_created = rt.config.role != "generator"
            || (!rt.video_flow.empty() && !rt.audio_flow.empty());
        const bool ok = process_up && outputs_created && (!want_registry || rt.nmos_registered);
        return json_response({{"ready", ok}, {"role", rt.config.role}, {"nmos_registered", rt.nmos_registered}}, ok ? 200 : 503);
    });
    http.add("GET", "/metrics", [&](const HttpRequest&) {
        return HttpResponse{200, "text/plain; version=0.0.4", metrics_text(rt), {}};
    });
    http.add("GET", "/api/v1/info", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        return json_response({{"version", kAppVersion}, {"core_version", kCoreVersion}, {"mxl_revision", kMxlRevision},
            {"role", rt.config.role}, {"label", rt.config.label()}});
    });
    http.add("GET", "/api/v1/status", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        return json_response({{"role", rt.config.role}, {"stopped", rt.stopped}, {"media_ready", rt.media_ready},
            {"timecode_packets_skipped", rt.timecode_skipped}, {"input_state", rt.input_state}, {"detail", rt.media_detail},
            {"nmos_enable", rt.config.nmos_enable}, {"nmos_port", rt.config.nmos_port}, {"nmos_registered", rt.nmos_registered},
            {"audio_enable", rt.config.audio_enable}, {"video_flow", rt.video_flow}, {"audio_flow", rt.audio_flow},
            {"result_flow", rt.result_flow}, {"result_domain", rt.result_domain},
            {"width", rt.frame_width}, {"height", rt.frame_height}, {"grain_rows", rt.grain_rows},
            {"interlaced", rt.interlaced_flow}, {"video_rate_fps", rt.frame_rate_fps},
            {"ptp", clock_status_json(rt)}});
    });
    auto preview_response = [](const std::string& slot) {
#ifdef AVSYNC_WITH_MXL
        std::vector<uint8_t> jpeg;
        if (preview_jpeg(slot, &jpeg)) {
            return HttpResponse{200, "image/jpeg", std::string(jpeg.begin(), jpeg.end()), {{"Cache-Control", "no-store"}}};
        }
#else
        (void)slot;
#endif
        return json_response({{"error", "no frame yet"}}, 404);
    };
    http.add("GET", "/api/v1/preview/input.jpg", [&](const HttpRequest&) { return preview_response("input"); });
    http.add("GET", "/api/v1/preview/generated.jpg", [&](const HttpRequest&) { return preview_response("generated"); });
    http.add("GET", "/statusz", [&](const HttpRequest& request) {
        return http_status_alias(rt);
        (void)request;
    });
    http.add("GET", "/api/v1/generator", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        if (rt.config.role != "generator") return json_response({{"error", "not a generator"}}, 404);
        return json_response({{"offset_ms", rt.config.offset_ms}, {"level_dbfs", rt.config.chirp_level_dbfs}, {"muted", !rt.config.audio_enable},
            {"stopped", rt.stopped}, {"title", rt.config.title}, {"source", rt.config.source}, {"subtitle", rt.config.subtitle},
            {"notes", rt.config.notes},
            {"timing_reference", rt.config.audio_timecode == "legacy" ? "centre" : "start"}, {"marker", rt.config.audio_timecode == "legacy" ? "toggle" : "flash"}, {"audio_timecode", rt.config.audio_timecode}, {"event_rate", rt.config.event_rate}, {"format", rt.config.video_format},
            {"source_image", !rt.source_image_path.empty()}, {"source_image_frames", rt.source_image_frames}});
    });
    http.add("PATCH", "/api/v1/generator", [&](const HttpRequest& request) {
        std::lock_guard lock(rt.mu);
        if (rt.config.role != "generator") return json_response({{"error", "not a generator"}}, 404);
        auto body = nlohmann::json::parse(request.body.empty() ? "{}" : request.body);
        nlohmann::json flat = nlohmann::json::object();
        if (body.contains("offset_ms")) flat["AVSYNC_AV_OFFSET_MS"] = std::to_string(body["offset_ms"].get<double>());
        if (body.contains("level_dbfs")) flat["AVSYNC_CHIRP_LEVEL_DBFS"] = std::to_string(body["level_dbfs"].get<double>());
        if (body.contains("title")) flat["AVSYNC_TITLE"] = body["title"];
        if (body.contains("source")) {
            if (!body["source"].is_string() || body["source"].get<std::string>().size() > 80) {
                return json_response({{"error", "source must be at most 80 characters"}}, 400);
            }
            flat["AVSYNC_SOURCE"] = body["source"];
        }
        if (body.contains("subtitle")) {
            if (!body["subtitle"].is_string() || body["subtitle"].get<std::string>().size() > 80) {
                return json_response({{"error", "subtitle must be at most 80 characters"}}, 400);
            }
            flat["AVSYNC_SUBTITLE"] = body["subtitle"];
        }
        if (body.contains("muted")) flat["AVSYNC_AUDIO_ENABLE"] = body["muted"].get<bool>() ? "false" : "true";
        if (body.contains("audio_timecode")) flat["AVSYNC_AUDIO_TIMECODE"] = body["audio_timecode"].get<std::string>();
        if (body.contains("event_rate")) flat["AVSYNC_EVENT_RATE"] = std::to_string(body["event_rate"].get<int>());
        try {
            apply_settings(rt.config, flat, nullptr);
        } catch (const std::exception& ex) {
            return json_response({{"error", ex.what()}}, 400);
        }
        return json_response({{"ok", true}, {"offset_ms", rt.config.offset_ms}, {"muted", !rt.config.audio_enable},
            {"source", rt.config.source}, {"subtitle", rt.config.subtitle}});
    });
    http.add("POST", "/api/v1/generator/source-image", [&](const HttpRequest& request) {
        std::lock_guard lock(rt.mu);
        if (rt.config.role != "generator") return json_response({{"error", "not a generator"}}, 404);
        const std::string& body = request.body;
        std::string ext;
        std::string type;
        if (body.size() >= 8 && static_cast<unsigned char>(body[0]) == 0x89 && body.compare(1, 3, "PNG") == 0) {
            ext = ".png";
            type = "image/png";
        } else if (body.size() >= 6 && (body.compare(0, 6, "GIF87a") == 0 || body.compare(0, 6, "GIF89a") == 0)) {
            ext = ".gif";
            type = "image/gif";
        } else {
            return json_response({{"error", "source image must be a PNG or a GIF"}}, 415);
        }
        std::filesystem::create_directories(rt.config.state_dir);
        const auto dir = std::filesystem::path(rt.config.state_dir);
        const auto path = dir / ("source-ident" + ext);
        {
            std::ofstream out(path, std::ios::binary);
            out.write(body.data(), static_cast<std::streamsize>(body.size()));
            if (!out) return json_response({{"error", "could not store source image"}}, 500);
        }
        auto clip = std::make_shared<SourceClip>();
        std::string error;
        if (!clip->load(path.string(), error)) {
            std::filesystem::remove(path);
            return json_response({{"error", error}}, 400);
        }
        const auto other = dir / (ext == ".png" ? "source-ident.gif" : "source-ident.png");
        std::error_code ec;
        std::filesystem::remove(other, ec);
        rt.source_image_path = path.string();
        rt.source_image_frames = static_cast<int>(clip->frames.size());
        rt.source_clip = std::move(clip);
        return json_response({{"ok", true}, {"frames", rt.source_image_frames}, {"content_type", type}});
    });
    http.add("GET", "/api/v1/generator/source-image", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        if (rt.config.role != "generator") return json_response({{"error", "not a generator"}}, 404);
        if (rt.source_image_path.empty() || !std::filesystem::exists(rt.source_image_path)) {
            return json_response({{"error", "no source image"}}, 404);
        }
        const bool gif = rt.source_image_path.size() >= 4 && rt.source_image_path.compare(rt.source_image_path.size() - 4, 4, ".gif") == 0;
        return HttpResponse{200, gif ? "image/gif" : "image/png", read_binary(rt.source_image_path), {}};
    });
    http.add("POST", "/api/v1/generator/stop", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        rt.stopped = true;
        return json_response({{"stopped", true}});
    });
    http.add("POST", "/api/v1/generator/start", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        rt.stopped = false;
        return json_response({{"stopped", false}});
    });
    http.add("GET", "/api/v1/measurement", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        GradeWindows windows{rt.config.pass_min_ms, rt.config.pass_max_ms, rt.config.warn_min_ms, rt.config.warn_max_ms};
        const double now_ms = measurement_now_ms();
        const std::string grade = current_grade(rt.events, rt.config.grade_events, windows, now_ms, rt.config.stale_s);
        const Measurement* last = nullptr;
        for (auto it = rt.events.rbegin(); it != rt.events.rend(); ++it) {
            if (it->av_offset_ms && it->grade != "invalid") {
                last = &(*it);
                break;
            }
        }
        nlohmann::json body = {{"grade", grade}, {"sentence", "no measurement"}, {"input_state", rt.input_state}, {"events", rt.events.size()}};
        if (last != nullptr) {
            const double age_ms = now_ms - last->video_time_ms;
            body["age_ms"] = age_ms;
            body["offset_ms"] = *last->av_offset_ms;
            if (grade == "invalid") {
                std::ostringstream sentence;
                sentence.setf(std::ios::fixed);
                sentence.precision(1);
                sentence << "last event " << (age_ms / 1000.0) << " s ago";
                body["sentence"] = sentence.str();
            } else {
                body["sentence"] = describe_offset(*last->av_offset_ms).sentence;
            }
            if (!last->reason.empty()) {
                body["reason"] = last->reason;
            }
            body["latency_estimated"] = last->latency_estimated;
            if (last->latency_estimated) body["source_reference"] = "phone-utc-scheduled-output";
            if (last->video_latency_ms) {
                body["video_latency_ms"] = *last->video_latency_ms;
            }
            if (last->audio_latency_ms) {
                body["audio_latency_ms"] = *last->audio_latency_ms;
            }
        }
        return json_response(body);
    });
    http.add("GET", "/api/v1/events", [&](const HttpRequest& request) {
        std::lock_guard lock(rt.mu);
        const auto query = query_map(request.query);
        uint64_t since = query.count("since") ? std::strtoull(query.at("since").c_str(), nullptr, 10) : 0;
        int limit = query.count("limit") ? std::atoi(query.at("limit").c_str()) : 100;
        if (limit < 1) {
            limit = 1;
        }
        nlohmann::json list = nlohmann::json::array();
        for (auto it = rt.events.rbegin(); it != rt.events.rend() && static_cast<int>(list.size()) < limit; ++it) {
            if (it->seq < since) {
                continue;
            }
            list.push_back(measurement_to_json(*it));
        }
        return json_response({{"events", list}, {"total", rt.events.size()}});
    });
    http.add("GET", "/api/v1/statistics", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        const auto bundle = compute_stats(rt.events);
        return json_response({{"window", statistics_to_json(bundle.offset)}, {"since_start", statistics_to_json(bundle.offset)}});
    });
    http.add("POST", "/api/v1/statistics/reset", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        rt.events.clear();
        ++rt.events_epoch;
        return json_response({{"epoch", rt.events_epoch}});
    });
    http.add("GET", "/api/v1/grading", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        return json_response({{"pass_min_ms", rt.config.pass_min_ms}, {"pass_max_ms", rt.config.pass_max_ms},
            {"warn_min_ms", rt.config.warn_min_ms}, {"warn_max_ms", rt.config.warn_max_ms}, {"grade_events", rt.config.grade_events}});
    });
    http.add("PATCH", "/api/v1/grading", [&](const HttpRequest& request) {
        auto body = nlohmann::json::parse(request.body.empty() ? "{}" : request.body);
        nlohmann::json flat = nlohmann::json::object();
        auto put = [&](const char* json_key, const char* env_key) {
            if (body.contains(json_key)) flat[env_key] = std::to_string(body[json_key].get<double>());
        };
        put("pass_min_ms", "AVSYNC_PASS_MIN_MS");
        put("pass_max_ms", "AVSYNC_PASS_MAX_MS");
        put("warn_min_ms", "AVSYNC_WARN_MIN_MS");
        put("warn_max_ms", "AVSYNC_WARN_MAX_MS");
        if (body.contains("grade_events")) flat["AVSYNC_GRADE_EVENTS"] = std::to_string(body["grade_events"].get<int>());
        std::lock_guard lock(rt.mu);
        try {
            apply_settings(rt.config, flat, nullptr);
        } catch (const std::exception& ex) {
            return json_response({{"error", ex.what()}}, 400);
        }
        return json_response({{"ok", true}});
    });
    http.add("GET", "/api/v1/config", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        std::string env_set;
        for (const auto& entry : rt.config.from_env) {
            if (!env_set.empty()) {
                env_set += ",";
            }
            env_set += entry.first;
        }
        auto response = json_response(config_settings(rt.config));
        response.headers["X-Env-Set"] = env_set;
        return response;
    });
    http.add("PUT", "/api/v1/config", [&](const HttpRequest& request) {
        auto body = nlohmann::json::parse(request.body.empty() ? "{}" : request.body);
        std::vector<std::string> restart;
        std::lock_guard lock(rt.mu);
        for (auto it = body.begin(); it != body.end(); ++it) {
            if (is_restart_key(it.key())) restart.push_back(it.key());
        }
        try {
            apply_settings(rt.config, body, nullptr);
        } catch (const std::exception& ex) {
            return json_response({{"error", ex.what()}}, 400);
        }
        return json_response({{"ok", true}, {"restart_required", restart}});
    });
    http.add("GET", "/api/v1/config/export", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        return json_response({{"version", 1}, {"secrets", false}, {"settings", config_settings(rt.config)}, {"routes", nlohmann::json::object()}});
    });
    http.add("POST", "/api/v1/config/import", [&](const HttpRequest& request) {
        auto body = nlohmann::json::parse(request.body.empty() ? "{}" : request.body);
        std::vector<std::string> skipped;
        std::lock_guard lock(rt.mu);
        apply_settings(rt.config, body.contains("settings") ? body["settings"] : body, &skipped);
        return json_response({{"ok", true}, {"skipped", skipped}});
    });
    http.add("GET", "/api/v1/config/env", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        std::ostringstream os;
        for (auto it = config_settings(rt.config).begin(); it != config_settings(rt.config).end(); ++it) {
            os << it.key() << "=" << it.value().get<std::string>() << "\n";
        }
        return HttpResponse{200, "text/plain", os.str(), {}};
    });
    http.add("POST", "/api/v1/merge", [&](const HttpRequest& request) {
        try {
            const auto body = nlohmann::json::parse(request.body.empty() ? "{}" : request.body);
            if (!body.contains("source") || !body.contains("destination")) {
                return json_response({{"error", "source and destination results are required"}}, 400);
            }
            return json_response(merge_result_documents(body["source"], body["destination"]));
        } catch (const std::exception& ex) {
            return json_response({{"error", ex.what()}}, 400);
        }
    });
    http.add("GET", "/api/v1/paths", [&](const HttpRequest&) {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& path : stage_paths()) {
            list.push_back({{"id", path.id}, {"stage", path.stage}, {"runnable_here", path.runnable_here}, {"detail", path.detail}});
        }
        return json_response(list);
    });
    http.add("GET", "/api/v1/events/ws", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        nlohmann::json hello = {{"type", "state"}, {"role", rt.config.role}, {"grade", "invalid"}};
        return HttpResponse{101, "application/json", hello.dump(), {}};
    });
    http.add("GET", "/widgets", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        nlohmann::json list = nlohmann::json::array();
        if (rt.config.role == "analyzer") {
            list.push_back({{"id", "avsync"}, {"min_width", 240}, {"min_height", 120},
                {"params", {{"latency", {{"type", "boolean"}, {"default", true}}}, {"trend", {{"type", "boolean"}, {"default", true}}}}}});
        } else {
            list.push_back({{"id", "generator"}, {"min_width", 240}, {"min_height", 80}, {"params", nlohmann::json::object()}});
        }
        return json_response(list);
    });
    http.add("GET", "/widget/avsync", [&](const HttpRequest& request) {
        const auto query = query_map(request.query);
        for (const auto& [key, value] : query) {
            if (key != "latency" && key != "trend") return json_response({{"error", "bad parameter"}}, 400);
            if (value != "true" && value != "false" && value != "1" && value != "0" && !value.empty()) {
                return json_response({{"error", "bad parameter"}}, 400);
            }
        }
        std::lock_guard lock(rt.mu);
        const auto off = [](const std::map<std::string, std::string>& query, const char* key) {
            return query.count(key) && (query.at(key) == "false" || query.at(key) == "0");
        };
        const std::string html = widget_html(rt, "avsync", !off(query, "latency"), !off(query, "trend"));
        return HttpResponse{200, "text/html; charset=utf-8", html, widget_headers(rt.config)};
    });
    http.add("GET", "/widget/generator", [&](const HttpRequest& request) {
        if (!request.query.empty()) return json_response({{"error", "bad parameter"}}, 400);
        std::lock_guard lock(rt.mu);
        return HttpResponse{200, "text/html; charset=utf-8", widget_html(rt, "generator", false, false), widget_headers(rt.config)};
    });
    http.add("GET", "/widget/*", [&](const HttpRequest&) { return json_response({{"error", "unknown widget"}}, 404); });
    http.add("GET", "/", [&](const HttpRequest&) {
        const auto path = std::filesystem::path(web_root()) / "index.html";
        if (!std::filesystem::exists(path)) return json_response({{"error", "ui missing"}}, 404);
        return HttpResponse{200, "text/html; charset=utf-8", read_file(path), {}};
    });
    http.add("GET", "/icon.png", [](const HttpRequest&) { return icon_png("icon.png"); });
    http.add("GET", "/favicon-32.png", [](const HttpRequest&) { return icon_png("favicon-32.png"); });
    http.add("GET", "/apple-touch-icon.png", [](const HttpRequest&) { return icon_png("apple-touch-icon.png"); });
    http.add("GET", "/vendor/vue.global.prod.js", [&](const HttpRequest&) {
        const auto path = std::filesystem::path(web_root()) / "vendor" / "vue.global.prod.js";
        return HttpResponse{200, "text/javascript", read_file(path), {}};
    });
}

HttpResponse http_status_alias(Runtime& rt)
{
    std::lock_guard lock(rt.mu);
    return json_response({{"role", rt.config.role}, {"stopped", rt.stopped}, {"media_ready", rt.media_ready},
        {"timecode_packets_skipped", rt.timecode_skipped}, {"input_state", rt.input_state}, {"detail", rt.media_detail}, {"ptp", clock_status_json(rt)}});
}

std::map<std::string, std::string> widget_headers(const Config& config)
{
    return {{"Content-Security-Policy", "frame-ancestors " + config.widget_frame_ancestors},
        {"Access-Control-Allow-Origin", "*"}};
}

std::string widget_html(Runtime& rt, const std::string& id, bool latency, bool trend)
{
    const bool avsync = id == "avsync";
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"/>"
            "<style>html,body{margin:0;height:100%;font:14px/1.3 ui-sans-serif,sans-serif}"
            "body{background:#6b7280;color:#fff}.band{min-height:100%;padding:8px 10px;box-sizing:border-box}"
            ".offset{font-size:28px;font-weight:700}canvas{width:100%;height:36px;display:block}"
            "button{margin-right:6px}</style></head><body><div class=\"band\" id=\"root\">"
            "<div id=\"view\"></div><canvas id=\"trend\"></canvas></div><script>"
            "const kind=" << (avsync ? "\"avsync\"" : "\"generator\"") << ";"
            "const latency=" << (latency ? "true" : "false") << ";"
            "const trend=" << (trend ? "true" : "false") << ";"
            "function post(message){parent.postMessage(message,'*');}"
            "post({type:'widget-ready',id:kind});"
            "post({type:'widget-size',width:kind==='avsync'?240:80,height:kind==='avsync'?120:80});"
            "const colours={pass:'#1f8a4c',warn:'#d9822b',fail:'#c4392a',invalid:'#6b7280'};"
            "async function tick(){"
            "if(kind==='generator'){"
            "const g=await (await fetch('/api/v1/generator')).json();"
            "const banner=(!g.muted&&!g.stopped)?'<b>TEST SIGNAL ACTIVE</b>':'';"
            "document.getElementById('view').innerHTML=banner+'<div class=\"offset\">offset '+g.offset_ms+' ms</div>'"
            "+'<button id=\"mute\">'+(g.muted?'Unmute':'Mute')+'</button><button id=\"stop\">'+(g.stopped?'Start':'Stop')+'</button>';"
            "document.getElementById('trend').style.display='none';"
            "document.getElementById('mute').onclick=async()=>{await fetch('/api/v1/generator',{method:'PATCH',headers:{'Content-Type':'application/json'},body:JSON.stringify({muted:!g.muted})});tick();};"
            "document.getElementById('stop').onclick=async()=>{await fetch(g.stopped?'/api/v1/generator/start':'/api/v1/generator/stop',{method:'POST'});tick();};"
            "return;}"
            "const m=await (await fetch('/api/v1/measurement')).json();"
            "const grade=m.grade||'invalid';"
            "document.body.style.background=colours[grade]||colours.invalid;"
            "let line=grade==='invalid'?'<div>'+(m.sentence||m.reason||'no measurement')+'</div>':'<div class=\"offset\">'+(m.sentence||'')+'</div>';"
            "if(latency&&grade!=='invalid'&&(m.video_latency_ms!=null||m.audio_latency_ms!=null)){"
            "line+='<div>'+(m.latency_estimated?'estimated ':'')+'video '+(m.video_latency_ms==null?'–':m.video_latency_ms)+' ms · audio '+(m.audio_latency_ms==null?'–':m.audio_latency_ms)+' ms</div>';}"
            "document.getElementById('view').innerHTML=line;"
            "const canvas=document.getElementById('trend');"
            "if(!trend||grade==='invalid'){canvas.style.display='none';return;}"
            "canvas.style.display='block';"
            "const stats=await (await fetch('/api/v1/statistics')).json();"
            "const series=((stats.window&&stats.window.series)||[]).slice(-120);"
            "const ctx=canvas.getContext('2d');"
            "const w=canvas.width=canvas.clientWidth||240;const h=canvas.height=36;"
            "ctx.clearRect(0,0,w,h);ctx.strokeStyle='#fff';ctx.beginPath();"
            "series.forEach((value,index)=>{const x=series.length<2?0:index*(w-1)/(series.length-1);"
            "const y=h/2-Math.max(-40,Math.min(40,value))/40*(h/2-2);if(index===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);});"
            "ctx.stroke();}"
            "tick();setInterval(tick,1000);"
            "</script></body></html>";
    (void)rt;
    return html.str();
}

#ifndef AVSYNC_WITH_NMOS
void install_nmos(HttpServer& http, Runtime& rt)
{
    http.add("GET", "/", [&](const HttpRequest&) {
        return json_response({{"api", {{"versions", {"v1.3"}}}}, {"connection", {{"versions", {"v1.1"}}}}});
    });
    http.add("GET", "/x-nmos/node/v1.3/self", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        return json_response(nmos_node(rt.config));
    });
    http.add("GET", "/x-nmos/node/v1.3/", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        const std::string host = rt.config.nmos_host_address.empty() ? "127.0.0.1" : rt.config.nmos_host_address;
        const std::string base = "http://" + host + ":" + std::to_string(rt.config.nmos_port) + "/x-nmos/node/v1.3/";
        return json_response(nlohmann::json::array({base + "self/", base + "devices/", base + "sources/", base + "flows/", base + "senders/", base + "receivers/"}));
    });
    auto resources = [&](const std::string& kind) {
        return [&, kind](const HttpRequest&) {
            std::lock_guard lock(rt.mu);
            const bool gen = rt.config.role == "generator";
            const std::string device = uuid_v5("mxl-avsync/" + rt.config.seed() + "/device");
            const std::string video_flow = uuid_v5("mxl-avsync/" + rt.config.seed() + "/flow/video/" + rt.config.video_format);
            const std::string audio_flow = uuid_v5("mxl-avsync/" + rt.config.seed() + "/flow/audio");
            nlohmann::json tags = {{"urn:x-nmos:tag:grouphint/v1.0", {gen ? "AVSync Test:Video" : "AVSync In:Video"}},
                {"urn:x-mxl:tag:test-signal/v1.0", {"mxl-avsync"}}};
            if (kind == "devices") {
                return json_response(nlohmann::json::array(
                    {{{"id", device}, {"label", rt.config.label() + (gen ? " avsync generator" : " avsync analyzer")}, {"node_id", uuid_v5("mxl-avsync/" + rt.config.seed() + "/node")},
                        {"tags", tags}}}));
            }
            if (kind == "senders" && gen) {
                return json_response(nlohmann::json::array({{{"id", uuid_v5("mxl-avsync/" + rt.config.seed() + "/sender/video")},
                                                                {"label", "AVSync Test Video"}, {"flow_id", video_flow},
                                                                {"transport", "urn:x-nmos:transport:mxl"}, {"device_id", device}, {"tags", tags}},
                    {{"id", uuid_v5("mxl-avsync/" + rt.config.seed() + "/sender/audio")}, {"label", "AVSync Test Audio"}, {"flow_id", audio_flow},
                        {"transport", "urn:x-nmos:transport:mxl"}, {"device_id", device}, {"tags", tags}}}));
            }
            if (kind == "flows" && gen) {
                return json_response(nlohmann::json::array(
                    {{{"id", video_flow}, {"label", "AVSync Test Video"}, {"format", "urn:x-nmos:format:video"}, {"media_type", "video/v210"},
                         {"description", "TEST SIGNAL — AV sync"}, {"tags", tags}},
                        {{"id", audio_flow}, {"label", "AVSync Test Audio"}, {"format", "urn:x-nmos:format:audio"}, {"media_type", "audio/float32"},
                            {"tags", tags}}}));
            }
            if (kind == "receivers" && !gen) {
                return json_response(nlohmann::json::array({{{"id", uuid_v5("mxl-avsync/" + rt.config.seed() + "/receiver/video")},
                                                                {"label", "AVSync In Video"}, {"transport", "urn:x-nmos:transport:mxl"},
                                                                {"format", "urn:x-nmos:format:video"}},
                    {{"id", uuid_v5("mxl-avsync/" + rt.config.seed() + "/receiver/audio")}, {"label", "AVSync In Audio"},
                        {"transport", "urn:x-nmos:transport:mxl"}, {"format", "urn:x-nmos:format:audio"}}}));
            }
            return json_response(nlohmann::json::array());
        };
    };
    http.add("GET", "/x-nmos/node/v1.3/devices/", resources("devices"));
    http.add("GET", "/x-nmos/node/v1.3/sources/", resources("sources"));
    http.add("GET", "/x-nmos/node/v1.3/flows/", resources("flows"));
    http.add("GET", "/x-nmos/node/v1.3/senders/", resources("senders"));
    http.add("GET", "/x-nmos/node/v1.3/receivers/", resources("receivers"));
    http.add("GET", "/x-nmos/connection/v1.1/single/senders/*", [&](const HttpRequest& request) {
        std::lock_guard lock(rt.mu);
        const bool active = request.path.find("/active") != std::string::npos;
        return json_response({{"master_enable", !rt.stopped}, {"activation", {{"mode", active ? "activate_immediate" : nullptr}}},
            {"transport_params", {{{"mxl_domain_id", rt.config.mxl_output_domain_id}, {"mxl_flow_id", uuid_v5("mxl-avsync/" + rt.config.seed() + "/flow/video/" + rt.config.video_format)}}}}});
    });
    http.add("PATCH", "/x-nmos/connection/v1.1/single/receivers/*", [&](const HttpRequest& request) {
        auto body = nlohmann::json::parse(request.body.empty() ? "{}" : request.body);
        std::lock_guard lock(rt.mu);
        if (body.contains("master_enable") && body["master_enable"].is_boolean() && !body["master_enable"].get<bool>()) {
            rt.input_state = "not_routed";
        } else if (body.contains("transport_params")) {
            rt.input_state = "waiting";
        }
        return json_response({{"master_enable", rt.input_state != "not_routed"}, {"transport_params", body.value("transport_params", nlohmann::json::array())}});
    });
    http.add("GET", "/x-nmos/connection/v1.1/single/receivers/*", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        return json_response({{"master_enable", rt.input_state != "not_routed"},
            {"transport_params", {{{"mxl_domain_id", nullptr}, {"mxl_flow_id", nullptr}}}}});
    });
}
#endif

} // namespace

int run_service(Config config)
{
    if (config.role != "generator" && config.role != "analyzer") {
        throw std::runtime_error("AVSYNC_ROLE must be generator or analyzer");
    }
    Runtime rt;
    rt.config = std::move(config);
    rt.input_state = rt.config.role == "analyzer" ? "not_routed" : "running";
    if (rt.config.role == "generator") {
        // A picture uploaded before a restart stays in the state directory; draw it again.
        for (const char* name : {"source-ident.png", "source-ident.gif"}) {
            const auto path = std::filesystem::path(rt.config.state_dir) / name;
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) continue;
            auto clip = std::make_shared<SourceClip>();
            std::string error;
            if (clip->load(path.string(), error)) {
                rt.source_image_path = path.string();
                rt.source_image_frames = static_cast<int>(clip->frames.size());
                rt.source_clip = std::move(clip);
                break;
            }
        }
    }
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    HttpServer web;
    HttpServer nmos;
    install_web(web, rt);
#ifdef AVSYNC_WITH_NMOS
    std::unique_ptr<NmosNode> nmos_node;
    if (rt.config.nmos_enable) {
        nmos_node = std::make_unique<NmosNode>(rt.config);
        nmos_node->start();
    }
#else
    if (rt.config.nmos_enable) {
        install_nmos(nmos, rt);
    }
#endif
    std::exception_ptr error;
    std::thread web_thread([&] {
        try {
            if (rt.config.web_enable) web.serve(rt.config.web_port);
        } catch (...) {
            error = std::current_exception();
            g_stop = true;
        }
    });
    std::thread nmos_thread([&] {
#ifndef AVSYNC_WITH_NMOS
        try {
            if (rt.config.nmos_enable) nmos.serve(rt.config.nmos_port);
        } catch (...) {
            error = std::current_exception();
            g_stop = true;
        }
#else
        (void)nmos;
#endif
    });
    std::thread beat([&] {
        while (!g_stop) {
            {
                std::lock_guard lock(rt.mu);
                rt.heartbeat = std::chrono::steady_clock::now();
            }
#ifdef AVSYNC_WITH_NMOS
            if (nmos_node) {
                const bool registered = nmos_node->registered();
                const auto activations = nmos_node->activations();
                std::lock_guard lock(rt.mu);
                rt.nmos_registered = registered;
                rt.nmos_activations = activations;
            }
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
    std::thread ptp([&] {
        bool socket_error_logged = false;
        while (!g_stop) {
            std::string url;
            std::string socket_path;
            long max_error_us = 100;
            {
                std::lock_guard lock(rt.mu);
                url = rt.config.ptp_status_url;
                socket_path = rt.config.ptp_socket;
                max_error_us = rt.config.clock_max_error_us;
            }
            const KernelClock kernel = read_kernel_clock();
            const auto reading = read_clock_status(url, socket_path, max_error_us, kernel, 300);
            if (!reading.socket_error.empty() && !socket_error_logged) {
                std::cerr << nlohmann::json{{"level", "warn"}, {"event", "ptp_socket_fallback"},
                    {"reason", reading.socket_error}}.dump() << '\n';
                socket_error_logged = true;
            }
            {
                std::lock_guard lock(rt.mu);
                rt.clock_status = reading;
                rt.clock_synchronized = kernel.read_ok && !kernel.unsynced;
                rt.clock_esterror_s = kernel.read_ok ? static_cast<double>(kernel.esterror_us) / 1e6 : 0;
                rt.clock_tai_offset_s = kernel.read_ok ? kernel.tai_offset_s : 0;
            }
            for (int i = 0; i < 10 && !g_stop; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
#ifdef AVSYNC_WITH_MXL
    std::thread media([&] {
        LiveCallbacks callbacks;
        callbacks.copy_config = [&] {
            std::lock_guard lock(rt.mu);
            return rt.config;
        };
        callbacks.stopped = [&] {
            std::lock_guard lock(rt.mu);
            return rt.stopped;
        };
        callbacks.source_image = [&] {
            std::lock_guard lock(rt.mu);
            return rt.source_clip;
        };
        callbacks.set_result = [&](const std::string& flow, const std::string& domain) {
            std::lock_guard lock(rt.mu);
            rt.result_flow = flow;
            rt.result_domain = domain;
        };
        callbacks.set_status = [&](const MediaStatus& status) {
            std::lock_guard lock(rt.mu);
            rt.media_ready = status.ready && !rt.stopped;
            if (!status.live) {
                rt.media_live = false;
            }
            if (!status.state.empty()) {
                rt.input_state = status.state;
            }
            rt.timecode_skipped = status.timecode_skipped;
            rt.media_detail = status.detail;
            rt.video_flow = status.video_flow;
            rt.audio_flow = status.audio_flow;
            if (!status.result_flow.empty()) {
                rt.result_flow = status.result_flow;
                rt.result_domain = status.result_domain;
            }
            rt.grain_rows = status.grain_rows;
            rt.frame_width = status.width;
            rt.frame_height = status.height;
            rt.interlaced_flow = status.interlaced;
            rt.frame_rate_fps = status.rate_fps;
        };
        callbacks.add_event = [&](const Measurement& event) {
            std::lock_guard lock(rt.mu);
            rt.events.push_back(event);
            if (rt.events.size() > 1000) {
                rt.events.erase(rt.events.begin(), rt.events.begin() + static_cast<std::ptrdiff_t>(rt.events.size() - 1000));
            }
        };
        callbacks.count_generator_event = [&] {
            std::lock_guard lock(rt.mu);
            rt.generator_events += 1;
        };
        callbacks.count_video_grain = [&] {
            std::lock_guard lock(rt.mu);
            rt.output_video += 1;
        };
        callbacks.count_audio_grain = [&] {
            std::lock_guard lock(rt.mu);
            rt.output_audio += 1;
        };
        callbacks.count_late = [&](bool video) {
            std::lock_guard lock(rt.mu);
            if (video) {
                rt.late_video += 1;
            } else {
                rt.late_audio += 1;
            }
        };
        callbacks.count_resync = [&] {
            std::lock_guard lock(rt.mu);
            rt.resyncs += 1;
        };
        callbacks.clock_reading = [&] {
            std::lock_guard lock(rt.mu);
            ClockReading clock;
            clock.sync_state = rt.clock_status.sync_state;
            clock.accuracy_ns = rt.clock_status.accuracy_ns;
            return clock;
        };
#ifdef AVSYNC_WITH_NMOS
        callbacks.input_route = [](bool video) {
            const auto route = get_input_route(video);
            InputRoute out;
            out.enable = route.enable;
            out.domain_id = route.domain_id;
            out.flow_id = route.flow_id;
            out.sender_id = route.sender_id;
            return out;
        };
#endif
        run_live_media(g_stop, std::move(callbacks));
    });
#endif
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#ifdef AVSYNC_WITH_NMOS
    if (nmos_node) {
        nmos_node->stop();
    }
#endif
    web.stop();
    nmos.stop();
    if (web_thread.joinable()) web_thread.join();
    if (nmos_thread.joinable()) nmos_thread.join();
    if (beat.joinable()) beat.join();
    if (ptp.joinable()) ptp.join();
#ifdef AVSYNC_WITH_MXL
    if (media.joinable()) media.join();
#endif
    if (error) std::rethrow_exception(error);
    return 143;
}

} // namespace avsync
