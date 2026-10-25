#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace avsync {

struct Config {
    std::string role;
    int event_rate = 2;
    int chirp_ms = 30;
    double chirp_low_hz = 700;
    double chirp_high_hz = 2500;
    std::string state_dir = "/config";
    std::string config_file;
    std::string video_format = "1080p50";
    int audio_channels = 2;
    double offset_ms = 0;
    double chirp_level_dbfs = -12;
    bool audio_enable = true;
    int audio_lead_ms = 20;
    int qr_period_ms = 1000;
    std::string test_id;
    std::string title;
    std::string source;
    std::string subtitle;
    std::string notes;
    int audio_channel = 0;
    int audio_window_ms = 10;
    double audio_threshold = 0.5;
    int qr_rate = 2;
    double pass_min_ms = -5;
    double pass_max_ms = 15;
    double warn_min_ms = -40;
    double warn_max_ms = 60;
    int grade_events = 10;
    double stale_s = 5;
    int stats_window = 120;
    bool event_log = true;
    bool result_enable = true;
    std::string result_format = "960x540";
    int result_rate = 25;
    int hold_ms = 1000;
    std::string ptp_status_url;
    int clock_max_error_us = 100;
    std::string host_id;
    std::string mxl_domain_scan = "/Volumes/mxl";
    std::string mxl_output_domain_dir;
    std::string mxl_output_domain_id;
    int64_t mxl_history_ns = 200000000;
    bool mxl_cleanup_on_exit = false;
    bool nmos_enable = true;
    std::string nmos_registry_address;
    int nmos_registry_port = 3210;
    std::string nmos_query_address;
    int nmos_query_port = 0;
    bool nmos_dns_sd = false;
    int nmos_port = 3332;
    std::string nmos_seed;
    std::string nmos_label;
    std::string nmos_host_address;
    nlohmann::json nmos_tags = nlohmann::json::object();
    bool web_enable = true;
    int web_port = 8180;
    std::string widget_frame_ancestors = "'self'";
    std::string log_level = "info";
    std::string log_format = "json";
    int shutdown_timeout_s = 10;
    bool stopped = false;
    std::map<std::string, std::string> from_env;

    std::string seed() const;
    std::string label() const;
};

// Throws std::runtime_error on invalid configuration. Precedence: environment, file, defaults.
Config load_config(const std::map<std::string, std::string>& extra = {});
nlohmann::json config_settings(const Config& config);
void apply_settings(Config& config, const nlohmann::json& flat, std::vector<std::string>* skipped_env);
bool is_restart_key(const std::string& key);
bool is_live_key(const std::string& key);

} // namespace avsync
