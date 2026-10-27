#include "avsync/avsync.hpp"

#include <cctype>
#include <charconv>
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
    os << ",\"r\":" << payload.rate << ",\"f\":\"" << payload.format << "\"";
    if (payload.timecode) os << ",\"tc\":\"v3\",\"sid\":" << payload.source_id;
    if (payload.estimated_source) os << ",\"clk\":\"phone-utc\"";
    os << '}';
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
            if (*key == "clk") { if (*text != "phone-utc") return std::nullopt; payload.estimated_source = true; }
            else if (*key == "tc") { payload.timecode = *text == "v3"; }
            else if (*key == "s") {
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
            if (*key == "tk" || *key == "k" || *key == "sid") {
                skip_ws(in);
                int64_t integer = 0;
                const auto result = std::from_chars(in.data(), in.data() + in.size(), integer);
                if (result.ec != std::errc{} || integer < 0) return std::nullopt;
                in.remove_prefix(size_t(result.ptr - in.data()));
                if (*key == "tk") payload.tk = integer;
                else if (*key == "k") payload.k = uint64_t(integer);
                else { if (uint64_t(integer) > UINT32_MAX) return std::nullopt; payload.source_id = uint32_t(integer); }
                continue;
            }
            const auto number = parse_number(in);
            if (!number) {
                return std::nullopt;
            }
            if (*key == "sid") { payload.source_id = static_cast<uint32_t>(*number); }
            else if (*key == "k") {
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
    if (payload.schema != kQrSchema || payload.test_id.empty() || (payload.mode != "FILE" && payload.mode != "LIVE") || (payload.rate != 1 && payload.rate != 2 && payload.rate != 4) || (payload.timecode && payload.rate == 4)) {
        return std::nullopt;
    }
    return payload;
}

bool flash_white(int frame, double grain_ms) {
    return frame >= 0 && frame * grain_ms < 100;
}

bool identify_video(VideoObservation& video, const QrPayload& qr) {
    if (!qr.timecode || (qr.rate != 1 && qr.rate != 2)) return false;
    const auto format = parse_video_format(qr.format);
    if (!format) return false;
    int32_t delta = int32_t((video.seq - (qr.k & 0xFFFFFFu)) & 0xFFFFFFu);
    if (delta >= 0x800000) delta -= 0x1000000;
    if (std::abs(delta) > 60) return false;
    const auto period = plan_period(*format, qr.rate);
    const int64_t period_ns = index_to_ns(format->grain_rate(), period.grains);
    const int64_t send = qr.tk + int64_t(delta) * period_ns;
    if (send < 0) return false;
    video.estimated_source = qr.estimated_source;
    video.timecode = true; video.source_id = qr.source_id;
    video.event_time_ns = quantize_timecode_ns(send);
    if (qr.mode == "LIVE") { video.have_set_time = true; video.set_tai_ns = send; }
    return true;
}

} // namespace avsync
