// ErMao Engine - Web 程序化纹理（与桌面端 Texture.cpp 中的算法一致）
#include "rhi/Texture.h"
#include "core/Math.h"

namespace erm {
namespace procedural {

void Checkerboard(std::vector<u8>& rgba, u32 size, u32 cell, const Vec3& c0, const Vec3& c1) {
    rgba.resize(static_cast<size_t>(size) * size * 4);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const bool odd = ((x / cell) + (y / cell)) & 1;
            const Vec3 c = odd ? c1 : c0;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            rgba[i + 0] = (u8)(Saturate(c.x) * 255.0f);
            rgba[i + 1] = (u8)(Saturate(c.y) * 255.0f);
            rgba[i + 2] = (u8)(Saturate(c.z) * 255.0f);
            rgba[i + 3] = 255;
        }
    }
}

void Noise(std::vector<u8>& rgba, u32 size, u32 seed, const Vec3& base, float contrast) {
    rgba.resize(static_cast<size_t>(size) * size * 4);
    u32 state = seed * 1664525u + 1013904223u;
    auto rnd = [&]() {
        state = state * 1664525u + 1013904223u;
        return ((state >> 8) & 0xFFFF) / 65535.0f;
    };
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const float n = (rnd() - 0.5f) * contrast;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4;
            rgba[i + 0] = (u8)(Saturate(base.x + n) * 255.0f);
            rgba[i + 1] = (u8)(Saturate(base.y + n) * 255.0f);
            rgba[i + 2] = (u8)(Saturate(base.z + n) * 255.0f);
            rgba[i + 3] = 255;
        }
    }
}

} // namespace procedural
} // namespace erm
