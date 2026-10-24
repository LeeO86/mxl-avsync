#include "domain.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// TAI-paced v210 writer used to exercise the reader without the measurement picture.
// 1920x1080 progressive, one white grain per index, for the requested number of seconds.
int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "usage: avsync-pattern-writer <domain-dir> <domain-id> <seconds>\n";
        return 78;
    }
    const std::string path = argv[1];
    const std::string id = argv[2];
    const int seconds = std::atoi(argv[3]);
    std::string error;
    if (!avsync::ensure_output_domain(path, id, 200000000, &error)) {
        std::cerr << error << "\n";
        return 1;
    }
    avsync::Domain domain;
    if (!domain.open(path, &error)) {
        std::cerr << error << "\n";
        return 1;
    }
    domain.garbage_collect();
    avsync::VideoFormat format{"1080p50", 1920, 1080, {50, 1}, false};
    const std::string flow = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    avsync::Rate pace = format.grain_rate();
    int rows = format.grain_height();
    if (!domain.create_video_writer(flow, avsync::video_flow_json(flow, "Pattern", "pattern", format), &pace, &rows, &error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const size_t bytes = avsync::v210_line_bytes(format.width) * static_cast<size_t>(rows);
    std::vector<uint16_t> y(static_cast<size_t>(format.width) * static_cast<size_t>(rows), avsync::kYWhite);
    std::vector<uint16_t> c(static_cast<size_t>(format.width / 2) * static_cast<size_t>(rows), avsync::kYNeutralChroma);
    std::vector<uint8_t> packed(bytes);
    avsync::pack_v210(y.data(), c.data(), c.data(), format.width, rows, format.width, packed.data());
    const uint64_t start = avsync::tai_now();
    const uint64_t end = start + static_cast<uint64_t>(std::max(1, seconds)) * 1000000000ULL;
    int written = 0;
    while (avsync::tai_now() < end) {
        const uint64_t index = avsync::index_now(pace);
        const uint64_t when = avsync::time_of_index(pace, index);
        if (avsync::tai_now() + 1000000 < when) {
            avsync::sleep_until_tai(when);
        }
        if (domain.write_grain(index, packed.size(), [&](uint8_t* dst, size_t size) {
                std::memcpy(dst, packed.data(), std::min(size, packed.size()));
            })) {
            ++written;
        }
        avsync::sleep_until_tai(avsync::time_of_index(pace, index + 1));
    }
    std::cout << "wrote " << written << " grains flow=" << flow << "\n";
    return written > 0 ? 0 : 1;
}
