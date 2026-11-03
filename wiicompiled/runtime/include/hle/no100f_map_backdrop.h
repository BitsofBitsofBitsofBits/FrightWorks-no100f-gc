#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#error "Map backdrop guards require -fno-finite-math-only"
#endif

namespace No100fMapBackdrop {

struct Vertex {
    float x, y, z;
    uint32_t color;
    float u, v;
    uint32_t reciprocalZ;
};
static_assert(sizeof(Vertex) == 28);

constexpr std::array<std::array<uint32_t, 2>, 4> kAssets{{
    {0x43140FF9u, 0x30E9ABE8u}, // BKG HUB UI / bkg_hub
    {0x153BDFF5u, 0x30E8D99Cu}, // BKG ECG UI / bkg_ecg
    {0xB0E06DF1u, 0x30E92150u}, // BKG FLW UI / bkg_flw
    {0x0B2E9DF2u, 0x07B4C6ADu}, // BKG ISPB UI / bkg_ispb
}};
constexpr std::array<uint16_t, 30> kIndices{
    0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19,
};
constexpr float kForestMargin = 2.0f / 256.0f;
constexpr float kMapInset = 10.0f; // Four source texels in the 640-wide UI.
constexpr uint32_t kForestTexture = 0x30E9ABE8u;
constexpr float kForestStrip = 0.12f;
constexpr float kForestPanelWidth = 640.0f * kForestStrip;

inline bool MatchesAsset(uint32_t ui, uint32_t texture) {
    for (const auto& asset : kAssets)
        if (asset[0] == ui && asset[1] == texture) return true;
    return false;
}

inline bool Finite(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

inline bool MatchesVertices(const std::array<Vertex, 4>& vertices) {
    constexpr float coordinates[4][4]{
        {0, 0, 0, 0}, {0, 480, 0, 1}, {640, 480, 1, 1}, {640, 0, 1, 0},
    };
    for (unsigned i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        if (!Finite(v.x) || !Finite(v.y) || !Finite(v.z) || !Finite(v.u) || !Finite(v.v) ||
            v.x != coordinates[i][0] || v.y != coordinates[i][1] ||
            v.u != coordinates[i][2] || v.v != coordinates[i][3] || v.z != vertices[0].z ||
            v.color != vertices[0].color || v.reciprocalZ != vertices[0].reciprocalZ)
            return false;
    }
    return true;
}

// The Map is a UI picture, not the title's model. Its routes and labels occupy almost all of the texture.
// Feather four source texels inside each edge as well as a short extension outside. Split at the
// original edge so every Map texel inside 0..640 retains x=640*u, including the translucent strip.
// The compound picture/ramp draw is not a fullscreen overlay to be stretched.
inline std::array<Vertex, 20> BuildVertices(const std::array<Vertex, 4>& source, float aspect) {
    std::array<Vertex, 20> vertices{};
    for (unsigned i = 0; i < source.size(); ++i) vertices[i] = source[i];
    const float wingWidth = std::max(0.0f, 240.0f * aspect - 320.0f);
    const float inset = std::min(kMapInset, wingWidth);
    const float feather = std::min(32.0f, wingWidth);
    vertices[0].x = vertices[1].x = inset;
    vertices[0].u = vertices[1].u = inset / 640.0f;
    vertices[2].x = vertices[3].x = 640.0f - inset;
    vertices[2].u = vertices[3].u = (640.0f - inset) / 640.0f;
    vertices[4] = source[0];
    vertices[5] = source[1];
    vertices[6] = vertices[1];
    vertices[7] = vertices[0];
    vertices[8] = vertices[3];
    vertices[9] = vertices[2];
    vertices[10] = source[2];
    vertices[11] = source[3];
    for (unsigned i : {4u, 5u, 10u, 11u}) {
        auto& color = vertices[i].color;
        color = (color & 0xFFFFFF00u) | (((color & 0xFFu) + 1u) / 2u);
    }
    vertices[12] = source[0];
    vertices[13] = source[1];
    vertices[14] = vertices[5];
    vertices[15] = vertices[4];
    vertices[12].x = vertices[13].x = -feather;
    vertices[12].u = vertices[13].u = kForestMargin;
    vertices[16] = vertices[11];
    vertices[17] = vertices[10];
    vertices[18] = source[2];
    vertices[19] = source[3];
    vertices[18].x = vertices[19].x = 640.0f + feather;
    vertices[18].u = vertices[19].u = 1.0f - kForestMargin;
    for (unsigned i : {12u, 13u, 18u, 19u}) vertices[i].color &= 0xFFFFFF00u;
    return vertices;
}

inline float ForestU(float distance) {
    const float period = 2.0f * kForestPanelWidth;
    float position = std::fmod(distance, period);
    if (position < 0) position += period;
    return std::clamp(std::min(position, period - position) / 640.0f, 0.0f, kForestStrip);
}

// bkg_hub's left 12% is clear forest for the full height, like the title's forest strip. Reuse that
// already-loaded picture for every Map view. Alternate mirrored tiles at its original texel density;
// clip the final tile's UV span as well as its width, rather than stretching it.
inline std::array<Vertex, 4> BuildForestPanel(const std::array<Vertex, 4>& source, bool right,
                                            float offset, float end) {
    const unsigned top = right ? 3 : 0, bottom = right ? 2 : 1;
    std::array<Vertex, 4> vertices{source[top], source[bottom], source[bottom], source[top]};
    const float innerX = right ? 640.0f + offset : -offset;
    const float outerX = right ? 640.0f + end : -end;
    const float innerU = ForestU(offset);
    const float outerU = ForestU(end);
    vertices[0].x = vertices[1].x = right ? innerX : outerX;
    vertices[2].x = vertices[3].x = right ? outerX : innerX;
    vertices[0].u = vertices[1].u = right ? innerU : outerU;
    vertices[2].u = vertices[3].u = right ? outerU : innerU;
    return vertices;
}

} // namespace No100fMapBackdrop
