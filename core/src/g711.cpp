#include "avsync/avsync.hpp"

#include <cmath>

namespace avsync {
namespace {

int16_t float_to_pcm(float sample)
{
    const double clipped = std::max(-1.0, std::min(1.0, static_cast<double>(sample)));
    return static_cast<int16_t>(std::lrint(clipped * 32767.0));
}

float pcm_to_float(int value)
{
    return static_cast<float>(std::max(-32768, std::min(32767, value)) / 32768.0);
}

uint8_t linear_to_ulaw(int16_t pcm)
{
    constexpr int BIAS = 0x84;
    constexpr int CLIP = 32635;
    int sign = (pcm >> 8) & 0x80;
    if (sign) {
        pcm = static_cast<int16_t>(-pcm);
    }
    if (pcm > CLIP) {
        pcm = CLIP;
    }
    pcm = static_cast<int16_t>(pcm + BIAS);
    int exponent = 7;
    for (int mask = 0x4000; (pcm & mask) == 0 && exponent > 0; mask >>= 1) {
        --exponent;
    }
    const int mantissa = (pcm >> (exponent + 3)) & 0x0F;
    return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}

int16_t ulaw_to_linear(uint8_t byte)
{
    byte = static_cast<uint8_t>(~byte);
    const int sign = byte & 0x80;
    const int exponent = (byte >> 4) & 0x07;
    const int mantissa = byte & 0x0F;
    int sample = ((mantissa << 3) + 0x84) << exponent;
    sample -= 0x84;
    return static_cast<int16_t>(sign ? -sample : sample);
}

uint8_t linear_to_alaw(int16_t pcm)
{
    int sign = 0;
    if (pcm < 0) {
        pcm = static_cast<int16_t>(-pcm);
        sign = 0x80;
    }
    if (pcm > 32635) {
        pcm = 32635;
    }
    int exponent = 7;
    for (int mask = 0x4000; (pcm & mask) == 0 && exponent > 0; mask >>= 1) {
        --exponent;
    }
    int mantissa = (pcm >> (exponent == 0 ? 4 : (exponent + 3))) & 0x0F;
    return static_cast<uint8_t>((sign | (exponent << 4) | mantissa) ^ 0x55);
}

int16_t alaw_to_linear(uint8_t byte)
{
    byte ^= 0x55;
    int sign = byte & 0x80;
    int exponent = (byte >> 4) & 0x07;
    int mantissa = byte & 0x0F;
    int sample = mantissa << 4;
    if (exponent > 0) {
        sample += 0x100;
        sample <<= (exponent - 1);
    }
    return static_cast<int16_t>(sign ? -sample : sample);
}

} // namespace

std::vector<uint8_t> encode_ulaw(const float* samples, size_t count)
{
    std::vector<uint8_t> out(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = linear_to_ulaw(float_to_pcm(samples[i]));
    }
    return out;
}

std::vector<uint8_t> encode_alaw(const float* samples, size_t count)
{
    std::vector<uint8_t> out(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = linear_to_alaw(float_to_pcm(samples[i]));
    }
    return out;
}

void decode_ulaw(const uint8_t* bytes, size_t count, float* dst)
{
    for (size_t i = 0; i < count; ++i) {
        dst[i] = pcm_to_float(ulaw_to_linear(bytes[i]));
    }
}

void decode_alaw(const uint8_t* bytes, size_t count, float* dst)
{
    for (size_t i = 0; i < count; ++i) {
        dst[i] = pcm_to_float(alaw_to_linear(bytes[i]));
    }
}

std::vector<float> resample_linear(const float* samples, size_t count, int from_rate, int to_rate)
{
    if (count == 0 || from_rate <= 0 || to_rate <= 0) {
        return {};
    }
    const double ratio = static_cast<double>(to_rate) / static_cast<double>(from_rate);
    const size_t out_count = static_cast<size_t>(std::llround(static_cast<double>(count) * ratio));
    std::vector<float> out(out_count);
    for (size_t i = 0; i < out_count; ++i) {
        const double src = static_cast<double>(i) / ratio;
        const size_t i0 = static_cast<size_t>(src);
        const double frac = src - static_cast<double>(i0);
        const float a = samples[std::min(i0, count - 1)];
        const float b = samples[std::min(i0 + 1, count - 1)];
        out[i] = static_cast<float>(a * (1.0 - frac) + b * frac);
    }
    return out;
}

} // namespace avsync
