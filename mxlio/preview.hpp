#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace avsync {

void start_preview_worker();
void stop_preview_worker();

// The media thread copies a committed or accepted grain. A worker encodes it, about once a second.
void note_preview_v210(const std::string& slot, const uint8_t* v210, int width, int height);

// False when that slot has not produced a JPEG yet.
bool preview_jpeg(const std::string& slot, std::vector<uint8_t>* jpeg);

} // namespace avsync
