#include "ptp.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/timex.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace avsync {
namespace {

bool http_get(const std::string& host, const std::string& port, const std::string& path, std::string* body, int timeout_ms)
{
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        return false;
    }
    int fd = -1;
    for (auto* it = result; it != nullptr; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0) {
        return false;
    }
    const std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
    if (send(fd, request.data(), request.size(), MSG_NOSIGNAL) < 0) {
        close(fd);
        return false;
    }
    std::string response;
    char buffer[1024];
    while (true) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        response.append(buffer, buffer + n);
    }
    close(fd);
    const auto split = response.find("\r\n\r\n");
    if (split == std::string::npos || response.find(" 200 ") == std::string::npos) {
        return false;
    }
    *body = response.substr(split + 4);
    return true;
}

} // namespace

KernelClock read_kernel_clock()
{
    KernelClock clock;
    timex tx{};
    tx.modes = 0;
    if (adjtimex(&tx) < 0) {
        return clock;
    }
    clock.read_ok = true;
    clock.unsynced = (tx.status & STA_UNSYNC) != 0;
    clock.esterror_us = tx.esterror;
    clock.maxerror_us = tx.maxerror;
    clock.tai_offset_s = tx.tai;
    return clock;
}

PtpReading read_ptp_status(const std::string& url, int timeout_ms)
{
    PtpReading reading;
    reading.sync_source = "url";
    reading.reason = "status URL unavailable or invalid";
    if (url.empty() || url.rfind("http://", 0) != 0) {
        return reading;
    }
    const auto rest = url.substr(7);
    const auto slash = rest.find('/');
    const auto hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    const auto path = slash == std::string::npos ? std::string("/") : rest.substr(slash);
    std::string host = hostport;
    std::string port = "80";
    const auto colon = hostport.rfind(':');
    if (colon != std::string::npos && hostport.find(':') == colon) {
        host = hostport.substr(0, colon);
        port = hostport.substr(colon + 1);
    }
    std::string body;
    if (!http_get(host, port, path, &body, timeout_ms)) {
        return reading;
    }
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_object()) {
        return reading;
    }
    if (json.contains("sync_state") && json["sync_state"].is_string()) {
        const auto state = json["sync_state"].get<std::string>();
        if (state == "locked" || state == "unlocked" || state == "unknown") {
            reading.sync_state = state;
            reading.reason = "explicit status URL";
        }
    }
    if (json.contains("accuracy_ns") && !json["accuracy_ns"].is_null()) {
        if (json["accuracy_ns"].is_string()) {
            reading.accuracy_ns = json["accuracy_ns"].get<std::string>();
        } else if (json["accuracy_ns"].is_number()) {
            reading.accuracy_ns = json["accuracy_ns"].dump();
        }
    }
    return reading;
}

namespace {

uint16_t u16(std::span<const uint8_t> bytes, size_t offset)
{
    return (uint16_t(bytes[offset]) << 8) | bytes[offset + 1];
}

uint64_t unsigned_be(std::span<const uint8_t> bytes)
{
    uint64_t value = 0;
    for (const auto byte : bytes) value = (value << 8) | byte;
    return value;
}

void put16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset] = value >> 8;
    bytes[offset + 1] = value & 0xff;
}

std::string port_name(uint8_t state)
{
    constexpr const char* names[] = {"unknown", "INITIALIZING", "FAULTY", "DISABLED",
        "LISTENING", "PRE_MASTER", "MASTER", "PASSIVE", "UNCALIBRATED", "SLAVE"};
    return state < std::size(names) ? names[state] : "unknown";
}

struct Socket {
    int fd = -1;
    ~Socket() { if (fd >= 0) close(fd); }
};

} // namespace

namespace ptp {

std::vector<uint8_t> management_get(uint16_t id, uint16_t sequence, const Identity& client)
{
    const size_t data_size = id == time_status_np ? 50 : id == port_data_set ? 26 : 0;
    if (!data_size) return {};
    std::vector<uint8_t> bytes(54 + data_size, 0);
    bytes[0] = 0x0d; // management, transportSpecific 0
    bytes[1] = 2; // IEEE 1588 v2, domain 0
    put16(bytes, 2, bytes.size());
    std::copy(client.begin(), client.end(), bytes.begin() + 20);
    put16(bytes, 30, sequence);
    bytes[32] = 4; // management controlField
    bytes[33] = 0x7f;
    std::fill(bytes.begin() + 34, bytes.begin() + 44, 0xff); // target all ports
    // Boundary hops 0: local daemon only. GET action 0; no SET/COMMAND code path.
    put16(bytes, 48, 1); // MANAGEMENT TLV
    put16(bytes, 50, data_size + 2);
    put16(bytes, 52, id);
    return bytes;
}

std::optional<Reply> management_reply(std::span<const uint8_t> bytes, uint16_t id,
    uint16_t sequence, const Identity& client)
{
    if (bytes.size() < 54 || (bytes[0] & 0x0f) != 0x0d || (bytes[1] & 0x0f) != 2
        || bytes[4] != 0 || u16(bytes, 30) != sequence || (bytes[46] & 0x0f) != 2
        || !std::equal(client.begin(), client.end(), bytes.begin() + 34)) return {};
    const size_t length = u16(bytes, 2);
    const size_t tlv_length = u16(bytes, 50);
    const size_t data_size = id == time_status_np ? 50 : id == port_data_set ? 26 : 0;
    if (!data_size || length < 54 || length > bytes.size() || u16(bytes, 48) != 1
        || tlv_length < data_size + 2 || (tlv_length & 1) || 52 + tlv_length > length
        || u16(bytes, 52) != id) return {};
    Reply result{id, {}, {bytes.begin() + 54, bytes.begin() + 54 + data_size}};
    std::copy_n(bytes.begin() + 20, 8, result.clock.begin());
    return result;
}

PtpReading classify_status(std::span<const uint8_t> data, uint8_t port_state, long max_error_us)
{
    PtpReading result;
    result.sync_source = "ptp4l";
    if (data.size() < 50) {
        result.socket_error = "truncated TIME_STATUS_NP";
        return result;
    }
    result.master_offset_ns = std::bit_cast<int64_t>(unsigned_be(data.first(8)));
    // Avoid signed abs(INT64_MIN) overflow, including corrupt daemon responses.
    const uint64_t magnitude = result.master_offset_ns < 0
        ? uint64_t(-(result.master_offset_ns + 1)) + 1 : uint64_t(result.master_offset_ns);
    result.accuracy_ns = std::to_string(magnitude);
    result.gm_present = unsigned_be(data.subspan(38, 4)) != 0;
    std::ostringstream identity;
    for (auto byte : data.subspan(42, 8)) identity << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    result.gm_identity = identity.str();
    result.port_state = port_name(port_state);
    result.sync_state = "unlocked";
    if (!result.gm_present) result.reason = "ptp4l reports no grandmaster";
    else if (port_state != 6 && port_state != 9) result.reason = "ptp4l port is not MASTER or SLAVE";
    else if (max_error_us < 0 || magnitude / 1000 > uint64_t(max_error_us)
        || (magnitude / 1000 == uint64_t(max_error_us) && magnitude % 1000 != 0)) {
        result.reason = "PTP master offset exceeds clock limit";
    } else {
        result.sync_state = "locked";
        result.reason = "ptp4l grandmaster present and offset within limit";
    }
    return result;
}

} // namespace ptp

PtpReading read_ptp_socket(const std::string& path, long max_error_us, int timeout_ms)
{
    PtpReading failure;
    failure.sync_source = "ptp4l";
    auto fail = [&](const std::string& message) {
        failure.socket_error = message;
        failure.reason = message;
        return failure;
    };
    sockaddr_un remote{};
    remote.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof(remote.sun_path) || path.find('\0') != std::string::npos)
        return fail("invalid ptp4l socket path");
    std::copy(path.begin(), path.end(), remote.sun_path);
    Socket socket;
    socket.fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (socket.fd < 0) return fail(std::string("create ptp4l client socket: ") + std::strerror(errno));
    // Linux autobind creates a unique abstract address; no writable hostPath,
    // temporary filesystem entry or root privilege is needed for replies.
    sockaddr_un local{};
    local.sun_family = AF_UNIX;
    if (bind(socket.fd, reinterpret_cast<sockaddr*>(&local), sizeof(sa_family_t)) < 0
        || connect(socket.fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0)
        return fail(std::string("connect ptp4l socket: ") + std::strerror(errno));
    ptp::Identity client{};
    const auto nonce = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ uint64_t(getpid());
    for (size_t i = 0; i < 8; ++i) client[i] = nonce >> (8 * i);
    client[0] = (client[0] & 0xfe) | 2; // local unicast identity, not a facility clock
    client[9] = 1;
    std::optional<ptp::Reply> time_status;
    uint8_t selected_state = 0;
    for (uint16_t sequence : {1, 2}) {
        const uint16_t id = sequence == 1 ? ptp::time_status_np : ptp::port_data_set;
        const auto request = ptp::management_get(id, sequence, client);
        if (send(socket.fd, request.data(), request.size(), MSG_NOSIGNAL) != ssize_t(request.size()))
            return fail(std::string("send ptp4l management GET: ") + std::strerror(errno));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1, timeout_ms));
        bool got_reply = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            pollfd pending{socket.fd, POLLIN, 0};
            const int ready = poll(&pending, 1, int(std::clamp<int64_t>(left, 1, INT_MAX)));
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0) break;
            std::array<uint8_t, 2048> buffer{};
            const ssize_t count = recv(socket.fd, buffer.data(), buffer.size(), MSG_TRUNC);
            if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (count < 0) return fail(std::string("receive ptp4l management reply: ") + std::strerror(errno));
            if (size_t(count) > buffer.size()) continue;
            auto response = ptp::management_reply({buffer.data(), size_t(count)}, id, sequence, client);
            if (!response) continue;
            if (sequence == 1) {
                time_status = std::move(response);
                got_reply = true;
                break;
            }
            // A boundary clock can return multiple ports. Accept a locked-capable
            // port only from the same clock as TIME_STATUS_NP. Otherwise wait to
            // avoid letting a LISTENING port hide a later SLAVE/MASTER response.
            if (response->clock != time_status->clock) continue;
            got_reply = true;
            selected_state = response->data[10];
            if (selected_state == 6 || selected_state == 9) break;
        }
        if (!got_reply) return fail("ptp4l management response timed out or was invalid");
    }
    return ptp::classify_status(time_status->data, selected_state, max_error_us);
}

PtpReading read_clock_status(const std::string& url, const std::string& socket_path,
    long max_error_us, const KernelClock& kernel, int timeout_ms)
{
    if (!url.empty()) return read_ptp_status(url, timeout_ms);
    PtpReading result;
    if (!socket_path.empty()) {
        result = read_ptp_socket(socket_path, max_error_us, timeout_ms);
        if (result.socket_error.empty()) return result;
    }
    const auto state = classify_kernel_clock(kernel, max_error_us);
    // Discard failed/partial socket data; retain only the once-logged diagnostic.
    const auto error = result.socket_error;
    result = PtpReading{};
    result.socket_error = error;
    result.sync_state = state.sync_state;
    result.accuracy_ns = std::to_string(state.accuracy_ns);
    result.reason = state.reason + (error.empty() ? "" : "; ptp4l unavailable, using kernel");
    return result;
}

} // namespace avsync
