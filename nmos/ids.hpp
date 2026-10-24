#pragma once

#include <string>

namespace avsync {

// UUIDv5 names are mxl-avsync/<seed>/..., the same strings the MXL flow ids use.
struct NmosIds {
    std::string seed;
    std::string node;
    std::string device;
    std::string domain;
    std::string video_source;
    std::string audio_source;
    std::string result_source;
    std::string video_sender;
    std::string audio_sender;
    std::string result_sender;
    std::string video_receiver;
    std::string audio_receiver;
    std::string video_flow;
    std::string audio_flow;
    std::string result_flow;
};

NmosIds make_nmos_ids(const std::string& seed, const std::string& video_format, int audio_channels, const std::string& result_format);

} // namespace avsync
