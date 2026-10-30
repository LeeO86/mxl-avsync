#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ptp.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <cctype>
#include <fstream>
#include <functional>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>

using namespace avsync;

namespace {

std::vector<uint8_t> hex(const char* text)
{
    std::string digits;
    for (const char* p = text; *p; ++p) if (!std::isspace(static_cast<unsigned char>(*p))) digits += *p;
    if (digits.size() % 2) throw std::runtime_error("odd hex fixture");
    std::vector<uint8_t> bytes;
    for (size_t offset = 0; offset < digits.size(); offset += 2)
        bytes.push_back(std::stoul(digits.substr(offset, 2), nullptr, 16));
    return bytes;
}

// Fixed wire fixtures: PTPv2 MANAGEMENT RESPONSE, domain 0, sequence 0x1234,
// target port identity 0200000000000001-0001. TIME_STATUS_NP's extension data
// is 50 octets: signed offset -250 ns, gmPresent int32 at 38, identity at 42.
// These are independent bytes, not produced by the encoder under test.
std::vector<uint8_t> time_reply()
{
    return hex(
        "0d 02 00 68 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
        "00 11 22 33 44 55 66 77 00 01 12 34 04 7f "
        "02 00 00 00 00 00 00 01 00 01 00 00 02 00 "
        "00 01 00 34 c0 00 "
        "ff ff ff ff ff ff ff 06 "
        "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
        "00 00 00 00 00 00 00 00 00 00 00 00 "
        "00 00 00 01 00 11 22 33 44 55 66 77");
}

std::vector<uint8_t> port_reply()
{
    return hex(
        "0d 02 00 50 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
        "00 11 22 33 44 55 66 77 00 01 12 34 04 7f "
        "02 00 00 00 00 00 00 01 00 01 00 00 02 00 "
        "00 01 00 1c 20 04 "
        "00 11 22 33 44 55 66 77 00 01 09 00 "
        "00 00 00 00 00 00 00 00 00 00 00 00 00 02");
}

ptp::Identity client{2, 0, 0, 0, 0, 0, 0, 1, 0, 1};

class FakeDaemon {
    int fd = -1;
    std::string directory;
    std::thread worker;
    std::atomic<bool> stop{false};
public:
    std::string path;
    std::atomic<int> requests{0};
    std::atomic<bool> valid_requests{true};
    explicit FakeDaemon(uint8_t state = 9, bool gm = true, bool mismatched_clock = false)
    {
        char name[] = "/tmp/avsync-clock-test-XXXXXX";
        if (!mkdtemp(name)) throw std::runtime_error("mkdtemp failed");
        directory = name;
        path = directory + "/ptp4l";
        fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strcpy(address.sun_path, path.c_str());
        if (fd < 0 || bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throw std::runtime_error("bind failed");
        worker = std::thread([=, this] {
            while (!stop) {
                pollfd pending{fd, POLLIN, 0};
                if (poll(&pending, 1, 10) <= 0) continue;
                sockaddr_un peer{};
                socklen_t size = sizeof(peer);
                uint8_t data[256]{};
                const auto count = recvfrom(fd, data, sizeof(data), 0, reinterpret_cast<sockaddr*>(&peer), &size);
                if (count < 54) { valid_requests = false; continue; }
                const bool time = data[52] == 0xc0 && data[53] == 0;
                if ((count != (time ? 104 : 80)) || data[0] != 0x0d || data[1] != 2
                    || data[4] || data[46] || data[48] || data[49] != 1
                    || (!time && (data[52] != 0x20 || data[53] != 4))
                    || !std::all_of(data + 54, data + count, [](auto byte) { return byte == 0; })) {
                    valid_requests = false;
                    continue;
                }
                ++requests;
                auto reply = time ? time_reply() : port_reply();
                std::copy_n(data + 20, 10, reply.begin() + 34);
                std::copy_n(data + 30, 2, reply.begin() + 30);
                if (time) reply[95] = gm ? 1 : 0;
                else {
                    reply[64] = state;
                    if (mismatched_clock) reply[20] ^= 1;
                }
                // Exercise filtering: a stale/wrong-sequence packet must be ignored.
                reply[31] ^= 0x40;
                sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer), size);
                reply[31] ^= 0x40;
                if (!time && state == 9) {
                    reply[64] = 4; // another port is LISTENING, followed by SLAVE
                    sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer), size);
                    reply[64] = 9;
                }
                sendto(fd, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&peer), size);
            }
        });
    }
    ~FakeDaemon() {
        stop = true;
        if (worker.joinable()) worker.join();
        if (fd >= 0) close(fd);
        unlink(path.c_str());
        rmdir(directory.c_str());
    }
};

KernelClock synchronized_kernel()
{
    KernelClock clock;
    clock.read_ok = true;
    clock.unsynced = false;
    clock.tai_offset_s = 37;
    clock.esterror_us = 20;
    clock.maxerror_us = 1000;
    return clock;
}

class FakeHttp {
    int fd;
    std::thread worker;
    std::atomic<bool> stop{false};
public:
    std::string url;
    explicit FakeHttp(const std::string& body)
    {
        fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(fd, 1))
            throw std::runtime_error("HTTP bind failed");
        socklen_t length = sizeof(address);
        getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
        url = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/status";
        worker = std::thread([=, this] {
            while (!stop) {
                pollfd pending{fd, POLLIN, 0};
                if (poll(&pending, 1, 10) <= 0) continue;
                const int client = accept(fd, nullptr, nullptr);
                if (client < 0) continue;
                char buffer[4096];
                recv(client, buffer, sizeof(buffer), 0);
                const auto response = "HTTP/1.0 200 OK\r\nContent-Length: " + std::to_string(body.size())
                    + "\r\n\r\n" + body;
                send(client, response.data(), response.size(), MSG_NOSIGNAL);
                close(client);
            }
        });
    }
    ~FakeHttp() { stop = true; worker.join(); close(fd); }
};

} // namespace

TEST_CASE("management GET contains a full zero-filled dataset and never a write action")
{
    auto bytes = ptp::management_get(ptp::time_status_np, 0x1234, client);
    REQUIRE(bytes.size() == 104);
    CHECK(bytes[2] == 0);
    CHECK(bytes[3] == 104);
    CHECK(bytes[46] == 0);
    CHECK(bytes[50] == 0);
    CHECK(bytes[51] == 52);
    CHECK(std::equal(client.begin(), client.end(), bytes.begin() + 20));
    CHECK(std::all_of(bytes.begin() + 34, bytes.begin() + 44, [](auto b) { return b == 0xff; }));
    CHECK(std::all_of(bytes.begin() + 54, bytes.end(), [](auto b) { return b == 0; }));
    CHECK(ptp::management_get(ptp::port_data_set, 1, client).size() == 80);
    CHECK(ptp::management_get(0xffff, 1, client).empty());
}

TEST_CASE("fixed TIME_STATUS_NP and PORT_DATA_SET bytes decode and map signed offset")
{
    auto bytes = time_reply();
    REQUIRE(bytes.size() == 104);
    auto time = ptp::management_reply(bytes, ptp::time_status_np, 0x1234, client);
    REQUIRE(time);
    auto port = ptp::management_reply(port_reply(), ptp::port_data_set, 0x1234, client);
    REQUIRE(port);
    CHECK(port->data[10] == 9);
    auto status = ptp::classify_status(time->data, port->data[10], 100);
    CHECK(status.sync_source == "ptp4l");
    CHECK(status.sync_state == "locked");
    CHECK(status.master_offset_ns == -250);
    CHECK(status.accuracy_ns == "250");
    CHECK(status.gm_identity == "0011223344556677");
    CHECK(status.gm_present);
    CHECK(status.port_state == "SLAVE");
    CHECK(ptp::classify_status(time->data, 6, 100).sync_state == "locked");
    CHECK(ptp::classify_status(time->data, 4, 100).sync_state == "unlocked");
    time->data[41] = 0;
    CHECK(ptp::classify_status(time->data, 9, 100).sync_state == "unlocked");
}

TEST_CASE("management replies reject truncation wrong sequence ID target action and TLV")
{
    const auto original = time_reply();
    for (size_t size = 0; size < original.size(); ++size)
        CHECK_FALSE(ptp::management_reply(std::span(original).first(size), ptp::time_status_np, 0x1234, client));
    for (size_t field : std::array<size_t, 10>{0, 1, 2, 4, 30, 34, 46, 48, 50, 52}) {
        auto bad = original;
        bad[field] ^= 1;
        CHECK_FALSE(ptp::management_reply(bad, ptp::time_status_np, 0x1234, client));
    }
    auto bad = original;
    bad[51] = 0xff;
    CHECK_FALSE(ptp::management_reply(bad, ptp::time_status_np, 0x1234, client));
}

TEST_CASE("PTP offset limit is inclusive in nanoseconds and INT64_MIN is safe")
{
    std::vector<uint8_t> data(50, 0);
    data[41] = 1;
    // +100000 ns is exactly the 100 us boundary; +100001 is outside.
    data[5] = 1; data[6] = 0x86; data[7] = 0xa0;
    CHECK(ptp::classify_status(data, 9, 100).sync_state == "locked");
    data[7] = 0xa1;
    CHECK(ptp::classify_status(data, 9, 100).sync_state == "unlocked");
    std::fill_n(data.begin(), 8, 0);
    data[0] = 0x80;
    auto state = ptp::classify_status(data, 9, 100);
    CHECK(state.master_offset_ns == INT64_MIN);
    CHECK(state.accuracy_ns == "9223372036854775808");
    CHECK(state.sync_state == "unlocked");
}

TEST_CASE("fake UDS filters stale messages and chooses a later SLAVE port")
{
    FakeDaemon daemon;
    const auto result = read_clock_status("", daemon.path, 100, synchronized_kernel(), 100);
    CHECK(result.socket_error.empty());
    CHECK(result.sync_source == "ptp4l");
    CHECK(result.sync_state == "locked");
    CHECK(result.master_offset_ns == -250);
    CHECK(result.port_state == "SLAVE");
    CHECK(daemon.valid_requests);
    CHECK(daemon.requests == 2);
}

TEST_CASE("valid unlocked PTP data never falls back to a locked kernel")
{
    FakeDaemon daemon(4);
    const auto result = read_clock_status("", daemon.path, 100, synchronized_kernel(), 20);
    CHECK(result.socket_error.empty());
    CHECK(result.sync_source == "ptp4l");
    CHECK(result.sync_state == "unlocked");
    CHECK(result.port_state == "LISTENING");
}

TEST_CASE("no grandmaster and excessive offset stay authoritative")
{
    FakeDaemon absent(9, false);
    auto result = read_clock_status("", absent.path, 100, synchronized_kernel(), 50);
    CHECK(result.sync_source == "ptp4l");
    CHECK(result.sync_state == "unlocked");
    CHECK_FALSE(result.gm_present);
    FakeDaemon present;
    result = read_clock_status("", present.path, 0, synchronized_kernel(), 50);
    CHECK(result.sync_source == "ptp4l");
    CHECK(result.sync_state == "unlocked");
}

TEST_CASE("socket failures and unrelated clocks fall back to the kernel")
{
    auto result = read_clock_status("", "/nonexistent/avsync-clock-socket", 100, synchronized_kernel(), 20);
    CHECK(result.sync_source == "kernel");
    CHECK(result.sync_state == "locked");
    CHECK(result.accuracy_ns == "20000");
    CHECK_FALSE(result.socket_error.empty());
    CHECK_FALSE(result.gm_present);
    CHECK(result.master_offset_ns == 0);
    FakeDaemon unrelated(9, true, true);
    result = read_clock_status("", unrelated.path, 100, synchronized_kernel(), 20);
    CHECK(result.sync_source == "kernel");
    CHECK_FALSE(result.socket_error.empty());
}

TEST_CASE("explicit URL wins even when invalid and does not contact a usable socket")
{
    FakeDaemon daemon;
    const auto result = read_clock_status("https://invalid.example/ptp", daemon.path, 100, synchronized_kernel(), 20);
    CHECK(result.sync_source == "url");
    CHECK(result.sync_state == "unknown");
    CHECK(result.socket_error.empty());
    CHECK(daemon.requests == 0);
}

TEST_CASE("successful explicit URL and malformed JSON values remain authoritative")
{
    FakeDaemon daemon;
    FakeHttp server(R"({"sync_state":"locked","accuracy_ns":"80"})");
    auto result = read_clock_status(server.url, daemon.path, 0, KernelClock{}, 100);
    CHECK(result.sync_source == "url");
    CHECK(result.sync_state == "locked");
    CHECK(result.accuracy_ns == "80");
    CHECK(daemon.requests == 0);
    FakeHttp malformed(R"({"sync_state":42,"accuracy_ns":null})");
    result = read_clock_status(malformed.url, daemon.path, 100, synchronized_kernel(), 100);
    CHECK(result.sync_source == "url");
    CHECK(result.sync_state == "unknown");
    CHECK(daemon.requests == 0);
}

TEST_CASE("status URL extras fill grandmaster, port state and offsets; the minimal format still works")
{
    auto full = parse_ptp_status(R"({"sync_state":"locked","accuracy_ns":"4048","grandmaster":"ec4670.fffe.00fe77","port_state":"SLAVE","ptp4l_offset_ns":-126,"phc2sys_offset_ns":-96,"path_delay_ns":7670})");
    CHECK(full.sync_source == "url");
    CHECK(full.sync_state == "locked");
    CHECK(full.accuracy_ns == "4048");
    CHECK(full.gm_present);
    CHECK(full.gm_identity == "ec4670fffe00fe77");
    CHECK(full.port_state == "SLAVE");
    REQUIRE(full.ptp4l_offset_ns);
    CHECK(*full.ptp4l_offset_ns == -126);
    CHECK(full.master_offset_ns == -126);
    REQUIRE(full.phc2sys_offset_ns);
    CHECK(*full.phc2sys_offset_ns == -96);
    REQUIRE(full.path_delay_ns);
    CHECK(*full.path_delay_ns == 7670);
    auto minimal = parse_ptp_status(R"({"sync_state":"locked","accuracy_ns":"80"})");
    CHECK(minimal.sync_state == "locked");
    CHECK(minimal.accuracy_ns == "80");
    CHECK(!minimal.gm_present);
    CHECK(minimal.gm_identity.empty());
    CHECK(minimal.port_state == "unknown");
    CHECK(!minimal.ptp4l_offset_ns);
    CHECK(!minimal.path_delay_ns);
    auto odd = parse_ptp_status(R"({"sync_state":"locked","accuracy_ns":"80","grandmaster":"xyz","port_state":7,"path_delay_ns":"7670"})");
    CHECK(odd.sync_state == "locked");
    CHECK(!odd.gm_present);
    CHECK(odd.port_state == "unknown");
    CHECK(!odd.path_delay_ns);
    FakeDaemon daemon;
    FakeHttp server(R"({"sync_state":"locked","accuracy_ns":"4048","grandmaster":"ec4670.fffe.00fe77","port_state":"SLAVE"})");
    auto result = read_clock_status(server.url, daemon.path, 0, KernelClock{}, 100);
    CHECK(result.gm_identity == "ec4670fffe00fe77");
    CHECK(result.port_state == "SLAVE");
}

TEST_CASE("recorded linuxptp 4.2 replies interoperate with the native codec")
{
    std::ifstream stream(std::string(AVSYNC_FIXTURE_DIR) + "/ptp4l-4.2-management.json");
    REQUIRE(stream.good());
    const auto fixture = nlohmann::json::parse(stream);
    auto identity = hex(fixture.at("client_identity").get<std::string>().c_str());
    REQUIRE(identity.size() == 10);
    ptp::Identity recorded_client{};
    std::copy(identity.begin(), identity.end(), recorded_client.begin());
    std::optional<ptp::Reply> time_status;
    uint8_t state = 0;
    REQUIRE(fixture.at("records").size() == 2);
    for (const auto& record : fixture.at("records")) {
        const uint16_t id = record.at("management_id");
        const uint16_t sequence = record.at("sequence");
        const auto request = hex(record.at("request").get<std::string>().c_str());
        CHECK(ptp::management_get(id, sequence, recorded_client) == request);
        const auto bytes = hex(record.at("response").get<std::string>().c_str());
        auto reply = ptp::management_reply(bytes, id, sequence, recorded_client);
        REQUIRE(reply);
        if (id == ptp::time_status_np) time_status = reply;
        else state = reply->data[10];
    }
    REQUIRE(time_status);
    const auto result = ptp::classify_status(time_status->data, state, 100);
    CHECK(result.sync_source == "ptp4l");
    CHECK(result.sync_state == "unlocked");
    CHECK(result.gm_identity == "0200000000000001");
    CHECK_FALSE(result.gm_present);
    CHECK(result.port_state == "LISTENING");
    CHECK(result.master_offset_ns == 0);
}
