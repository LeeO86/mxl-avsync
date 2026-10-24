#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace avsync {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
    int fd = -1;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    std::map<std::string, std::string> headers;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;
    void add(const std::string& method, const std::string& path, Handler handler);
    // Binds or throws std::runtime_error. Serves until stop().
    void serve(int port);
    void stop();

private:
    std::vector<std::tuple<std::string, std::string, Handler>> routes_;
    int listen_fd_ = -1;
    bool stop_ = false;
};

std::map<std::string, std::string> query_map(const std::string& query);

} // namespace avsync
