#include "avsync/avsync.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace avsync {
namespace {

void skip_ws(std::string_view& in)
{
    while (!in.empty() && std::isspace(static_cast<unsigned char>(in.front()))) {
        in.remove_prefix(1);
    }
}

bool consume(std::string_view& in, char ch)
{
    skip_ws(in);
    if (in.empty() || in.front() != ch) {
        return false;
    }
    in.remove_prefix(1);
    return true;
}

std::optional<std::string> parse_string(std::string_view& in)
{
    skip_ws(in);
    if (in.empty() || in.front() != '"') {
        return std::nullopt;
    }
    in.remove_prefix(1);
    std::string out;
    while (!in.empty() && in.front() != '"') {
        if (in.front() == '\\' && in.size() >= 2) {
            out.push_back(in[1]);
            in.remove_prefix(2);
            continue;
        }
        out.push_back(in.front());
        in.remove_prefix(1);
    }
    if (in.empty() || in.front() != '"') {
        return std::nullopt;
    }
    in.remove_prefix(1);
    return out;
}

std::optional<double> parse_number(std::string_view& in)
{
    skip_ws(in);
    if (in.empty()) {
        return std::nullopt;
    }
    const char* begin = in.data();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return std::nullopt;
    }
    in.remove_prefix(static_cast<size_t>(end - begin));
    return value;
}

} // namespace

std::string qr_payload_json(const QrPayload& payload)
{
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(0);
    os << "{\"s\":\"" << payload.schema << "\",\"t\":\"" << payload.test_id << "\",\"m\":\"" << payload.mode << "\",\"k\":" << payload.k
       << ",\"tk\":" << payload.tk << ",\"o\":";
    if (payload.sweep) {
        os << "\"sweep\"";
    } else {
        os << static_cast<long long>(std::llround(payload.offset_ms));
    }
    os << ",\"r\":" << payload.rate << ",\"f\":\"" << payload.format << "\"}";
    return os.str();
}

std::optional<QrPayload> parse_qr_payload(std::string_view json)
{
    QrPayload payload;
    std::string_view in = json;
    if (!consume(in, '{')) {
        return std::nullopt;
    }
    bool any = false;
    while (true) {
        skip_ws(in);
        if (!in.empty() && in.front() == '}') {
            in.remove_prefix(1);
            break;
        }
        if (any && !consume(in, ',')) {
            return std::nullopt;
        }
        any = true;
        const auto key = parse_string(in);
        if (!key || !consume(in, ':')) {
            return std::nullopt;
        }
        skip_ws(in);
        if (!in.empty() && in.front() == '"') {
            const auto text = parse_string(in);
            if (!text) {
                return std::nullopt;
            }
            if (*key == "s") {
                payload.schema = *text;
            } else if (*key == "t") {
                payload.test_id = *text;
            } else if (*key == "m") {
                payload.mode = *text;
            } else if (*key == "o") {
                payload.sweep = (*text == "sweep");
            } else if (*key == "f") {
                payload.format = *text;
            }
        } else {
            const auto number = parse_number(in);
            if (!number) {
                return std::nullopt;
            }
            if (*key == "k") {
                payload.k = static_cast<uint64_t>(*number);
            } else if (*key == "tk") {
                payload.tk = static_cast<int64_t>(*number);
            } else if (*key == "o") {
                payload.offset_ms = *number;
                payload.sweep = false;
            } else if (*key == "r") {
                payload.rate = static_cast<int>(*number);
            }
        }
    }
    if (payload.schema != kQrSchema || payload.test_id.empty() || (payload.mode != "FILE" && payload.mode != "LIVE")) {
        return std::nullopt;
    }
    return payload;
}

} // namespace avsync
