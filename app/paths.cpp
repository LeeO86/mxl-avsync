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
        {"sibling-gateways", "1.1", false, "ST 2110, SRT, SIP and fabrics are sibling repositories. This process does not start them."},
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
