#include "http.hpp"

#include "uuid.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace avsync {
namespace {

std::string lower(std::string text)
{
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::string status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 101: return "Switching Protocols";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

} // namespace

std::map<std::string, std::string> query_map(const std::string& query)
{
    std::map<std::string, std::string> out;
    std::stringstream ss(query);
    std::string part;
    while (std::getline(ss, part, '&')) {
        const auto eq = part.find('=');
        if (eq == std::string::npos) {
            out[part] = "";
        } else {
            out[part.substr(0, eq)] = part.substr(eq + 1);
        }
    }
    return out;
}

void HttpServer::add(const std::string& method, const std::string& path, Handler handler)
{
    routes_.emplace_back(method, path, std::move(handler));
}

void HttpServer::stop()
{
    stop_ = true;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void HttpServer::serve(int port)
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error("socket");
    }
    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("port " + std::to_string(port) + " is not available");
    }
    if (listen(listen_fd_, 16) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("listen");
    }
    while (!stop_) {
        const int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            break;
        }
        std::string data;
        char buf[4096];
        while (data.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            data.append(buf, buf + n);
            if (data.size() > 2 * 1024 * 1024) {
                break;
            }
        }
        HttpRequest request;
        request.fd = fd;
        const auto head_end = data.find("\r\n\r\n");
        std::string head = head_end == std::string::npos ? data : data.substr(0, head_end);
        std::string body = head_end == std::string::npos ? "" : data.substr(head_end + 4);
        std::stringstream hs(head);
        std::string start;
        std::getline(hs, start);
        if (!start.empty() && start.back() == '\r') {
            start.pop_back();
        }
        std::stringstream ss(start);
        std::string target;
        ss >> request.method >> target;
        const auto q = target.find('?');
        request.path = q == std::string::npos ? target : target.substr(0, q);
        request.query = q == std::string::npos ? "" : target.substr(q + 1);
        std::string line;
        while (std::getline(hs, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                request.headers[lower(line.substr(0, colon))] = line.substr(colon + 1 + (line.size() > colon + 1 && line[colon + 1] == ' ' ? 1 : 0));
            }
        }
        int length = 0;
        if (auto it = request.headers.find("content-length"); it != request.headers.end()) {
            length = std::atoi(it->second.c_str());
        }
        if (length > 8 * 1024 * 1024) {
            const char* too_large = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(fd, too_large, std::strlen(too_large), MSG_NOSIGNAL);
            close(fd);
            continue;
        }
        while (static_cast<int>(body.size()) < length) {
            const ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            body.append(buf, buf + n);
        }
        request.body = body.substr(0, static_cast<size_t>(std::max(0, length)));
        const bool upgrade = request.headers.count("upgrade") && lower(request.headers["upgrade"]) == "websocket";
        HttpResponse response{404, "application/json", "{\"error\":\"not found\"}", {}};
        bool matched = false;
        for (const auto& [method, path, handler] : routes_) {
            if (method == request.method && (path == request.path || (!path.empty() && path.back() == '*' && request.path.rfind(path.substr(0, path.size() - 1), 0) == 0))) {
                response = handler(request);
                matched = true;
                break;
            }
        }
        if (upgrade && matched && response.status == 101) {
            const std::string key = request.headers.count("sec-websocket-key") ? request.headers["sec-websocket-key"] : "";
            std::ostringstream out;
            out << "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                << websocket_accept(key) << "\r\n\r\n";
            const std::string bytes = out.str();
            send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
            // One text frame with the current body, then close. Live pushes are the REST document.
            const std::string payload = response.body;
            std::string frame;
            frame.push_back(static_cast<char>(0x81));
            if (payload.size() < 126) {
                frame.push_back(static_cast<char>(payload.size()));
            } else {
                frame.push_back(126);
                frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
                frame.push_back(static_cast<char>(payload.size() & 0xFF));
            }
            frame += payload;
            send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
            close(fd);
            continue;
        }
        std::ostringstream out;
        out << "HTTP/1.1 " << response.status << ' ' << status_text(response.status) << "\r\n";
        out << "Content-Type: " << response.content_type << "\r\n";
        out << "Content-Length: " << response.body.size() << "\r\n";
        out << "Connection: close\r\n";
        for (const auto& [name, value] : response.headers) {
            out << name << ": " << value << "\r\n";
        }
        out << "\r\n" << response.body;
        const std::string bytes = out.str();
        send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        close(fd);
    }
}

} // namespace avsync
