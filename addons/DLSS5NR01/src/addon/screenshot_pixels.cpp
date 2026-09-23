// The conversion of a presented buffer's rows to 8-bit BGRA with full alpha, for the PNG (kept apart so the offline test can use it alone).
#include "addon/screenshot.h"
#include <cstdint>
#include <cstring>

namespace nr::screenshot {

namespace {
float HalfToFloat(uint16_t h) {
    const uint32_t sign = (h & 0x8000u) << 16, exponent = (h >> 10) & 0x1F, mantissa = h & 0x3FF;
    uint32_t bits;
    if (exponent == 0) {
        if (!mantissa) bits = sign;
        else { int e = -1; uint32_t m = mantissa; do { ++e; m <<= 1; } while (!(m & 0x400)); bits = sign | ((112 - e) << 23) | ((m & 0x3FF) << 13); }
    } else if (exponent == 31) bits = sign | 0x7F800000u | (mantissa << 13);
    else bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float f; memcpy(&f, &bits, sizeof f);
    return f;
}
unsigned char Byte(float v) { return static_cast<unsigned char>(v <= 0.0f ? 0 : v >= 1.0f ? 255 : v * 255.0f + 0.5f); }

} // namespace

bool ToBgra8(DXGI_FORMAT format, const void* row, unsigned width, unsigned char* out) {
    const auto* in = static_cast<const unsigned char*>(row);
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        for (unsigned x = 0; x < width; ++x) { memcpy(out + x * 4, in + x * 4, 3); out[x * 4 + 3] = 255; }
        return true;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        for (unsigned x = 0; x < width; ++x) { out[x * 4] = in[x * 4 + 2]; out[x * 4 + 1] = in[x * 4 + 1]; out[x * 4 + 2] = in[x * 4]; out[x * 4 + 3] = 255; }
        return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        for (unsigned x = 0; x < width; ++x) {
            uint32_t v; memcpy(&v, in + x * 4, 4);
            out[x * 4] = static_cast<unsigned char>(((v >> 20) & 1023) >> 2); out[x * 4 + 1] = static_cast<unsigned char>(((v >> 10) & 1023) >> 2);
            out[x * 4 + 2] = static_cast<unsigned char>((v & 1023) >> 2); out[x * 4 + 3] = 255;
        }
        return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS:   // kept to 0..1: HDR highlights are cut, not tone-mapped
        for (unsigned x = 0; x < width; ++x) {
            uint16_t c[4]; memcpy(c, in + x * 8, 8);
            out[x * 4] = Byte(HalfToFloat(c[2])); out[x * 4 + 1] = Byte(HalfToFloat(c[1])); out[x * 4 + 2] = Byte(HalfToFloat(c[0])); out[x * 4 + 3] = 255;
        }
        return true;
    default:
        return false;
    }
}

} // namespace nr::screenshot
