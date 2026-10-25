#include "ptp.hpp"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/timex.h>
#include <unistd.h>

#include <cstring>
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
    clock.tai_offset_s = tx.tai;
    return clock;
}

PtpReading read_ptp_status(const std::string& url, int timeout_ms)
{
    PtpReading reading;
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
    const auto state = json.value("sync_state", "unknown");
    if (state == "locked" || state == "unlocked" || state == "unknown") {
        reading.sync_state = state;
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

} // namespace avsync
