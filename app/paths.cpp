#include "paths.hpp"

#include <sstream>

namespace avsync {

std::vector<StagePath> stage_paths()
{
    return {
        {"h264-file", "1.1", true, "File-mode H.264 at several bitrates, including 1 Mbit/s. This is the picture the SRT gateway would deliver."},
        {"g711-file", "1.1", true, "PCMU and PCMA in a file, including 8 kHz telephone rate."},
        {"fiducial-tile", "1.1", true, "Marker search in a scaled or cropped picture. Tested in avsync-core and with --locate."},
        {"merge-view", "1.1", true, "Cross-node event merge (CLI and POST /api/v1/merge). Valid only when both references are tai-ptp."},
        {"srt-live", "1.1", false, "Needs mxl-srt-gateway and libmxl on the Linux host. No gateway process is started here."},
        {"fabrics-cross-node", "1.1", false, "Needs the fabrics agent and two PTP-locked nodes. The merge view itself runs on saved results."},
        {"g711-sip", "1.1", false, "Live G.711 waits for a SIP function. File mode is the substitute."},
        {"st2110-loop", "1.0-host", false, "Needs libmxl, mxl-st2110-gateway and a 2110 NIC. Not part of stages 2 or 3, still open from stage 1."},
        {"phone-core", "2.0", true, "PhoneSession in avsync-core: fiducials, rolling shutter, calibration subtraction. Tested without a device."},
        {"phone-device", "2.0", false, "Expo UI and native capture are in mobile-app/. iOS and Android SDKs are not on this VM."},
    };
}

std::string stage_paths_text()
{
    std::ostringstream os;
    os << "stage path status\n";
    for (const auto& path : stage_paths()) {
        os << "  " << path.stage << "  " << path.id << "  " << (path.runnable_here ? "runs here" : "needs the Linux host") << "\n    " << path.detail
           << "\n";
    }
    return os.str();
}

} // namespace avsync
