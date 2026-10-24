#include "jpeg.hpp"

#include <cstdio>

#include <jpeglib.h>

#include <algorithm>
#include <csetjmp>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace avsync {
namespace {

struct JpegError {
    jpeg_error_mgr pub;
    jmp_buf jump;
};

void jpeg_failed(j_common_ptr info)
{
    longjmp(reinterpret_cast<JpegError*>(info->err)->jump, 1);
}

uint16_t v210_luma_at(const uint8_t* v210, int row_bytes, int width, int x, int y)
{
    if (x < 0) {
        x = 0;
    }
    if (x >= width) {
        x = width - 1;
    }
    const auto* words = reinterpret_cast<const uint32_t*>(v210 + static_cast<size_t>(y) * static_cast<size_t>(row_bytes));
    const int group = x / 6;
    const int which = x % 6;
    const uint32_t* w = words + group * 4;
    switch (which) {
    case 0: return static_cast<uint16_t>((w[0] >> 10) & 0x3FF);
    case 1: return static_cast<uint16_t>(w[1] & 0x3FF);
    case 2: return static_cast<uint16_t>((w[1] >> 20) & 0x3FF);
    case 3: return static_cast<uint16_t>((w[2] >> 10) & 0x3FF);
    case 4: return static_cast<uint16_t>(w[3] & 0x3FF);
    default: return static_cast<uint16_t>((w[3] >> 20) & 0x3FF);
    }
}

void v210_chroma_at(const uint8_t* v210, int row_bytes, int width, int x, int y, uint16_t* cb, uint16_t* cr)
{
    const int cx = std::min(x, width - 1) & ~1;
    const auto* words = reinterpret_cast<const uint32_t*>(v210 + static_cast<size_t>(y) * static_cast<size_t>(row_bytes));
    const int group = cx / 6;
    const int which = (cx % 6) / 2;
    const uint32_t* w = words + group * 4;
    if (which == 0) {
        *cb = static_cast<uint16_t>(w[0] & 0x3FF);
        *cr = static_cast<uint16_t>((w[0] >> 20) & 0x3FF);
    } else if (which == 1) {
        *cb = static_cast<uint16_t>((w[1] >> 10) & 0x3FF);
        *cr = static_cast<uint16_t>(w[2] & 0x3FF);
    } else {
        *cb = static_cast<uint16_t>((w[2] >> 20) & 0x3FF);
        *cr = static_cast<uint16_t>((w[3] >> 10) & 0x3FF);
    }
}

void yuv_to_rgb(uint16_t y10, uint16_t cb10, uint16_t cr10, uint8_t* rgb)
{
    const double y = y10 / 4.0;
    const double cb = cb10 / 4.0 - 128.0;
    const double cr = cr10 / 4.0 - 128.0;
    auto clamp = [](double v) { return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); };
    rgb[0] = clamp(y + 1.5748 * cr);
    rgb[1] = clamp(y - 0.1873 * cb - 0.4681 * cr);
    rgb[2] = clamp(y + 1.8556 * cb);
}

} // namespace

std::vector<uint8_t> encode_v210_jpeg(const uint8_t* v210, int width, int height, int out_width, int quality)
{
    if (v210 == nullptr || width < 2 || height < 2 || out_width < 16) {
        return {};
    }
    const int out_height = std::max(16, height * out_width / width);
    const int row_bytes = ((width + 47) / 48) * 128;
    std::vector<uint8_t> rgb(static_cast<size_t>(out_width * out_height * 3));
    for (int y = 0; y < out_height; ++y) {
        const int sy = std::min(height - 1, y * height / out_height);
        for (int x = 0; x < out_width; ++x) {
            const int sx = std::min(width - 1, x * width / out_width);
            uint16_t cb = 512;
            uint16_t cr = 512;
            v210_chroma_at(v210, row_bytes, width, sx, sy, &cb, &cr);
            yuv_to_rgb(v210_luma_at(v210, row_bytes, width, sx, sy), cb, cr, rgb.data() + static_cast<size_t>((y * out_width + x) * 3));
        }
    }
    jpeg_compress_struct info{};
    JpegError error{};
    info.err = jpeg_std_error(&error.pub);
    error.pub.error_exit = jpeg_failed;
    if (setjmp(error.jump) != 0) {
        jpeg_destroy_compress(&info);
        return {};
    }
    jpeg_create_compress(&info);
    unsigned char* out = nullptr;
    unsigned long out_size = 0;
    jpeg_mem_dest(&info, &out, &out_size);
    info.image_width = static_cast<JDIMENSION>(out_width);
    info.image_height = static_cast<JDIMENSION>(out_height);
    info.input_components = 3;
    info.in_color_space = JCS_RGB;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, std::clamp(quality, 1, 100), TRUE);
    jpeg_start_compress(&info, TRUE);
    while (info.next_scanline < info.image_height) {
        JSAMPROW row = rgb.data() + static_cast<size_t>(info.next_scanline) * static_cast<size_t>(out_width * 3);
        jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    std::vector<uint8_t> jpeg(out, out + out_size);
    free(out);
    jpeg_destroy_compress(&info);
    return jpeg;
}

} // namespace avsync
