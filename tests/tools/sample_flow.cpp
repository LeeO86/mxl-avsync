#include "domain.hpp"

#include <fstream>
#include <iostream>
#include <string>

// Prints one grain of an MXL video flow: index, byte size, and whether it was valid.
// With a third argument the grain payload (v210) is also written to that file.
int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "usage: avsync-sample-flow <domain-dir> <flow-id> [payload.v210]\n";
        return 78;
    }
    const std::string domain_path = argv[1];
    const std::string flow_id = argv[2];
    avsync::Domain domain;
    std::string error;
    if (!domain.open(domain_path, &error)) {
        std::cerr << error << "\n";
        return 1;
    }
    avsync::FlowDesc desc;
    avsync::Rate pace{};
    int grain_rows = 0;
    uint32_t grain_count = 0;
    if (!domain.create_video_reader(flow_id, &desc, &pace, &grain_rows, &grain_count, &error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const uint64_t head = domain.video_head();
    if (head == 0) {
        std::cout << "index=0 bytes=0 grain_rows=" << grain_rows << " width=" << desc.width << " height=" << desc.height
                  << " interlaced=" << (desc.interlaced ? 1 : 0) << " valid=0\n";
        return 0;
    }
    std::vector<uint8_t> payload;
    bool invalid = false;
    const uint64_t index = head - 1;
    const bool ok = domain.copy_grain(index, 200000000, grain_rows, desc.width, grain_count, &payload, &invalid);
    std::cout << "index=" << index << " bytes=" << payload.size() << " grain_rows=" << grain_rows << " width=" << desc.width
              << " height=" << desc.height << " interlaced=" << (desc.interlaced ? 1 : 0) << " valid=" << (ok ? 1 : 0) << "\n";
    if (ok && argc > 3) {
        std::ofstream out(argv[3], std::ios::binary);
        out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (!out) return 1;
    }
    return ok ? 0 : 1;
}
