#include "domain.hpp"

#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <sys/stat.h>

namespace avsync {
namespace {

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::trunc);
    if (out) {
        out << text;
    }
}

std::string json_escape(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        if (ch == '\n' || ch == '\r') {
            out.push_back(' ');
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

mxlRational rational_of(Rate rate)
{
    return mxlRational{rate.num, rate.den == 0 ? 1 : rate.den};
}

mxlInstance as_instance(void* instance)
{
    return static_cast<mxlInstance>(instance);
}

std::string read_flow_def(mxlInstance instance, const std::string& flow_id)
{
    size_t size = 0;
    mxlGetFlowDef(instance, flow_id.c_str(), nullptr, &size);
    if (size == 0) {
        size = 16384;
    }
    std::string buffer(size, '\0');
    if (mxlGetFlowDef(instance, flow_id.c_str(), buffer.data(), &size) != MXL_STATUS_OK) {
        return {};
    }
    buffer.resize(std::strlen(buffer.c_str()));
    return buffer;
}

void interleave_slice(const uint8_t* first, size_t first_bytes, const uint8_t* second, size_t second_bytes, size_t stride, size_t planes,
    int count, float* out, int channels)
{
    const int head = std::min(count, static_cast<int>(first_bytes / sizeof(float)));
    const int end = std::min(count, head + static_cast<int>(second_bytes / sizeof(float)));
    for (int channel = 0; channel < channels && channel < static_cast<int>(planes) && first != nullptr; ++channel) {
        const auto* src = reinterpret_cast<const float*>(first + static_cast<size_t>(channel) * stride);
        for (int frame = 0; frame < head; ++frame) {
            out[frame * channels + channel] = src[frame];
        }
        if (second == nullptr) {
            continue;
        }
        const auto* wrapped = reinterpret_cast<const float*>(second + static_cast<size_t>(channel) * stride);
        for (int frame = head; frame < end; ++frame) {
            out[frame * channels + channel] = wrapped[frame - head];
        }
    }
}

FlowDesc parse_flow_json(const std::string& body, const std::string& domain_path, const std::string& domain_id, const std::string& fallback_id)
{
    FlowDesc desc;
    desc.domain_path = domain_path;
    desc.domain_id = domain_id;
    desc.flow_id = fallback_id;
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_object()) {
        return desc;
    }
    desc.flow_id = json.value("id", fallback_id);
    desc.media_type = json.value("media_type", "");
    desc.interlace_mode = json.value("interlace_mode", "progressive");
    desc.interlaced = desc.interlace_mode.rfind("interlaced", 0) == 0;
    desc.bottom_field_first = desc.interlace_mode.find("bff") != std::string::npos;
    desc.width = json.value("frame_width", 0);
    desc.height = json.value("frame_height", 0);
    desc.channels = json.value("channel_count", 0);
    if (json.contains("grain_rate") && json["grain_rate"].is_object()) {
        desc.rate.num = json["grain_rate"].value("numerator", 0);
        desc.rate.den = std::max<int64_t>(1, json["grain_rate"].value("denominator", 1));
    }
    if (json.contains("sample_rate") && json["sample_rate"].is_object()) {
        desc.rate.num = json["sample_rate"].value("numerator", 48000);
        desc.rate.den = std::max<int64_t>(1, json["sample_rate"].value("denominator", 1));
    }
    return desc;
}

} // namespace

bool is_mirror_domain(const std::string& path)
{
    const auto name = std::filesystem::path(path).filename().string();
    return name.rfind("mirror-", 0) == 0;
}

bool path_on_tmpfs(const std::string& path)
{
    std::filesystem::path cursor(path.empty() ? "." : path);
    std::error_code ec;
    while (!cursor.empty() && !std::filesystem::exists(cursor, ec)) {
        const auto parent = cursor.parent_path();
        if (parent == cursor) {
            break;
        }
        cursor = parent;
    }
    if (cursor.empty() || !std::filesystem::exists(cursor, ec)) {
        return false;
    }
    bool tmpfs = false;
    return mxlIsTmpFs(cursor.c_str(), &tmpfs) == MXL_STATUS_OK && tmpfs;
}

bool ensure_output_domain(const std::string& path, const std::string& id, int64_t history_ns, std::string* error, bool* created)
{
    if (path.empty() || id.empty() || is_mirror_domain(path)) {
        if (error != nullptr) {
            *error = "refusing to create a mirror or empty MXL domain";
        }
        return false;
    }
    if (!path_on_tmpfs(path)) {
        if (error != nullptr) {
            *error = "MXL domain " + path + " is not on a tmpfs";
        }
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "cannot create MXL domain " + path;
        }
        return false;
    }
    const auto def = std::filesystem::path(path) / "domain_def.json";
    const bool was_absent = !std::filesystem::exists(def);
    if (!was_absent) {
        const auto existing = nlohmann::json::parse(read_text(def), nullptr, false);
        const std::string have = existing.is_object() ? existing.value("id", "") : "";
        if (have != id) {
            if (error != nullptr) {
                *error = "domain " + path + " already has id " + have
                    + "; set MXL_OUTPUT_DOMAIN_ID or NMOS_SEED to keep that id, or remove the leftover directory";
            }
            return false;
        }
    } else {
        write_text(def, std::string("{\"id\":\"") + id + "\",\"label\":\"mxl-avsync\",\"description\":\"mxl-avsync output domain\",\"tags\":{}}\n");
    }
    const auto options = std::filesystem::path(path) / "options.json";
    if (!std::filesystem::exists(options)) {
        write_text(options, std::string("{\"urn:x-mxl:option:history_duration/v1.0\":") + std::to_string(history_ns) + "}\n");
    }
    if (created != nullptr) {
        *created = was_absent;
    }
    return true;
}

bool remove_own_domain(const std::string& path, const std::string& id)
{
    if (path.empty() || path == "/" || id.empty() || is_mirror_domain(path)) {
        return false;
    }
    const auto def = std::filesystem::path(path) / "domain_def.json";
    if (std::filesystem::exists(def)) {
        const auto existing = nlohmann::json::parse(read_text(def), nullptr, false);
        const std::string have = existing.is_object() ? existing.value("id", "") : "";
        if (have != id) {
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
}

bool flow_file_id(const std::string& domain_path, const std::string& flow_id, FlowFileId* id)
{
    const auto path = std::filesystem::path(domain_path) / (flow_id + ".mxl-flow") / "data";
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0 || id == nullptr) {
        return false;
    }
    id->dev = static_cast<uint64_t>(st.st_dev);
    id->ino = static_cast<uint64_t>(st.st_ino);
    return true;
}

std::vector<FlowDesc> scan_flows(const std::string& root)
{
    std::vector<FlowDesc> found;
    if (root.empty()) {
        return found;
    }
    std::vector<std::filesystem::path> domains;
    std::error_code ec;
    const std::filesystem::path root_path(root);
    if (std::filesystem::is_regular_file(root_path / "domain_def.json", ec)) {
        domains.push_back(root_path);
    }
    if (std::filesystem::is_directory(root_path, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(root_path, ec)) {
            if (entry.is_directory() && std::filesystem::is_regular_file(entry.path() / "domain_def.json", ec)) {
                domains.push_back(entry.path());
            }
        }
    }
    for (const auto& domain : domains) {
        const auto def = nlohmann::json::parse(read_text(domain / "domain_def.json"), nullptr, false);
        const std::string domain_id = def.is_object() ? def.value("id", "") : "";
        for (const auto& entry : std::filesystem::directory_iterator(domain, ec)) {
            const auto name = entry.path().filename().string();
            const std::string suffix = ".mxl-flow";
            if (!entry.is_directory() || name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            const std::string fallback = name.substr(0, name.size() - suffix.size());
            found.push_back(parse_flow_json(read_text(entry.path() / "flow_def.json"), domain.string(), domain_id, fallback));
        }
    }
    return found;
}

std::string video_flow_json(const std::string& id, const std::string& label, const std::string& description, const VideoFormat& format,
    const std::string& group)
{
    const char* mode = format.interlaced ? "interlaced_tff" : "progressive";
    const Rate frame = format.frame;
    std::ostringstream out;
    out << "{\"id\":\"" << json_escape(id) << "\",\"format\":\"urn:x-nmos:format:video\",\"label\":\"" << json_escape(label)
        << "\",\"description\":\"" << json_escape(description)
        << "\",\"tags\":{\"urn:x-nmos:tag:grouphint/v1.0\":[\"" << json_escape(group) << "\"],\"urn:x-mxl:tag:test-signal/v1.0\":[\"mxl-avsync\"]}"
        << ",\"parents\":[],\"media_type\":\"video/v210\",\"grain_rate\":{\"numerator\":" << frame.num << ",\"denominator\":" << frame.den
        << "},\"frame_width\":" << format.width << ",\"frame_height\":" << format.height << ",\"interlace_mode\":\"" << mode
        << "\",\"colorspace\":\"BT709\",\"components\":["
        << "{\"name\":\"Y\",\"width\":" << format.width << ",\"height\":" << format.height << ",\"bit_depth\":10},"
        << "{\"name\":\"Cb\",\"width\":" << format.width / 2 << ",\"height\":" << format.height << ",\"bit_depth\":10},"
        << "{\"name\":\"Cr\",\"width\":" << format.width / 2 << ",\"height\":" << format.height << ",\"bit_depth\":10}]}";
    return out.str();
}

std::string audio_flow_json(const std::string& id, const std::string& label, int channels)
{
    std::ostringstream out;
    out << "{\"id\":\"" << json_escape(id) << "\",\"format\":\"urn:x-nmos:format:audio\",\"label\":\"" << json_escape(label)
        << "\",\"description\":\"TEST SIGNAL — AV sync audio\""
        << ",\"tags\":{\"urn:x-nmos:tag:grouphint/v1.0\":[\"AVSync Test:Audio\"],\"urn:x-mxl:tag:test-signal/v1.0\":[\"mxl-avsync\"]}"
        << ",\"parents\":[],\"media_type\":\"audio/float32\",\"sample_rate\":{\"numerator\":48000,\"denominator\":1},\"channel_count\":" << channels
        << ",\"bit_depth\":32}";
    return out.str();
}

Domain::~Domain()
{
    close();
}

bool Domain::open(const std::string& path, std::string* error)
{
    close();
    path_ = path;
    instance_ = mxlCreateInstance(path.c_str(), nullptr);
    if (instance_ == nullptr) {
        if (error != nullptr) {
            *error = "mxlCreateInstance failed for " + path;
        }
        return false;
    }
    return true;
}

void Domain::close()
{
    release_video_writer();
    release_result_writer();
    release_audio_writer();
    release_video_reader();
    release_audio_reader();
    if (instance_ != nullptr) {
        mxlDestroyInstance(as_instance(instance_));
        instance_ = nullptr;
    }
}

void Domain::garbage_collect()
{
    if (instance_ != nullptr) {
        mxlGarbageCollectFlows(as_instance(instance_));
    }
}

bool Domain::create_video_writer(const std::string& flow_id, const std::string& definition, Rate* pace, int* grain_rows, std::string* error)
{
    release_video_writer();
    mxlFlowWriter writer = nullptr;
    mxlFlowConfigInfo info{};
    bool created = false;
    auto status = mxlCreateFlowWriter(as_instance(instance_), definition.c_str(), "{\"maxCommitBatchSizeHint\":1}", &writer, &info, &created);
    if (status != MXL_STATUS_OK) {
        status = mxlCreateFlowWriter(as_instance(instance_), definition.c_str(), "{\"maxCommitBatchSizeHint\":1}", &writer, &info, &created);
    }
    if (status != MXL_STATUS_OK || writer == nullptr) {
        if (error != nullptr) {
            *error = "video flow writer failed for " + flow_id;
        }
        return false;
    }
    video_writer_ = writer;
    if (pace != nullptr && info.common.grainRate.numerator > 0 && info.common.grainRate.denominator > 0) {
        pace->num = info.common.grainRate.numerator;
        pace->den = info.common.grainRate.denominator;
    }
    if (grain_rows != nullptr && info.discrete.sliceSizes[0] > 0) {
        // sliceSizes[0] is one v210 line. The grain row count comes from the caller when the
        // flow is interlaced (a grain is one field). This value is the line size, not the row count.
        (void)grain_rows;
    }
    (void)flow_id;
    return true;
}

bool Domain::create_result_writer(const std::string& flow_id, const std::string& definition, Rate* pace, std::string* error)
{
    release_result_writer();
    mxlFlowWriter writer = nullptr;
    mxlFlowConfigInfo info{};
    bool created = false;
    auto status = mxlCreateFlowWriter(as_instance(instance_), definition.c_str(), "{\"maxCommitBatchSizeHint\":1}", &writer, &info, &created);
    if (status != MXL_STATUS_OK || writer == nullptr) {
        if (error != nullptr) {
            *error = "result flow writer failed for " + flow_id;
        }
        return false;
    }
    result_writer_ = writer;
    if (pace != nullptr && info.common.grainRate.numerator > 0 && info.common.grainRate.denominator > 0) {
        pace->num = info.common.grainRate.numerator;
        pace->den = info.common.grainRate.denominator;
    }
    return true;
}

bool Domain::write_result_grain(uint64_t index, size_t expected_bytes, const std::function<void(uint8_t*, size_t)>& fill)
{
    auto* writer = static_cast<mxlFlowWriter>(result_writer_);
    if (writer == nullptr) {
        return false;
    }
    mxlGrainInfo info{};
    uint8_t* payload = nullptr;
    if (mxlFlowWriterOpenGrain(writer, index, &info, &payload) != MXL_STATUS_OK || payload == nullptr) {
        return false;
    }
    if (info.grainSize < expected_bytes) {
        info.flags = MXL_GRAIN_FLAG_INVALID;
        mxlFlowWriterCommitGrain(writer, &info);
        return false;
    }
    fill(payload, static_cast<size_t>(info.grainSize));
    info.flags = 0;
    info.validSlices = info.totalSlices;
    return mxlFlowWriterCommitGrain(writer, &info) == MXL_STATUS_OK;
}

void Domain::release_result_writer()
{
    if (result_writer_ != nullptr && instance_ != nullptr) {
        mxlReleaseFlowWriter(as_instance(instance_), static_cast<mxlFlowWriter>(result_writer_));
    }
    result_writer_ = nullptr;
}

bool Domain::create_audio_writer(const std::string& flow_id, const std::string& definition, size_t* max_write, std::string* error)
{
    release_audio_writer();
    mxlFlowWriter writer = nullptr;
    mxlFlowConfigInfo info{};
    bool created = false;
    auto status = mxlCreateFlowWriter(as_instance(instance_), definition.c_str(), "{\"maxCommitBatchSizeHint\":2000}", &writer, &info, &created);
    if (status != MXL_STATUS_OK || writer == nullptr) {
        if (error != nullptr) {
            *error = "audio flow writer failed for " + flow_id;
        }
        return false;
    }
    audio_writer_ = writer;
    size_t limit = 0;
    if (mxlFlowWriterGetMaxWriteLengthSamples(writer, &limit) != MXL_STATUS_OK || limit == 0) {
        limit = info.continuous.bufferLength / 2;
    }
    if (max_write != nullptr) {
        *max_write = limit;
    }
    return true;
}

bool Domain::create_video_reader(const std::string& flow_id, FlowDesc* desc, Rate* pace, int* grain_rows, uint32_t* grain_count, std::string* error)
{
    release_video_reader();
    mxlFlowReader reader = nullptr;
    if (mxlCreateFlowReader(as_instance(instance_), flow_id.c_str(), nullptr, &reader) != MXL_STATUS_OK || reader == nullptr) {
        if (error != nullptr) {
            *error = "video flow reader failed for " + flow_id;
        }
        return false;
    }
    video_reader_ = reader;
    if (desc != nullptr) {
        *desc = parse_flow_json(read_flow_def(as_instance(instance_), flow_id), path_, "", flow_id);
    }
    mxlFlowInfo info{};
    if (mxlFlowReaderGetInfo(reader, &info) == MXL_STATUS_OK) {
        if (pace != nullptr && info.config.common.grainRate.numerator > 0) {
            pace->num = info.config.common.grainRate.numerator;
            pace->den = info.config.common.grainRate.denominator > 0 ? info.config.common.grainRate.denominator : 1;
        }
        if (grain_count != nullptr) {
            *grain_count = info.config.discrete.grainCount;
        }
    }
    if (grain_rows != nullptr && desc != nullptr && desc->height > 0) {
        *grain_rows = desc->interlaced ? desc->height / 2 : desc->height;
    }
    return true;
}

bool Domain::create_audio_reader(const std::string& flow_id, int* channels, size_t* max_read, std::string* error)
{
    release_audio_reader();
    mxlFlowReader reader = nullptr;
    if (mxlCreateFlowReader(as_instance(instance_), flow_id.c_str(), nullptr, &reader) != MXL_STATUS_OK || reader == nullptr) {
        if (error != nullptr) {
            *error = "audio flow reader failed for " + flow_id;
        }
        return false;
    }
    audio_reader_ = reader;
    const auto desc = parse_flow_json(read_flow_def(as_instance(instance_), flow_id), path_, "", flow_id);
    if (channels != nullptr) {
        *channels = desc.channels > 0 ? desc.channels : 1;
    }
    size_t limit = 0;
    if (mxlFlowReaderGetMaxReadLengthSamples(reader, &limit) != MXL_STATUS_OK) {
        limit = 0;
    }
    if (max_read != nullptr) {
        *max_read = limit;
    }
    return true;
}

bool Domain::write_grain(uint64_t index, size_t expected_bytes, const std::function<void(uint8_t*, size_t)>& fill)
{
    auto* writer = static_cast<mxlFlowWriter>(video_writer_);
    if (writer == nullptr) {
        return false;
    }
    mxlGrainInfo info{};
    uint8_t* payload = nullptr;
    if (mxlFlowWriterOpenGrain(writer, index, &info, &payload) != MXL_STATUS_OK || payload == nullptr) {
        return false;
    }
    if (info.grainSize < expected_bytes) {
        info.flags = MXL_GRAIN_FLAG_INVALID;
        mxlFlowWriterCommitGrain(writer, &info);
        return false;
    }
    fill(payload, static_cast<size_t>(info.grainSize));
    info.flags = 0;
    info.validSlices = info.totalSlices;
    return mxlFlowWriterCommitGrain(writer, &info) == MXL_STATUS_OK;
}

bool Domain::write_silence_grain(uint64_t index)
{
    auto* writer = static_cast<mxlFlowWriter>(video_writer_);
    if (writer == nullptr) {
        return false;
    }
    mxlGrainInfo info{};
    uint8_t* payload = nullptr;
    if (mxlFlowWriterOpenGrain(writer, index, &info, &payload) != MXL_STATUS_OK || payload == nullptr) {
        return false;
    }
    info.flags = MXL_GRAIN_FLAG_INVALID;
    return mxlFlowWriterCommitGrain(writer, &info) == MXL_STATUS_OK;
}

bool Domain::write_samples(uint64_t end_index, const float* interleaved, int frames, int channels, size_t max_write)
{
    auto* writer = static_cast<mxlFlowWriter>(audio_writer_);
    if (writer == nullptr || frames <= 0 || channels <= 0 || max_write == 0 || interleaved == nullptr) {
        return false;
    }
    int done = 0;
    while (done < frames) {
        const int count = std::min(frames - done, static_cast<int>(max_write));
        const uint64_t head = end_index - static_cast<uint64_t>(frames - done) + static_cast<uint64_t>(count);
        mxlMutableWrappedMultiBufferSlice slices{};
        if (mxlFlowWriterOpenSamples(writer, head, static_cast<size_t>(count), &slices) != MXL_STATUS_OK) {
            return false;
        }
        for (int channel = 0; channel < channels && channel < static_cast<int>(slices.count); ++channel) {
            auto* base = static_cast<uint8_t*>(slices.base.fragments[0].pointer);
            if (base == nullptr) {
                continue;
            }
            auto* dst = reinterpret_cast<float*>(base + static_cast<size_t>(channel) * slices.stride);
            const int first = static_cast<int>(slices.base.fragments[0].size / sizeof(float));
            const int head_count = std::min(count, first);
            for (int frame = 0; frame < head_count; ++frame) {
                dst[frame] = interleaved[(done + frame) * channels + channel];
            }
            if (head_count < count && slices.base.fragments[1].pointer != nullptr) {
                auto* wrapped = reinterpret_cast<float*>(static_cast<uint8_t*>(slices.base.fragments[1].pointer)
                    + static_cast<size_t>(channel) * slices.stride);
                for (int frame = head_count; frame < count; ++frame) {
                    wrapped[frame - head_count] = interleaved[(done + frame) * channels + channel];
                }
            }
        }
        if (mxlFlowWriterCommitSamples(writer) != MXL_STATUS_OK) {
            return false;
        }
        done += count;
    }
    return true;
}

void Domain::release_video_writer()
{
    if (video_writer_ != nullptr && instance_ != nullptr) {
        mxlReleaseFlowWriter(as_instance(instance_), static_cast<mxlFlowWriter>(video_writer_));
    }
    video_writer_ = nullptr;
}

void Domain::release_audio_writer()
{
    if (audio_writer_ != nullptr && instance_ != nullptr) {
        mxlReleaseFlowWriter(as_instance(instance_), static_cast<mxlFlowWriter>(audio_writer_));
    }
    audio_writer_ = nullptr;
}

void Domain::release_video_reader()
{
    if (video_reader_ != nullptr && instance_ != nullptr) {
        mxlReleaseFlowReader(as_instance(instance_), static_cast<mxlFlowReader>(video_reader_));
    }
    video_reader_ = nullptr;
}

void Domain::release_audio_reader()
{
    if (audio_reader_ != nullptr && instance_ != nullptr) {
        mxlReleaseFlowReader(as_instance(instance_), static_cast<mxlFlowReader>(audio_reader_));
    }
    audio_reader_ = nullptr;
}

bool Domain::copy_grain(uint64_t index, uint64_t timeout_ns, int grain_rows, int width, uint32_t grain_count, std::vector<uint8_t>* payload, bool* flow_invalid, uint64_t* grain_index)
{
    auto* reader = static_cast<mxlFlowReader>(video_reader_);
    if (reader == nullptr || payload == nullptr || grain_rows < 1 || width < 2) {
        return false;
    }
    const size_t expected = v210_line_bytes(width) * static_cast<size_t>(grain_rows);
    mxlGrainInfo info{};
    uint8_t* grain = nullptr;
    const auto status = mxlFlowReaderGetGrain(reader, index, timeout_ns, &info, &grain);
    if (status == MXL_ERR_FLOW_INVALID) {
        if (flow_invalid != nullptr) {
            *flow_invalid = true;
        }
        return false;
    }
    if (status != MXL_STATUS_OK || grain == nullptr || (info.flags & MXL_GRAIN_FLAG_INVALID) != 0 || info.grainSize < expected || info.index != index) {
        return false;
    }
    mxlFlowRuntimeInfo runtime{};
    if (grain_count > 0 && mxlFlowReaderGetRuntimeInfo(reader, &runtime) == MXL_STATUS_OK && runtime.headIndex + 1 >= index + grain_count) {
        return false;
    }
    uint8_t* again = nullptr;
    mxlGrainInfo check{};
    if (mxlFlowReaderGetGrainNonBlocking(reader, index, &check, &again) != MXL_STATUS_OK || again != grain
        || (check.flags & MXL_GRAIN_FLAG_INVALID) != 0 || check.grainSize < expected) {
        return false;
    }
    payload->assign(grain, grain + expected);
    if (grain_index != nullptr) {
        *grain_index = info.index;
    }
    return true;
}

bool Domain::copy_samples(uint64_t end_index, int frames, int channels, uint64_t timeout_ns, size_t max_read, std::vector<float>* interleaved, bool* flow_invalid)
{
    auto* reader = static_cast<mxlFlowReader>(audio_reader_);
    if (reader == nullptr || interleaved == nullptr || frames <= 0 || channels <= 0) {
        return false;
    }
    interleaved->assign(static_cast<size_t>(frames * channels), 0.f);
    const size_t limit = max_read == 0 ? static_cast<size_t>(frames) : max_read;
    int done = 0;
    while (done < frames) {
        const int count = std::min(frames - done, static_cast<int>(limit));
        const uint64_t head = end_index - static_cast<uint64_t>(frames - done) + static_cast<uint64_t>(count);
        mxlWrappedMultiBufferSlice slices{};
        const auto status = mxlFlowReaderGetSamples(reader, head, static_cast<size_t>(count), timeout_ns, &slices);
        if (status == MXL_ERR_FLOW_INVALID) {
            if (flow_invalid != nullptr) {
                *flow_invalid = true;
            }
            return false;
        }
        if (status != MXL_STATUS_OK) {
            return false;
        }
        interleave_slice(static_cast<const uint8_t*>(slices.base.fragments[0].pointer), slices.base.fragments[0].size,
            static_cast<const uint8_t*>(slices.base.fragments[1].pointer), slices.base.fragments[1].size, slices.stride, slices.count, count,
            interleaved->data() + static_cast<size_t>(done * channels), channels);
        done += count;
    }
    return true;
}

uint64_t Domain::video_head()
{
    auto* reader = static_cast<mxlFlowReader>(video_reader_);
    mxlFlowRuntimeInfo info{};
    if (reader != nullptr && mxlFlowReaderGetRuntimeInfo(reader, &info) == MXL_STATUS_OK) {
        return info.headIndex;
    }
    return 0;
}

uint64_t Domain::audio_head()
{
    auto* reader = static_cast<mxlFlowReader>(audio_reader_);
    mxlFlowRuntimeInfo info{};
    if (reader != nullptr && mxlFlowReaderGetRuntimeInfo(reader, &info) == MXL_STATUS_OK) {
        return info.headIndex;
    }
    return 0;
}

uint64_t tai_now()
{
    return mxlGetTime();
}

uint64_t index_now(Rate rate)
{
    const mxlRational edit = rational_of(rate);
    return mxlGetCurrentIndex(&edit);
}

uint64_t index_at(Rate rate, uint64_t tai_ns)
{
    const mxlRational edit = rational_of(rate);
    return mxlTimestampToIndex(&edit, tai_ns);
}

uint64_t time_of_index(Rate rate, uint64_t index)
{
    const mxlRational edit = rational_of(rate);
    return mxlIndexToTimestamp(&edit, index);
}

void sleep_until_tai(uint64_t tai_ns)
{
    mxlSleepUntil(tai_ns);
}

} // namespace avsync
