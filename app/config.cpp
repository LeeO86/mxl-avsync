#include "config.hpp"

#include "avsync/avsync.hpp"
#include "uuid.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace avsync {
namespace {

static std::string short_host(std::string name)
{
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        name.resize(dot);
    }
    return name.empty() ? std::string("localhost") : name;
}

static std::string hostname()
{
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) != 0) {
        return "localhost";
    }
    return short_host(buf);
}

std::string env_or(const char* key, const std::string& fallback, std::map<std::string, std::string>& from_env)
{
    if (const char* value = std::getenv(key); value != nullptr) {
        from_env[key] = value;
        return value;
    }
    return fallback;
}

bool parse_bool(const std::string& text)
{
    return text == "1" || text == "true" || text == "TRUE" || text == "yes";
}

double num(const std::string& text, const char* key)
{
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        throw std::runtime_error(std::string(key) + " is not a number");
    }
    return value;
}

void require(bool ok, const std::string& message)
{
    if (!ok) {
        throw std::runtime_error(message);
    }
}

} // namespace

std::string Config::seed() const
{
    return nmos_seed.empty() ? host_id + "-avsync" : nmos_seed;
}

std::string Config::label() const
{
    return nmos_label.empty() ? host_id : nmos_label;
}

namespace {
int event_rate_value(const std::string& value) {
    if (value != "1" && value != "2" && value != "4") throw std::runtime_error("AVSYNC_EVENT_RATE must be 1, 2 or 4");
    return std::stoi(value);
}
} // namespace

bool is_restart_key(const std::string& key)
{
    return key == "AVSYNC_ROLE" || key == "AVSYNC_STATE_DIR" || key == "AVSYNC_CONFIG_FILE" || key == "AVSYNC_AUDIO_LEAD_MS" || key == "AVSYNC_VIDEO_LEAD_MS"
        || key == "AVSYNC_AUDIO_WINDOW_MS" || key == "AVSYNC_PTP_STATUS_URL" || key == "AVSYNC_PTP_SOCKET" || key == "AVSYNC_CLOCK_MAX_ERROR_US" || key.rfind("MXL_", 0) == 0 || key.rfind("NMOS_", 0) == 0
        || key.rfind("WEB_", 0) == 0 || key == "HOST_ID" || key == "WIDGET_FRAME_ANCESTORS" || key == "LOG_FORMAT" || key == "SHUTDOWN_TIMEOUT_S";
}

bool is_live_key(const std::string& key)
{
    return key == "AVSYNC_AV_OFFSET_MS" || key == "AVSYNC_CHIRP_LEVEL_DBFS" || key == "AVSYNC_AUDIO_ENABLE"
        || key == "AVSYNC_EVENT_RATE" || key == "AVSYNC_AUDIO_TIMECODE"
        || key == "AVSYNC_TEST_ID" || key == "AVSYNC_TITLE" || key == "AVSYNC_SOURCE" || key == "AVSYNC_SUBTITLE" || key == "AVSYNC_NOTES"
        || key == "AVSYNC_AUDIO_CHANNEL" || key == "AVSYNC_AUDIO_THRESHOLD" || key == "AVSYNC_QR_RATE"
        || key == "AVSYNC_PASS_MIN_MS" || key == "AVSYNC_PASS_MAX_MS" || key == "AVSYNC_WARN_MIN_MS" || key == "AVSYNC_WARN_MAX_MS"
        || key == "AVSYNC_GRADE_EVENTS" || key == "AVSYNC_STALE_S" || key == "AVSYNC_STATS_WINDOW" || key == "LOG_LEVEL";
}

Config load_config(const std::map<std::string, std::string>& extra)
{
    Config config;
    config.host_id = hostname();
    std::map<std::string, std::string> file_values;
    const char* file_env = std::getenv("AVSYNC_CONFIG_FILE");
    std::string state = std::getenv("AVSYNC_STATE_DIR") ? std::getenv("AVSYNC_STATE_DIR") : "/config";
    const std::string path = (file_env && *file_env) ? file_env : state + "/config.json";
    {
        std::ifstream in(path);
        if (in) {
            nlohmann::json doc;
            in >> doc;
            if (doc.is_object()) {
                for (auto it = doc.begin(); it != doc.end(); ++it) {
                    if (it.value().is_string()) {
                        file_values[it.key()] = it.value().get<std::string>();
                    } else {
                        file_values[it.key()] = it.value().dump();
                    }
                }
            }
        }
    }
    auto pick = [&](const char* key, const std::string& fallback) {
        if (const char* env = std::getenv(key)) {
            config.from_env[key] = env;
            return std::string(env);
        }
        if (auto it = extra.find(key); it != extra.end()) {
            return it->second;
        }
        if (auto it = file_values.find(key); it != file_values.end()) {
            return it->second;
        }
        return fallback;
    };
    config.role = pick("AVSYNC_ROLE", "");
    config.audio_timecode = pick("AVSYNC_AUDIO_TIMECODE", "1200");
    config.event_rate = event_rate_value(pick("AVSYNC_EVENT_RATE", "2"));
    config.chirp_ms = static_cast<int>(num(pick("AVSYNC_CHIRP_MS", "30"), "AVSYNC_CHIRP_MS"));
    config.chirp_low_hz = num(pick("AVSYNC_CHIRP_LOW_HZ", "700"), "AVSYNC_CHIRP_LOW_HZ");
    config.chirp_high_hz = num(pick("AVSYNC_CHIRP_HIGH_HZ", "2500"), "AVSYNC_CHIRP_HIGH_HZ");
    config.state_dir = pick("AVSYNC_STATE_DIR", "/config");
    config.config_file = pick("AVSYNC_CONFIG_FILE", "");
    config.video_format = pick("AVSYNC_VIDEO_FORMAT", "1080p50");
    config.audio_channels = static_cast<int>(num(pick("AVSYNC_AUDIO_CHANNELS", "2"), "AVSYNC_AUDIO_CHANNELS"));
    config.offset_ms = num(pick("AVSYNC_AV_OFFSET_MS", "0"), "AVSYNC_AV_OFFSET_MS");
    config.chirp_level_dbfs = num(pick("AVSYNC_CHIRP_LEVEL_DBFS", "-12"), "AVSYNC_CHIRP_LEVEL_DBFS");
    config.audio_enable = parse_bool(pick("AVSYNC_AUDIO_ENABLE", "true"));
    config.audio_lead_ms = static_cast<int>(num(pick("AVSYNC_AUDIO_LEAD_MS", "20"), "AVSYNC_AUDIO_LEAD_MS"));
    config.video_lead_ms = static_cast<int>(num(pick("AVSYNC_VIDEO_LEAD_MS", "20"), "AVSYNC_VIDEO_LEAD_MS"));
    config.qr_period_ms = static_cast<int>(num(pick("AVSYNC_QR_PERIOD_MS", "1000"), "AVSYNC_QR_PERIOD_MS"));
    config.host_id = short_host(pick("HOST_ID", config.host_id));
    config.nmos_seed = pick("NMOS_SEED", config.host_id + "-avsync");
    config.test_id = pick("AVSYNC_TEST_ID", config.nmos_seed);
    config.title = pick("AVSYNC_TITLE", "");
    config.source = pick("AVSYNC_SOURCE", "");
    config.subtitle = pick("AVSYNC_SUBTITLE", "");
    config.notes = pick("AVSYNC_NOTES", "");
    config.audio_channel = static_cast<int>(num(pick("AVSYNC_AUDIO_CHANNEL", "0"), "AVSYNC_AUDIO_CHANNEL"));
    config.audio_window_ms = static_cast<int>(num(pick("AVSYNC_AUDIO_WINDOW_MS", "10"), "AVSYNC_AUDIO_WINDOW_MS"));
    config.audio_threshold = num(pick("AVSYNC_AUDIO_THRESHOLD", "0.5"), "AVSYNC_AUDIO_THRESHOLD");
    config.qr_rate = static_cast<int>(num(pick("AVSYNC_QR_RATE", "2"), "AVSYNC_QR_RATE"));
    config.pass_min_ms = num(pick("AVSYNC_PASS_MIN_MS", "-5"), "AVSYNC_PASS_MIN_MS");
    config.pass_max_ms = num(pick("AVSYNC_PASS_MAX_MS", "15"), "AVSYNC_PASS_MAX_MS");
    config.warn_min_ms = num(pick("AVSYNC_WARN_MIN_MS", "-40"), "AVSYNC_WARN_MIN_MS");
    config.warn_max_ms = num(pick("AVSYNC_WARN_MAX_MS", "60"), "AVSYNC_WARN_MAX_MS");
    config.grade_events = static_cast<int>(num(pick("AVSYNC_GRADE_EVENTS", "10"), "AVSYNC_GRADE_EVENTS"));
    config.stale_s = num(pick("AVSYNC_STALE_S", "5"), "AVSYNC_STALE_S");
    config.stats_window = static_cast<int>(num(pick("AVSYNC_STATS_WINDOW", "120"), "AVSYNC_STATS_WINDOW"));
    config.event_log = parse_bool(pick("AVSYNC_EVENT_LOG", "true"));
    config.result_enable = parse_bool(pick("AVSYNC_RESULT_ENABLE", "true"));
    config.result_format = pick("AVSYNC_RESULT_FORMAT", "960x540");
    config.result_rate = static_cast<int>(num(pick("AVSYNC_RESULT_RATE", "25"), "AVSYNC_RESULT_RATE"));
    config.hold_ms = static_cast<int>(num(pick("AVSYNC_HOLD_MS", "1000"), "AVSYNC_HOLD_MS"));
    config.ptp_status_url = pick("AVSYNC_PTP_STATUS_URL", "");
    config.ptp_socket = pick("AVSYNC_PTP_SOCKET", "");
    config.clock_max_error_us = static_cast<int>(num(pick("AVSYNC_CLOCK_MAX_ERROR_US", "100"), "AVSYNC_CLOCK_MAX_ERROR_US"));
    config.mxl_domain_scan = pick("MXL_DOMAIN_SCAN_PATH", "/Volumes/mxl");
    config.mxl_output_domain_id = pick("MXL_OUTPUT_DOMAIN_ID", uuid_v5("mxl-avsync/" + config.seed() + "/domain"));
    config.mxl_output_domain_dir = pick("MXL_OUTPUT_DOMAIN_DIR", config.mxl_domain_scan + "/avsync-" + config.mxl_output_domain_id.substr(0, 8));
    config.mxl_history_ns = static_cast<int64_t>(num(pick("MXL_HISTORY_DURATION_NS", "200000000"), "MXL_HISTORY_DURATION_NS"));
    config.mxl_cleanup_on_exit = parse_bool(pick("MXL_CLEANUP_ON_EXIT", "false"));
    config.nmos_enable = parse_bool(pick("NMOS_ENABLE", "true"));
    config.nmos_registry_address = pick("NMOS_REGISTRY_ADDRESS", "");
    config.nmos_registry_port = static_cast<int>(num(pick("NMOS_REGISTRY_PORT", "3210"), "NMOS_REGISTRY_PORT"));
    config.nmos_query_address = pick("NMOS_QUERY_ADDRESS", config.nmos_registry_address);
    config.nmos_query_port = static_cast<int>(num(pick("NMOS_QUERY_PORT", std::to_string(config.nmos_registry_port + 1)), "NMOS_QUERY_PORT"));
    config.nmos_dns_sd = parse_bool(pick("NMOS_DNS_SD", "false"));
    config.nmos_port = static_cast<int>(num(pick("NMOS_PORT", "3332"), "NMOS_PORT"));
    config.nmos_label = pick("NMOS_LABEL", "");
    config.nmos_host_address = pick("NMOS_HOST_ADDRESS", "127.0.0.1");
    config.web_enable = parse_bool(pick("WEB_ENABLE", "true"));
    config.web_port = static_cast<int>(num(pick("WEB_PORT", "8180"), "WEB_PORT"));
    config.widget_frame_ancestors = pick("WIDGET_FRAME_ANCESTORS", "'self'");
    config.log_level = pick("LOG_LEVEL", "info");
    config.log_format = pick("LOG_FORMAT", "json");
    config.shutdown_timeout_s = static_cast<int>(num(pick("SHUTDOWN_TIMEOUT_S", "10"), "SHUTDOWN_TIMEOUT_S"));
    require(config.audio_timecode == "legacy" || config.audio_timecode == "600" || config.audio_timecode == "1200", "AVSYNC_AUDIO_TIMECODE must be legacy, 600 or 1200");
    if (config.audio_timecode != "legacy") {
        require(config.event_rate == 1 || config.event_rate == 2, "timecode supports 1 or 2 events/s");
        require(config.audio_timecode != "600" || config.event_rate == 1, "600 baud requires 1 event/s");
        require(config.chirp_ms == 30 && config.chirp_low_hz == 700 && config.chirp_high_hz == 2500, "timecode prefix is fixed at 30 ms / 700-2500 Hz");
    }
    require(config.event_rate == 1 || config.event_rate == 2 || config.event_rate == 4, "AVSYNC_EVENT_RATE must be 1, 2 or 4");
    require(config.chirp_ms >= 10 && config.chirp_ms <= 100, "AVSYNC_CHIRP_MS out of range");
    require(config.chirp_low_hz >= 100 && config.chirp_high_hz <= 20000 && config.chirp_low_hz < config.chirp_high_hz, "chirp band");
    require(config.audio_channels >= 1 && config.audio_channels <= 16, "AVSYNC_AUDIO_CHANNELS");
    require(config.offset_ms >= -500 && config.offset_ms <= 500, "AVSYNC_AV_OFFSET_MS");
    require(config.chirp_level_dbfs >= -40 && config.chirp_level_dbfs <= -1, "AVSYNC_CHIRP_LEVEL_DBFS");
    require(config.video_lead_ms >= 0 && config.video_lead_ms <= 100, "AVSYNC_VIDEO_LEAD_MS must be 0-100");
    require(config.qr_period_ms >= 500 && config.qr_period_ms <= 5000, "AVSYNC_QR_PERIOD_MS");
    require(config.title.size() <= 80 && config.source.size() <= 80 && config.subtitle.size() <= 80 && config.notes.size() <= 80, "free text longer than 80");
    GradeWindows windows{config.pass_min_ms, config.pass_max_ms, config.warn_min_ms, config.warn_max_ms};
    require(windows_valid(windows), "grade windows must satisfy warn_min <= pass_min <= 0 <= pass_max <= warn_max");
    require(parse_video_format(config.video_format).has_value(), "AVSYNC_VIDEO_FORMAT");
    require(config.web_port > 0 && config.nmos_port > 0 && config.web_port != config.nmos_port, "ports collide or are unset");
    (void)env_or;
    return config;
}

nlohmann::json config_settings(const Config& config)
{
    auto b = [](bool v) { return v ? "true" : "false"; };
    return {
        {"AVSYNC_ROLE", config.role},
        {"AVSYNC_AUDIO_TIMECODE", config.audio_timecode},
        {"AVSYNC_EVENT_RATE", std::to_string(config.event_rate)},
        {"AVSYNC_CHIRP_MS", std::to_string(config.chirp_ms)},
        {"AVSYNC_CHIRP_LOW_HZ", std::to_string(config.chirp_low_hz)},
        {"AVSYNC_CHIRP_HIGH_HZ", std::to_string(config.chirp_high_hz)},
        {"AVSYNC_VIDEO_FORMAT", config.video_format},
        {"AVSYNC_AUDIO_CHANNELS", std::to_string(config.audio_channels)},
        {"AVSYNC_AV_OFFSET_MS", std::to_string(config.offset_ms)},
        {"AVSYNC_CHIRP_LEVEL_DBFS", std::to_string(config.chirp_level_dbfs)},
        {"AVSYNC_AUDIO_ENABLE", b(config.audio_enable)},
        {"AVSYNC_TITLE", config.title},
        {"AVSYNC_SOURCE", config.source},
        {"AVSYNC_SUBTITLE", config.subtitle},
        {"AVSYNC_NOTES", config.notes},
        {"AVSYNC_TEST_ID", config.test_id},
        {"AVSYNC_PASS_MIN_MS", std::to_string(config.pass_min_ms)},
        {"AVSYNC_PASS_MAX_MS", std::to_string(config.pass_max_ms)},
        {"AVSYNC_WARN_MIN_MS", std::to_string(config.warn_min_ms)},
        {"AVSYNC_WARN_MAX_MS", std::to_string(config.warn_max_ms)},
        {"AVSYNC_GRADE_EVENTS", std::to_string(config.grade_events)},
        {"AVSYNC_STALE_S", std::to_string(config.stale_s)},
        {"AVSYNC_STATS_WINDOW", std::to_string(config.stats_window)},
        {"AVSYNC_AUDIO_THRESHOLD", std::to_string(config.audio_threshold)},
        {"AVSYNC_AUDIO_CHANNEL", std::to_string(config.audio_channel)},
        {"AVSYNC_RESULT_ENABLE", b(config.result_enable)},
        {"AVSYNC_QR_RATE", std::to_string(config.qr_rate)},
        {"AVSYNC_PTP_STATUS_URL", config.ptp_status_url},
        {"AVSYNC_PTP_SOCKET", config.ptp_socket},
        {"AVSYNC_CLOCK_MAX_ERROR_US", std::to_string(config.clock_max_error_us)},
        {"LOG_LEVEL", config.log_level},
    };
}

void apply_settings(Config& config, const nlohmann::json& flat, std::vector<std::string>* skipped_env)
{
    Config next = config;
    auto text = [](const nlohmann::json& value) { return value.is_string() ? value.get<std::string>() : value.dump(); };
    for (auto it = flat.begin(); it != flat.end(); ++it) {
        if (next.from_env.count(it.key())) {
            if (skipped_env || !is_live_key(it.key())) {
                if (skipped_env) {
                    skipped_env->push_back(it.key());
                }
                continue;
            }
        }
        const std::string value = text(it.value());
        const std::string& key = it.key();
        if (key == "AVSYNC_AUDIO_TIMECODE") next.audio_timecode = value;
        else if (key == "AVSYNC_EVENT_RATE") next.event_rate = event_rate_value(value);
        else if (key == "AVSYNC_CHIRP_MS") next.chirp_ms = static_cast<int>(num(value, key.c_str()));
        else if (key == "AVSYNC_CHIRP_LOW_HZ") next.chirp_low_hz = num(value, key.c_str());
        else if (key == "AVSYNC_CHIRP_HIGH_HZ") next.chirp_high_hz = num(value, key.c_str());
        else if (key == "AVSYNC_VIDEO_FORMAT") next.video_format = value;
        else if (key == "AVSYNC_AUDIO_CHANNELS") next.audio_channels = static_cast<int>(num(value, key.c_str()));
        else if (key == "AVSYNC_AV_OFFSET_MS") next.offset_ms = num(value, key.c_str());
        else if (key == "AVSYNC_CHIRP_LEVEL_DBFS") next.chirp_level_dbfs = num(value, key.c_str());
        else if (key == "AVSYNC_AUDIO_ENABLE") next.audio_enable = parse_bool(value);
        else if (key == "AVSYNC_TITLE") next.title = value;
        else if (key == "AVSYNC_SOURCE") next.source = value;
        else if (key == "AVSYNC_SUBTITLE") next.subtitle = value;
        else if (key == "AVSYNC_NOTES") next.notes = value;
        else if (key == "AVSYNC_TEST_ID") next.test_id = value;
        else if (key == "AVSYNC_PASS_MIN_MS") next.pass_min_ms = num(value, key.c_str());
        else if (key == "AVSYNC_PASS_MAX_MS") next.pass_max_ms = num(value, key.c_str());
        else if (key == "AVSYNC_WARN_MIN_MS") next.warn_min_ms = num(value, key.c_str());
        else if (key == "AVSYNC_WARN_MAX_MS") next.warn_max_ms = num(value, key.c_str());
        else if (key == "AVSYNC_GRADE_EVENTS") next.grade_events = static_cast<int>(num(value, key.c_str()));
        else if (key == "AVSYNC_STALE_S") next.stale_s = num(value, key.c_str());
        else if (key == "AVSYNC_STATS_WINDOW") next.stats_window = static_cast<int>(num(value, key.c_str()));
        else if (key == "AVSYNC_AUDIO_THRESHOLD") next.audio_threshold = num(value, key.c_str());
        else if (key == "AVSYNC_AUDIO_CHANNEL") next.audio_channel = static_cast<int>(num(value, key.c_str()));
        else if (key == "AVSYNC_QR_RATE") next.qr_rate = static_cast<int>(num(value, key.c_str()));
        else if (key == "AVSYNC_RESULT_ENABLE") next.result_enable = parse_bool(value);
        else if (key == "LOG_LEVEL") next.log_level = value;
        else if (is_restart_key(key)) {
            continue;
        } else {
            throw std::runtime_error("unknown setting " + key);
        }
    }
    require(windows_valid(GradeWindows{next.pass_min_ms, next.pass_max_ms, next.warn_min_ms, next.warn_max_ms}), "grade windows");
    require(next.offset_ms >= -500 && next.offset_ms <= 500, "AVSYNC_AV_OFFSET_MS");
    require(next.audio_timecode == "legacy" || next.audio_timecode == "600" || next.audio_timecode == "1200", "AVSYNC_AUDIO_TIMECODE must be legacy, 600 or 1200");
    if (next.audio_timecode != "legacy") {
        require(next.event_rate == 1 || next.event_rate == 2, "timecode supports 1 or 2 events/s");
        require(next.audio_timecode != "600" || next.event_rate == 1, "600 baud requires 1 event/s");
        require(next.chirp_ms == 30 && next.chirp_low_hz == 700 && next.chirp_high_hz == 2500, "timecode prefix is fixed at 30 ms / 700-2500 Hz");
    }
    require(next.event_rate == 1 || next.event_rate == 2 || next.event_rate == 4, "AVSYNC_EVENT_RATE");
    require(parse_video_format(next.video_format).has_value(), "AVSYNC_VIDEO_FORMAT");
    config = std::move(next);
}

} // namespace avsync
