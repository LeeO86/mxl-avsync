#include "service.hpp"

#include "http.hpp"
#include "ptp.hpp"
#include "paths.hpp"
#include "picture.hpp"
#include "report.hpp"
#include "uuid.hpp"

#ifdef AVSYNC_WITH_MXL
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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace avsync {
namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop = true; }

struct Runtime {
    Config config;
    std::mutex mu;
    bool stopped = false;
    bool media_ready = false;
    bool media_live = true;
    bool nmos_registered = false;
    std::string input_state = "not_routed";
    std::vector<Measurement> events;
    int events_epoch = 0;
    double generator_events = 0;
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
    std::string source_image_path;
    int source_image_frames = 0;
    std::string ptp_sync_state = "unknown";
    std::string ptp_accuracy_ns = "unknown";
    std::chrono::steady_clock::time_point heartbeat = std::chrono::steady_clock::now();
};

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
            GradeWindows{rt.config.pass_min_ms, rt.config.pass_max_ms, rt.config.warn_min_ms, rt.config.warn_max_ms}, 0, rt.config.stale_s);
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
    for (const char* le : {"-0.5", "-0.2", "-0.1", "-0.06", "-0.04", "-0.015", "-0.005", "0.005", "0.015", "0.04", "0.06", "0.1", "0.2", "0.5", "+Inf"}) {
        os << "mxl_avsync_av_offset_seconds_bucket{le=\"" << le << "\"} 0\n";
    }
    os << "mxl_avsync_av_offset_seconds_sum 0\nmxl_avsync_av_offset_seconds_count 0\n";
    os << "# TYPE mxl_avsync_video_drops_total counter\n";
    os << "# TYPE mxl_avsync_video_duplicates_total counter\n";
    os << "mxl_avsync_video_drops_total 0\nmxl_avsync_video_duplicates_total 0\n";
    os << "# TYPE mxl_avsync_audio_correlation gauge\n";
    os << "# TYPE mxl_avsync_audio_level_dbfs gauge\n";
    os << "mxl_avsync_audio_correlation 0\nmxl_avsync_audio_level_dbfs 0\n";
    os << "# TYPE mxl_avsync_reference_sync gauge\n";
    for (const char* state : {"locked", "unlocked", "unknown"}) {
        const int on = ana && rt.ptp_sync_state == state ? 1 : 0;
        os << "mxl_avsync_reference_sync{type=\"tai-ptp\",state=\"" << state << "\"} " << on << "\n";
    }
    os << "# TYPE mxl_avsync_nmos_registry_up gauge\n";
    os << "mxl_avsync_nmos_registry_up " << (rt.nmos_registered ? 1 : 0) << "\n";
    os << "# TYPE mxl_avsync_nmos_activations_total counter\n";
    os << "mxl_avsync_nmos_activations_total 0\n";
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
        const auto age = std::chrono::steady_clock::now() - rt.heartbeat;
        bool media_live = true;
        {
            std::lock_guard lock(rt.mu);
            media_live = rt.media_live;
        }
        const bool heartbeat_ok = age < std::chrono::seconds(5);
        const bool ok = heartbeat_ok && media_live;
        return json_response({{"status", ok ? "ok" : (media_live ? "stale" : "domain")}}, ok ? 200 : 503);
    });
    http.add("GET", "/readyz", [&](const HttpRequest&) {
        std::lock_guard lock(rt.mu);
        const bool ok = rt.media_ready;
        return json_response({{"ready", ok}, {"role", rt.config.role}}, ok ? 200 : 503);
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
            {"input_state", rt.input_state}, {"detail", rt.media_detail}, {"nmos_registered", rt.nmos_registered},
            {"audio_enable", rt.config.audio_enable}, {"video_flow", rt.video_flow}, {"audio_flow", rt.audio_flow},
            {"result_flow", rt.result_flow}, {"result_domain", rt.result_domain},
            {"width", rt.frame_width}, {"height", rt.frame_height}, {"grain_rows", rt.grain_rows},
            {"interlaced", rt.interlaced_flow},
            {"ptp", {{"sync_state", rt.ptp_sync_state}, {"accuracy_ns", rt.ptp_accuracy_ns}}}});
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
            {"event_rate", rt.config.event_rate}, {"format", rt.config.video_format},
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
        SourceClip clip;
        std::string error;
        if (!clip.load(path.string(), error)) {
            std::filesystem::remove(path);
            return json_response({{"error", error}}, 400);
        }
        const auto other = dir / (ext == ".png" ? "source-ident.gif" : "source-ident.png");
        std::error_code ec;
        std::filesystem::remove(other, ec);
        rt.source_image_path = path.string();
        rt.source_image_frames = static_cast<int>(clip.frames.size());
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
        const std::string grade = current_grade(rt.events, rt.config.grade_events, windows, 0, rt.config.stale_s);
        std::string sentence = "no measurement";
        if (!rt.events.empty() && rt.events.back().av_offset_ms) {
            sentence = describe_offset(*rt.events.back().av_offset_ms).line;
        }
        return json_response({{"grade", grade}, {"sentence", sentence}, {"input_state", rt.input_state}, {"events", rt.events.size()}});
    });
    http.add("GET", "/api/v1/events", [&](const HttpRequest& request) {
        std::lock_guard lock(rt.mu);
        const auto query = query_map(request.query);
        uint64_t since = query.count("since") ? std::strtoull(query.at("since").c_str(), nullptr, 10) : 0;
        int limit = query.count("limit") ? std::atoi(query.at("limit").c_str()) : 100;
        nlohmann::json list = nlohmann::json::array();
        for (const auto& event : rt.events) {
            if (event.seq < since) continue;
            list.push_back(measurement_to_json(event));
            if (static_cast<int>(list.size()) >= limit) break;
        }
        return json_response({{"events", list}});
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
        return json_response(config_settings(rt.config));
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
        const std::string html = widget_html(rt, "avsync", query.count("latency") ? query.at("latency") != "false" : true,
            query.count("trend") ? query.at("trend") != "false" : true);
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
    return json_response({{"role", rt.config.role}, {"stopped", rt.stopped}, {"media_ready", rt.media_ready}});
}

std::map<std::string, std::string> widget_headers(const Config& config)
{
    return {{"Content-Security-Policy", "frame-ancestors " + config.widget_frame_ancestors},
        {"Access-Control-Allow-Origin", "*"}};
}

std::string widget_html(Runtime& rt, const std::string& id, bool latency, bool trend)
{
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><style>body{margin:0;font:14px sans-serif;background:#111;color:#eee}"
            ".band{padding:12px}button{margin-right:8px}</style></head><body><div class=\"band\" id=\"root\"></div><script>"
            "parent.postMessage({type:'widget-ready',id:'"
         << id << "'},'*');parent.postMessage({type:'widget-size',width:240,height:" << (id == "avsync" ? 120 : 80)
         << "},'*');";
    if (id == "avsync") {
        html << "document.getElementById('root').textContent=" << nlohmann::json("NO EVENTS").dump() << ";";
        (void)latency;
        (void)trend;
    } else {
        html << "const muted=" << (!rt.config.audio_enable ? "true" : "false") << ";const stopped=" << (rt.stopped ? "true" : "false")
             << ";document.getElementById('root').innerHTML='<b>TEST SIGNAL ACTIVE</b><div>offset '+"
             << rt.config.offset_ms
             << "+' ms</div><button onclick=\"fetch(\\'/api/v1/generator\\',{method:\\'PATCH\\',headers:{\\'Content-Type\\':\\'application/json\\'},body:\\'{\\\\\\\"muted\\\\\\\":true}\\'})\">Mute</button><button onclick=\"fetch(\\'/api/v1/generator/stop\\',{method:\\'POST\\'})\">Stop</button>';";
    }
    html << "</script></body></html>";
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
            rt.heartbeat = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });
    std::thread ptp([&] {
        while (!g_stop) {
            std::string url;
            {
                std::lock_guard lock(rt.mu);
                url = rt.config.ptp_status_url;
            }
            const PtpReading reading = read_ptp_status(url, 300);
            {
                std::lock_guard lock(rt.mu);
                rt.ptp_sync_state = reading.sync_state;
                rt.ptp_accuracy_ns = reading.accuracy_ns;
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
            clock.sync_state = rt.ptp_sync_state;
            clock.accuracy_ns = rt.ptp_accuracy_ns;
            return clock;
        };
#ifdef AVSYNC_WITH_NMOS
        callbacks.input_route = [](bool video) {
            const auto route = get_input_route(video);
            InputRoute out;
            out.enable = route.enable;
            out.domain_id = route.domain_id;
            out.flow_id = route.flow_id;
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
