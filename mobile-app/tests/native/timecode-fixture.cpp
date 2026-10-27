#include "avsync/avsync.hpp"
#include <fstream>
#include <vector>

int main(int argc, char **argv) {
    using namespace avsync;
    const std::string dir = argv[1];
    const int rate = argc > 2 ? std::stoi(argv[2]) : 2;
    PlanConfig config;
    config.video = *parse_video_format("1080p50");
    config.event_rate = rate;
    config.duration_ms = 7000;
    config.timecode_baud = 1200;
    config.source_id = timecode_source_id("native-timecode");
    auto plan = make_file_plan(config);
    constexpr int64_t epoch = 1700000037000000000LL;
    for (auto &event : plan.events) {
        event.audio_packet =
            encode_timecode(config.source_id,
                            epoch + int64_t(std::llround(event.video_timeline_ms * 1e6)), 0, true);
        event.audio_sample += 35 * 48; // physical audio-path delay, absent from source metadata
    }
    std::ofstream video(dir + "/video.gray", std::ios::binary);
    constexpr int w = 640, h = 360;
    for (int frame = 0; frame < 350; ++frame) {
        uint64_t seq = 0;
        int position = 0;
        bool white = false;
        for (const auto &event : plan.events)
            if (frame >= int(event.video_grain)) {
                seq = event.seq;
                position = frame - int(event.video_grain);
                white = flash_white(position, 20);
            }
        std::vector<uint16_t> pixels(w * h);
        LumaView view{pixels.data(), w, h, w};
        fill_luma(view, kYBackground);
        draw_static_layer(view);
        draw_timing(view, seq, position, plan.period.grains, white);
        for (auto value : pixels)
            video.put(char(value / 4));
    }
    std::vector<float> audio(48000 * 7);
    AudioTimecodeWriter writer;
    writer.write(plan.events, 0, audio.data(), audio.size(), 1200);
    std::ofstream sound(dir + "/audio.f32", std::ios::binary);
    sound.write(reinterpret_cast<const char *>(audio.data()), audio.size() * sizeof(float));
    QrPayload qr;
    qr.mode = "LIVE";
    qr.test_id = "native-timecode";
    qr.k = 1;
    qr.tk = epoch + 1000000000LL;
    qr.rate = rate;
    qr.timecode = true;
    qr.source_id = config.source_id;
    std::ofstream(dir + "/qr.json") << qr_payload_json(qr);
}
