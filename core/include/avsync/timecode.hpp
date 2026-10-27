#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace avsync {

struct PlannedEvent;

// Voice-band audio timecode v3. All reception times refer to chirp onset.
// FILE packets carry media time; LIVE packets carry TAI, never device UTC.
struct AudioTimecode {
    uint32_t source_id = 0;
    int64_t video_send_ns = 0;
    int64_t audio_send_ns = 0;
    bool live = false;
    bool onset = true;
    bool compact = true;
    int baud = 1200;
    double time_ms = 0;
    double arrival_ms = 0;
    double correlation = 0;
    double level_dbfs = -120;
    uint32_t legacy_seq = 0;
    std::vector<uint8_t> bytes;
};

uint32_t timecode_source_id(const char *text);
int64_t quantize_timecode_ns(int64_t ns);
std::vector<uint8_t> encode_timecode(uint32_t source_id, int64_t video_ns, int64_t offset_ns,
                                     bool live);
bool parse_timecode(const std::vector<uint8_t> &bytes, AudioTimecode &out);
double timecode_duration_ms(size_t payload_bytes, int baud);
std::vector<float> timecode_waveform(const std::vector<uint8_t> &bytes, int baud,
                                     int sample_rate = 48000, double level_dbfs = -12);
// Blind decoder: no QR, manifest, source ID, event count or event rate required.
std::vector<AudioTimecode> decode_timecodes(const float *audio, size_t count, int sample_rate,
                                            double origin_ms = 0);

// Latches each complete packet when its onset enters the output block. Updates
// cannot rewrite a telegram already in flight. Collisions are skipped explicitly.
class AudioTimecodeWriter {
  public:
    void write(const std::vector<PlannedEvent> &events, uint64_t start, float *dst, size_t count,
               int baud, double level_dbfs = -12);
    uint64_t skipped() const { return skipped_; }

  private:
    struct Wave {
        int64_t start;
        std::vector<float> samples;
    };
    std::map<uint64_t, Wave> waves_;
    int64_t last_end_ = -1;
    uint64_t skipped_ = 0, last_seq_ = 0;
    bool have_seq_ = false;
};

// Bounded streaming window. A packet retains the arrival mapping of its chirp,
// independently of when its FEC/CRC completes. scan() is throttled to 10 Hz.
class AudioTimecodeStream {
  public:
    void push(const float *audio, size_t count, int sample_rate, double start_ms,
              double arrival_start_ms);
    const std::vector<AudioTimecode> &scan(bool force = false);
    void reset();

  private:
    struct Arrival {
        double start, end, arrival;
    };
    std::vector<float> audio_;
    std::vector<Arrival> arrivals_;
    std::vector<AudioTimecode> decoded_;
    int rate_ = 0;
    double origin_ms_ = 0, last_scan_ms_ = -1e30;
};

} // namespace avsync
