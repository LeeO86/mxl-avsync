// Synthetic 720p60 phone source filmed into a distinct 1080p50 MXL raster.
// No physical phone, clock discipline or existing NMOS routes are involved.
#include "domain.hpp"
#include "picture.hpp"
#include "avsync/phone_generator.hpp"
#include "avsync/picture_reader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

using namespace avsync;

int main(int argc, char** argv)
{
    if (argc != 7) {
        std::cerr << "usage: avsync-phone-fixture <domain-dir> <rate> <manifest> <domain-id> <video-id> <audio-id>\n";
        return 78;
    }
    const std::string path = argv[1], manifest = argv[3];
    const std::string domain_id = argv[4], video_id = argv[5], audio_id = argv[6];
    const int rate = std::stoi(argv[2]);
    if (rate != 1 && rate != 2) return 78;
    std::string error;
    if (!ensure_output_domain(path, domain_id, 200000000, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    Domain domain;
    if (!domain.open(path, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    domain.garbage_collect();
    VideoFormat format{"1080p50", 1920, 1080, {50, 1}, false};
    Rate pace{50, 1}, audio_rate{48000, 1};
    int rows = 1080;
    size_t max_write = 0;
    if (!domain.create_video_writer(video_id, video_flow_json(video_id, "Phone camera fixture", "synthetic camera", format), &pace, &rows, &error)
        || !domain.create_audio_writer(audio_id, audio_flow_json(audio_id, "Phone camera audio", 2), &max_write, &error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const int64_t epoch = (tai_now() / 1000000000ULL + 2) * 1000000000ULL;
    PhoneGeneratorPlan plan("phone-fixture-" + std::to_string(rate), rate, epoch, 1000);
    struct Packet { uint64_t start; std::vector<float> wave; };
    std::vector<Packet> packets;
    for (int k = 1; k <= 22 * rate; ++k) {
        packets.push_back({index_at(audio_rate, plan.qr(k).tk + 155000000), plan.audio(k)});
    }
    std::ofstream output(manifest);
    output << "{\"epoch_tai_ns\":" << epoch << ",\"rate\":" << rate
           << ",\"video_delay_ms\":120,\"audio_delay_ms\":155,\"lost_from_ns\":" << epoch + 8000000000LL
           << ",\"lost_until_ns\":" << epoch + 11000000000LL << ",\"source_id\":" << plan.qr(1).source_id
           << ",\"video_flow\":\"" << video_id << "\",\"audio_flow\":\"" << audio_id << "\"}";
    output.close();
    if (!output) return 1;

    Yuv422p10 source;
    source.resize(1280, 720);
    std::vector<uint16_t> camera(1920 * 1080, 240), chroma(960 * 1080, kYNeutralChroma);
    std::vector<uint8_t> packed(v210_line_bytes(1920) * 1080);
    std::vector<float> audio;
    uint64_t written = index_now(audio_rate), frames = 0;
    bool checked = false;
    while (tai_now() < uint64_t(epoch + 22000000000LL)) {
        const uint64_t index = index_now(pace), stamp = time_of_index(pace, index);
        const int64_t elapsed = int64_t(stamp) - epoch;
        std::fill(camera.begin(), camera.end(), 240);
        const bool lost = elapsed >= 8000000000LL && elapsed < 11000000000LL;
        const int64_t source_ns = elapsed - 120000000;
        if (source_ns >= 0 && !lost) {
            const uint64_t frame = uint64_t(source_ns) * 60 / 1000000000ULL;
            const uint64_t sequence = frame / plan.period_frames() + 1;
            const int position = frame % plan.period_frames();
            PictureInfo info;
            info.title = "TEST SIGNAL";
            info.source = "Synthetic phone camera";
            info.qr_json = qr_payload_text(plan.qr(sequence));
            info.period_ms = 1000.0 / rate;
            render_picture(source, info, sequence, position, plan.period_frames(), flash_white(position, 1000.0 / 60));
            // Resample source pixels; its 60-Hz QR/flash must survive 50-Hz capture.
            for (int y = 0; y < 540; ++y) {
                for (int x = 0; x < 960; ++x) {
                    camera[size_t(y + 180) * 1920 + x + 240] = source.y[size_t(y * 4 / 3) * 1280 + x * 4 / 3];
                }
            }
            if (!checked) {
                QrFlashTracker reader;
                const auto qr = decode_qr_signal(camera.data(), 1920, 1080, 1920);
                if(qr)reader.observe(*qr,0);
                reader.push(camera.data(),1920,1080,1920,VideoObservation{});
                std::cout << "fixture preflight located=" << reader.pose().found
                          << " photometry=" << reader.valid_frame() << " qr=" << bool(qr) << '\n' << std::flush;
                if (!reader.valid_frame() || !qr || !qr->payload.qr_flash) return 2;
                checked = true;
            }
        }
        pack_v210(camera.data(), chroma.data(), chroma.data(), 1920, 1080, 1920, packed.data());
        if (domain.write_grain(index, packed.size(), [&](uint8_t* destination, size_t size) {
                std::memcpy(destination, packed.data(), std::min(size, packed.size()));
            })) ++frames;
        const uint64_t target = index_at(audio_rate, time_of_index(pace, index + 1) + 20000000);
        if (target > written) {
            const int count = std::min<uint64_t>(target - written, 48000);
            audio.assign(size_t(count) * 2, 0);
            for (const auto& packet : packets) {
                const uint64_t begin = std::max(written, packet.start);
                const uint64_t end = std::min(written + uint64_t(count), packet.start + packet.wave.size());
                for (uint64_t at = begin; at < end; ++at) {
                    audio[(at - written) * 2] = audio[(at - written) * 2 + 1] = packet.wave[at - packet.start];
                }
            }
            if (!domain.write_samples(written + count, audio.data(), count, 2, max_write)) {
                std::cerr << "audio write failed\n";
                return 3;
            }
            written += count;
        }
        sleep_until_tai(time_of_index(pace, index + 1));
    }
    std::cout << "frames=" << frames << '\n';
    return checked && frames > 0 ? 0 : 1;
}
