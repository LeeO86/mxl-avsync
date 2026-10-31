// Per-grain cost of the live generator picture: render_picture + pack_v210, as the MXL writer does it.
// The live path (LivePicture) is timed too and must give the same bytes on every grain.
// usage: avsync-render-bench [format=1080p50] [grains=500]
#include "avsync/avsync.hpp"
#include "picture.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace avsync;

int main(int argc, char** argv)
{
    const std::string token = argc > 1 ? argv[1] : "1080p50";
    const int grains = argc > 2 ? std::atoi(argv[2]) : 500;
    const auto format = parse_video_format(token);
    if (!format) return 78;
    const Rate pace = format->grain_rate();
    const int rows = format->grain_height();
    Yuv422p10 picture;
    picture.resize(format->width, rows);
    std::vector<uint8_t> packed(v210_line_bytes(format->width) * static_cast<size_t>(rows));
    const Period period = plan_period(*format, 2);
    std::vector<double> render_ms, pack_ms, total_ms, live_ms;
    LivePicture live;
    int mismatches = 0;
    uint64_t hash = 1469598103934665603ULL; // FNV-1a over every packed grain: the output must not change

    const uint64_t first = 90000000000ULL;
    for (int n = 0; n < grains; ++n) {
        const uint64_t index = first + static_cast<uint64_t>(n);
        const int frame_in_period = static_cast<int>(index % static_cast<uint64_t>(period.grains));
        const uint64_t seq = index / static_cast<uint64_t>(period.grains);
        PictureInfo info;
        info.title = "TEST SIGNAL";
        info.format_line = format->token;
        info.offset_line = "offset 0 ms";
        info.period_ms = period.period_ms;
        info.media_ms = static_cast<double>(index_to_ns(pace, index)) / 1e6;
        QrPayload payload;
        payload.timecode = payload.qr_flash = true;
        payload.mode = "LIVE";
        payload.test_id = "bench";
        payload.k = seq;
        payload.tk = index_to_ns(pace, seq * static_cast<uint64_t>(period.grains));
        payload.format = format->token;
        info.qr_json = qr_payload_text(payload);
        const auto t0 = std::chrono::steady_clock::now();
        render_picture(picture, info, seq, frame_in_period, period.grains, flash_white(frame_in_period, period.period_ms / period.grains));
        const auto t1 = std::chrono::steady_clock::now();
        pack_v210(picture.y.data(), picture.cb.data(), picture.cr.data(), format->width, rows, format->width, packed.data());
        const auto t2 = std::chrono::steady_clock::now();
        const auto t3 = std::chrono::steady_clock::now();
        const auto& incremental = live.render(format->width, rows, info, seq, frame_in_period, period.grains, flash_white(frame_in_period, period.period_ms / period.grains));
        const auto t4 = std::chrono::steady_clock::now();
        live_ms.push_back(std::chrono::duration<double, std::milli>(t4 - t3).count());
        if (incremental != packed) ++mismatches;
        for (uint8_t byte : packed) hash = (hash ^ byte) * 1099511628211ULL;
        render_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        pack_ms.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
        total_ms.push_back(std::chrono::duration<double, std::milli>(t2 - t0).count());
    }
    auto show = [](const char* name, std::vector<double> v) {
        std::sort(v.begin(), v.end());
        std::printf("%-7s p50 %6.2f  p99 %6.2f  max %6.2f ms\n", name, v[v.size() / 2], v[v.size() * 99 / 100], v.back());
    };
    std::printf("%s, %d grains, output hash %016llx\n", token.c_str(), grains, static_cast<unsigned long long>(hash));
    show("render", render_ms);
    show("pack", pack_ms);
    show("total", total_ms);
    show("live", live_ms);
    std::printf("live picture differs from the full render in %d grains\n", mismatches);
    return mismatches == 0 ? 0 : 1;
}
