#include "ids.hpp"

#include "uuid.hpp"

namespace avsync {
namespace {

std::string named(const std::string& seed, const std::string& tail)
{
    return uuid_v5("mxl-avsync/" + seed + "/" + tail);
}

} // namespace

NmosIds make_nmos_ids(const std::string& seed, const std::string& video_format, int audio_channels, const std::string& result_format)
{
    NmosIds ids;
    ids.seed = seed;
    ids.node = named(seed, "node");
    ids.device = named(seed, "device");
    ids.domain = named(seed, "domain");
    ids.video_source = named(seed, "source/video");
    ids.audio_source = named(seed, "source/audio");
    ids.result_source = named(seed, "source/result");
    ids.video_sender = named(seed, "sender/video");
    ids.audio_sender = named(seed, "sender/audio");
    ids.result_sender = named(seed, "sender/result");
    ids.video_receiver = named(seed, "receiver/video");
    ids.audio_receiver = named(seed, "receiver/audio");
    ids.video_flow = named(seed, "flow/video/" + video_format);
    ids.audio_flow = named(seed, "flow/audio/" + std::to_string(audio_channels));
    ids.result_flow = named(seed, "flow/result/" + result_format);
    return ids;
}

} // namespace avsync
