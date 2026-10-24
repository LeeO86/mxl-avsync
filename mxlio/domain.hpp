#pragma once

#include "avsync/avsync.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace avsync {

struct FlowFileId {
    uint64_t dev = 0;
    uint64_t ino = 0;
    bool operator==(const FlowFileId&) const = default;
};

struct FlowDesc {
    std::string domain_path;
    std::string domain_id;
    std::string flow_id;
    std::string media_type;
    std::string interlace_mode;
    int width = 0;
    int height = 0;
    Rate rate{};
    int channels = 0;
    bool interlaced = false;
    bool bottom_field_first = false;
};

// A directory name starting with "mirror-" is a mirror domain. This process does not write one.
bool is_mirror_domain(const std::string& path);

// True when path, or the nearest existing parent, is a tmpfs. MXL domains live on tmpfs.
bool path_on_tmpfs(const std::string& path);

// Creates the directory and writes domain_def.json and options.json only when they are absent.
// An existing domain_def.json with a different id is left untouched.
bool ensure_output_domain(const std::string& path, const std::string& id, int64_t history_ns, std::string* error, bool* created = nullptr);

// Deletes path when its domain_def.json id matches. Refuses an empty path, "/", and a mismatched id.
bool remove_own_domain(const std::string& path, const std::string& id);

bool flow_file_id(const std::string& domain_path, const std::string& flow_id, FlowFileId* id);

// Domains under root (root itself, and one level of children) and the flows in each.
std::vector<FlowDesc> scan_flows(const std::string& root);

std::string video_flow_json(const std::string& id, const std::string& label, const std::string& description, const VideoFormat& format,
    const std::string& group = "AVSync Test:Video");
std::string audio_flow_json(const std::string& id, const std::string& label, int channels);

// One MXL instance for one domain directory.
class Domain {
public:
    Domain() = default;
    ~Domain();
    Domain(const Domain&) = delete;
    Domain& operator=(const Domain&) = delete;

    bool open(const std::string& path, std::string* error);
    void close();
    bool is_open() const { return instance_ != nullptr; }
    const std::string& path() const { return path_; }
    void garbage_collect();

    bool create_video_writer(const std::string& flow_id, const std::string& definition, Rate* pace, int* grain_rows, std::string* error);
    bool create_result_writer(const std::string& flow_id, const std::string& definition, Rate* pace, std::string* error);
    bool create_audio_writer(const std::string& flow_id, const std::string& definition, size_t* max_write, std::string* error);
    bool create_video_reader(const std::string& flow_id, FlowDesc* desc, Rate* pace, int* grain_rows, uint32_t* grain_count, std::string* error);
    bool create_audio_reader(const std::string& flow_id, int* channels, size_t* max_read, std::string* error);

    bool write_grain(uint64_t index, size_t expected_bytes, const std::function<void(uint8_t*, size_t)>& fill);
    bool write_result_grain(uint64_t index, size_t expected_bytes, const std::function<void(uint8_t*, size_t)>& fill);
    bool write_silence_grain(uint64_t index);
    bool write_samples(uint64_t end_index, const float* interleaved, int frames, int channels, size_t max_write);

    void release_video_writer();
    void release_result_writer();
    void release_audio_writer();
    void release_video_reader();
    void release_audio_reader();

    // Copies a valid grain. False when the grain is invalid, short, overwritten, or the flow is gone.
    bool copy_grain(uint64_t index, uint64_t timeout_ns, int grain_rows, int width, uint32_t grain_count, std::vector<uint8_t>* payload, bool* flow_invalid);
    bool copy_samples(uint64_t end_index, int frames, int channels, uint64_t timeout_ns, size_t max_read, std::vector<float>* interleaved, bool* flow_invalid);
    uint64_t video_head();
    uint64_t audio_head();

private:
    std::string path_;
    void* instance_ = nullptr;
    void* video_writer_ = nullptr;
    void* result_writer_ = nullptr;
    void* audio_writer_ = nullptr;
    void* video_reader_ = nullptr;
    void* audio_reader_ = nullptr;
};

uint64_t tai_now();
uint64_t index_now(Rate rate);
uint64_t index_at(Rate rate, uint64_t tai_ns);
uint64_t time_of_index(Rate rate, uint64_t index);
void sleep_until_tai(uint64_t tai_ns);

} // namespace avsync
