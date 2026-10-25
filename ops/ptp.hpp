#pragma once

#include "avsync/clock.hpp"

#include <string>

namespace avsync {

struct PtpReading {
    std::string sync_state = "unknown";
    std::string accuracy_ns = "unknown";
};

// GET the URL and read {"sync_state","accuracy_ns"}. Any failure leaves sync_state unknown.
// Only http:// is accepted. Empty url does not contact anything.
PtpReading read_ptp_status(const std::string& url, int timeout_ms = 300);

// adjtimex(2) with modes 0: reads the kernel clock, changes nothing, needs no privilege.
KernelClock read_kernel_clock();

} // namespace avsync
