#include "file_tool.hpp"
#include "config.hpp"
#include "paths.hpp"
#include "service.hpp"

#include "avsync/avsync.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage()
{
    std::cout << R"(mxl-avsync )" << avsync::kAppVersion << R"(

  mxl-avsync generate --format 1080p50 --duration 60 --offset 0 --out test.mov
      [--offset-sweep -40,-20,0,20,40] [--event-rate 1|2] [--audio-timecode 1200|600|legacy] [--audio-rate 48000]
      [--channels 2] [--container mov|mp4|mkv] [--video-codec prores|v210|h264]
      [--audio-codec pcm24|pcm16|aac] [--title ...] [--source ...] [--subtitle ...]
      [--source-image png|gif]
      [--notes ...] [--allow-container-shift]
  mxl-avsync analyze <file> [--manifest m.json] [--json out.json] [--from s] [--to s]
      [--channel 0] [--fail-on warn|fail] [--locate]
  mxl-avsync paths
  mxl-avsync validate <file.json>
  mxl-avsync merge <a.json> <b.json> [--json out.json]
  mxl-avsync serve

Exit codes: 0 ok, 2 analyze found no event, 75 startup failure, 78 bad arguments, 143 signal.
Positive AV offset means audio is late.
)";
}

std::vector<double> split_doubles(const std::string& text)
{
    std::vector<double> values;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            values.push_back(std::strtod(item.c_str(), nullptr));
        }
    }
    return values;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage();
        return 0;
    }
    const std::string command = argv[1];
    if (command == "--help" || command == "-h" || command == "help") {
        usage();
        return 0;
    }
    if (command == "--version" || command == "-V") {
        std::cout << "mxl-avsync " << avsync::kAppVersion << " core " << avsync::kCoreVersion << "\n";
        return 0;
    }
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    auto opt = [&](const std::string& key, const std::string& fallback) {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == key) {
                return args[i + 1];
            }
        }
        return fallback;
    };
    auto flag = [&](const std::string& key) {
        for (const auto& arg : args) {
            if (arg == key) {
                return true;
            }
        }
        return false;
    };
    std::vector<std::string> positional;
    for (size_t i = 0; i < args.size(); ++i) {
        if (!args[i].empty() && args[i][0] == '-') {
            if (args[i] != "--allow-container-shift" && i + 1 < args.size()) {
                ++i;
            }
            continue;
        }
        positional.push_back(args[i]);
    }
    std::string error;
    if (command == "generate") {
        avsync::GenerateRequest request;
        request.format = opt("--format", "1080p50");
        request.duration_s = std::strtod(opt("--duration", "60").c_str(), nullptr);
        request.offset_ms = std::strtod(opt("--offset", "0").c_str(), nullptr);
        const std::string sweep = opt("--offset-sweep", "");
        if (!sweep.empty()) {
            request.sweep = split_doubles(sweep);
        }
        request.audio_timecode = opt("--audio-timecode", "1200");
        request.event_rate = std::atoi(opt("--event-rate", "2").c_str());
        request.audio_rate = std::atoi(opt("--audio-rate", "48000").c_str());
        request.channels = std::atoi(opt("--channels", "2").c_str());
        request.container = opt("--container", "mov");
        request.video_codec = opt("--video-codec", "prores");
        request.audio_codec = opt("--audio-codec", "pcm24");
        request.title = opt("--title", "");
        request.source = opt("--source", "");
        request.subtitle = opt("--subtitle", "");
        request.source_image = opt("--source-image", "");
        request.notes = opt("--notes", "");
        request.test_id = opt("--test-id", "TEST-2026-0001");
        request.out_path = opt("--out", positional.empty() ? "" : positional.back());
        request.allow_container_shift = flag("--allow-container-shift");
        if (request.out_path.empty()) {
            std::cerr << "generate needs --out\n";
            return 78;
        }
        const int code = avsync::run_generate(request, error);
        if (code != 0) {
            std::cerr << error << "\n";
        }
        return code;
    }
    if (command == "analyze") {
        if (positional.empty()) {
            std::cerr << "analyze needs a file\n";
            return 78;
        }
        avsync::AnalyzeRequest request;
        request.path = positional[0];
        request.manifest = opt("--manifest", "");
        request.json_out = opt("--json", "");
        if (opt("--from", "").empty()) {
            request.from_s = -1;
        } else {
            request.from_s = std::strtod(opt("--from", "0").c_str(), nullptr);
        }
        if (opt("--to", "").empty()) {
            request.to_s = -1;
        } else {
            request.to_s = std::strtod(opt("--to", "0").c_str(), nullptr);
        }
        request.channel = std::atoi(opt("--channel", "0").c_str());
        request.fail_on = opt("--fail-on", "");
        request.locate = flag("--locate");
        const int code = avsync::run_analyze(request, error);
        if (code != 0) {
            std::cerr << error << "\n";
        }
        return code;
    }
    if (command == "validate") {
        if (positional.empty()) {
            std::cerr << "validate needs a json file\n";
            return 78;
        }
        const int code = avsync::run_validate(positional[0], error);
        if (code != 0) {
            std::cerr << error << "\n";
        }
        return code;
    }
    if (command == "merge") {
        if (positional.size() < 2) {
            std::cerr << "merge needs two result files\n";
            return 78;
        }
        const int code = avsync::run_merge(positional[0], positional[1], opt("--json", ""), error);
        if (code != 0) {
            std::cerr << error << "\n";
        }
        return code;
    }
    if (command == "paths") {
        std::cout << avsync::stage_paths_text();
        return 0;
    }
    if (command == "serve") {
        try {
            avsync::Config config = avsync::load_config();
            return avsync::run_service(std::move(config));
        } catch (const std::exception& ex) {
            const std::string message = ex.what();
            if (message.find("port") != std::string::npos) {
                std::cerr << message << "\n";
                return 75;
            }
            std::cerr << message << "\n";
            return 78;
        }
    }
    std::cerr << "unknown command " << command << "\n";
    usage();
    return 78;
}
