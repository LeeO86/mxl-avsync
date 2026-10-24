#include "uuid.hpp"

#include <array>
#include <cstdint>
#include <sstream>
#include <vector>

namespace avsync {
namespace {

uint32_t rotl(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len)
{
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0);
    }
    const uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (msg[chunk + i * 4] << 24) | (msg[chunk + i * 4 + 1] << 16) | (msg[chunk + i * 4 + 2] << 8) | msg[chunk + i * 4 + 3];
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    std::array<uint8_t, 20> out{};
    const uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = (hs[i] >> 24) & 0xFF;
        out[i * 4 + 1] = (hs[i] >> 16) & 0xFF;
        out[i * 4 + 2] = (hs[i] >> 8) & 0xFF;
        out[i * 4 + 3] = hs[i] & 0xFF;
    }
    return out;
}

int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

std::array<uint8_t, 16> parse_uuid(std::string_view text)
{
    std::array<uint8_t, 16> out{};
    int n = 0;
    for (size_t i = 0; i + 1 < text.size() && n < 16; ++i) {
        if (text[i] == '-') {
            continue;
        }
        out[n++] = static_cast<uint8_t>((hex_nibble(text[i]) << 4) | hex_nibble(text[i + 1]));
        ++i;
    }
    return out;
}

std::string format_uuid(const uint8_t* b)
{
    static const char* hex = "0123456789abcdef";
    std::string out(36, ' ');
    int p = 0;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            out[p++] = '-';
        }
        out[p++] = hex[b[i] >> 4];
        out[p++] = hex[b[i] & 0xF];
    }
    return out;
}

} // namespace

std::string b64(const uint8_t* data, size_t len)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t n = (data[i] << 16) | ((i + 1 < len ? data[i + 1] : 0) << 8) | (i + 2 < len ? data[i + 2] : 0);
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        out.push_back(i + 1 < len ? table[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? table[n & 63] : '=');
    }
    return out;
}

std::string websocket_accept(std::string_view client_key)
{
    const std::string material = std::string(client_key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const auto dig = sha1(reinterpret_cast<const uint8_t*>(material.data()), material.size());
    return b64(dig.data(), dig.size());
}

std::string uuid_v5(std::string_view name)
{
    return uuid_v5("6ba7b811-9dad-11d1-80b4-00c04fd430c8", name);
}

std::string uuid_v5(std::string_view namespace_uuid, std::string_view name)
{
    const auto ns = parse_uuid(namespace_uuid);
    std::vector<uint8_t> data(ns.begin(), ns.end());
    data.insert(data.end(), name.begin(), name.end());
    auto dig = sha1(data.data(), data.size());
    dig[6] = static_cast<uint8_t>((dig[6] & 0x0F) | 0x50);
    dig[8] = static_cast<uint8_t>((dig[8] & 0x3F) | 0x80);
    return format_uuid(dig.data());
}

} // namespace avsync
