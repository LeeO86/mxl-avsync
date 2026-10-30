#pragma once

#include "avsync/clock.hpp"

#include <string>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace avsync {

struct PtpReading {
    std::string sync_state = "unknown";
    std::string accuracy_ns = "unknown";
    std::string sync_source = "kernel";
    std::string reason = "clock status unavailable";
    bool gm_present = false;
    std::string gm_identity;
    int64_t master_offset_ns = 0;
    std::string port_state = "unknown";
    std::string socket_error;
    // Status URL extras, when the page sends them (ptp4l / phc2sys offsets and mean path delay).
    std::optional<int64_t> ptp4l_offset_ns;
    std::optional<int64_t> phc2sys_offset_ns;
    std::optional<int64_t> path_delay_ns;
};

// Body of a status URL: required sync_state and accuracy_ns; optional grandmaster, port_state,
// ptp4l_offset_ns, phc2sys_offset_ns and path_delay_ns. Anything invalid leaves that field as it was.
PtpReading parse_ptp_status(const std::string& body);

// Plain HTTP/1.0 GET; true and the body on a 200 answer.
bool http_get(const std::string& host, const std::string& port, const std::string& path, std::string* body, int timeout_ms);

// GET the URL and read {"sync_state","accuracy_ns"}. Any failure leaves sync_state unknown.
// Only http:// is accepted. Empty url does not contact anything.
PtpReading read_ptp_status(const std::string& url, int timeout_ms = 300);

// adjtimex(2) with modes 0: reads the kernel clock, changes nothing, needs no privilege.
KernelClock read_kernel_clock();

// Independently implemented, GET-only IEEE 1588 management subset (domain 0).
// Packet offsets/IDs interoperate with linuxptp TIME_STATUS_NP and PORT_DATA_SET;
// no linuxptp implementation is linked or copied. Requests contain zero-filled
// dataset values, as required by the current pmc GET convention (not pmc -z).
namespace ptp {
inline constexpr uint16_t time_status_np = 0xc000;
inline constexpr uint16_t port_data_set = 0x2004;
using Identity = std::array<uint8_t, 10>;
struct Reply {
    uint16_t id;
    std::array<uint8_t, 8> clock;
    std::vector<uint8_t> data;
};
std::vector<uint8_t> management_get(uint16_t id, uint16_t sequence, const Identity& client);
std::optional<Reply> management_reply(std::span<const uint8_t> bytes, uint16_t id,
    uint16_t sequence, const Identity& client);
PtpReading classify_status(std::span<const uint8_t> time_status, uint8_t port_state, long max_error_us);
} // namespace ptp

// The local reply endpoint is a Linux abstract UDS, requiring the daemon and
// client to share a network namespace (host networking for container clients).
PtpReading read_ptp_socket(const std::string& path, long max_error_us, int timeout_ms = 300);

// URL wins even if invalid/unreachable; socket failures fall back to the kernel,
// but a valid unlocked ptp4l response stays authoritative.
PtpReading read_clock_status(const std::string& url, const std::string& socket_path,
    long max_error_us, const KernelClock& kernel, int timeout_ms = 300);

} // namespace avsync
