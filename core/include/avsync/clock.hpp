#pragma once

#include <string>

namespace avsync {

// What adjtimex(2) reports about the kernel clock. Read-only; the values are the host's inside a container.
struct KernelClock {
    bool read_ok = false; // adjtimex() succeeded
    bool unsynced = true; // STA_UNSYNC set
    long esterror_us = 0; // estimated error, microseconds
    long tai_offset_s = 0; // TAI - UTC, 0 when never set (37 s in 2026)
};

struct ClockState {
    std::string sync_state = "unknown"; // locked | unlocked | unknown
    long accuracy_ns = -1; // esterror in ns, -1 when unknown
};

// locked: disciplined, TAI offset set and esterror within the limit. unlocked: STA_UNSYNC or no TAI offset.
// The kernel cannot tell PTP from NTP; "locked" means the clock is disciplined and TAI is valid.
inline ClockState classify_kernel_clock(const KernelClock& clock, long max_error_us)
{
    ClockState state;
    if (!clock.read_ok) {
        return state;
    }
    state.accuracy_ns = clock.esterror_us * 1000;
    state.sync_state = (!clock.unsynced && clock.tai_offset_s > 0 && clock.esterror_us <= max_error_us) ? "locked" : "unlocked";
    return state;
}

} // namespace avsync
