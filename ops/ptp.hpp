#pragma once

#include <string>

namespace avsync {

struct PtpReading {
    std::string sync_state = "unknown";
    std::string accuracy_ns = "unknown";
    bool read = false;
};

// GET the URL and read {"sync_state","accuracy_ns"}. Any failure leaves sync_state unknown.
// Only http:// is accepted. Empty url does not contact anything.
PtpReading read_ptp_status(const std::string& url, int timeout_ms = 300);

} // namespace avsync
