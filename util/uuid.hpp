#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace avsync {

// RFC 4122 UUID version 5 (SHA-1), URL namespace 6ba7b811-9dad-11d1-80b4-00c04fd430c8.
std::string uuid_v5(std::string_view name);
std::string uuid_v5(std::string_view namespace_uuid, std::string_view name);
std::string websocket_accept(std::string_view client_key);

} // namespace avsync
