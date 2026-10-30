#pragma once

#include <string>
#include <limits>

namespace avsync {

// What adjtimex(2) reports about the kernel clock. Read-only; the values are the host's inside a container.
struct KernelClock {
    bool read_ok = false; // adjtimex() succeeded
    bool unsynced = true; // STA_UNSYNC set
    long esterror_us = 0; // estimated error, microseconds
    long tai_offset_s = 0; // TAI - UTC, 0 when never set (37 s in 2026)
    long maxerror_us = 0;
};

struct ClockState {
    std::string sync_state = "unknown"; // locked | unlocked | unknown
    long accuracy_ns = -1; // esterror in ns, -1 when unknown
    std::string reason = "kernel clock unavailable";
};

// locked: disciplined, TAI offset set and esterror within the limit. unlocked: STA_UNSYNC or no TAI offset.
// The kernel cannot tell PTP from NTP; "locked" means the clock is disciplined and TAI is valid.
inline ClockState classify_kernel_clock(const KernelClock& clock, long max_error_us)
{
    ClockState state;
    if (!clock.read_ok) {
        return state;
    }
    // Linux initializes esterror to NTP_PHASE_LIMIT (16 s) and only NTP daemons update it.
    // phc2sys clears STA_UNSYNC and keeps maxerror low (often 0) but leaves esterror at
    // that default, so esterror alone identifies an unreported error. Do not treat the
    // sentinel as a measured 16-second error, or claim a numeric accuracy from it.
    const bool not_reported = clock.esterror_us >= 16000000;
    if (!not_reported && clock.esterror_us >= 0) {
        state.accuracy_ns = clock.esterror_us <= std::numeric_limits<long>::max() / 1000
            ? clock.esterror_us * 1000 : std::numeric_limits<long>::max();
    }
    state.sync_state = "unlocked";
    if (clock.unsynced) state.reason = "STA_UNSYNC is set";
    else if (clock.tai_offset_s <= 0) state.reason = "TAI offset is not set";
    else if (not_reported) {
        state.sync_state = "locked";
        state.reason = "error not reported (PTP discipline)";
    } else if (clock.esterror_us < 0 || clock.esterror_us > max_error_us) {
        state.reason = "estimated error exceeds clock limit";
    } else {
        state.sync_state = "locked";
        state.reason = "kernel clock synchronized";
    }
    return state;
}

} // namespace avsync
