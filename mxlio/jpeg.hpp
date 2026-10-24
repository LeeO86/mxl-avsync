#pragma once

#include <cstdint>
#include <vector>

namespace avsync {

// Downscales packed v210 to a JPEG. Empty when the raster cannot be encoded.
std::vector<uint8_t> encode_v210_jpeg(const uint8_t* v210, int width, int height, int out_width, int quality);

} // namespace avsync
