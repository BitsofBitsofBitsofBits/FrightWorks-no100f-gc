#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace No100fTitleBackdrop {

constexpr uint32_t kModelId = 0xBC1FF96Eu;
constexpr unsigned kWingPanels = 8;
constexpr unsigned kAddedQuads = kWingPanels * 4;
constexpr unsigned kVertices = 24 + kAddedQuads * 4;
constexpr unsigned kTriangles = 12 + kAddedQuads * 2;
constexpr unsigned kIndices = 34 + kAddedQuads * 6;
constexpr size_t kColors = 28;
constexpr size_t kUvs = kColors + kVertices * 4;
constexpr size_t kTriangleData = kUvs + kVertices * 8;
constexpr size_t kMorph = kTriangleData + kTriangles * 8;
constexpr size_t kPositions = kMorph + 24;
constexpr size_t kNormals = kPositions + kVertices * 12;
constexpr size_t kGeometrySize = kNormals + kVertices * 12;

inline uint32_t Read32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void Write32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (i * 8));
}

inline float ReadFloat(const uint8_t* p) {
    const uint32_t bits = Read32(p);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

inline void WriteFloat(uint8_t* p, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    Write32(p, bits);
}

inline bool Matches(const uint8_t* model, size_t size) {
    if (size != 1692) return false;
    const auto chunk = [&](size_t offset, uint32_t type, uint32_t count) {
        return Read32(model + offset) == type && Read32(model + offset + 4) == count;
    };
    if (!chunk(0, 0x10, 0x684) || !chunk(0xC0, 0x1A, 0x580) ||
        !chunk(0xDC, 0xF, 0x564) || !chunk(0xE8, 1, 0x3F4) ||
        !chunk(0x53C, 6, 0x44) || !chunk(0x548, 1, 4) ||
        Read32(model + 0x554) != 0x1106 ||
        !chunk(0x598, 3, 0xA8) || !chunk(0x5A4, 0x50E, 0x9C) ||
        std::memcmp(model + 0x564, "text_title\0", 11) != 0) return false;
    const uint8_t* g = model + 0xF4;
    if (Read32(g) != 0x1003F || Read32(g + 4) != 12 || Read32(g + 8) != 24 ||
        Read32(g + 12) != 1 || Read32(g + 428) != 1 || Read32(g + 432) != 1 ||
        Read32(model + 0x5B0) != 1 || Read32(model + 0x5B4) != 1 ||
        Read32(model + 0x5B8) != 34 || Read32(model + 0x5BC) != 34 ||
        Read32(model + 0x5C0) != 0) return false;
    // Runtime sources use fast-math; inspect exponent bits rather than a foldable isfinite.
    for (size_t offset = 124; offset < 316; offset += 4)
        if ((Read32(g + offset) & 0x7F800000u) == 0x7F800000u) return false;
    for (unsigned i = 0; i < 24; ++i) {
        const float u = ReadFloat(g + 124 + i * 8);
        if (u < -1 || u > 0) return false;
    }
    for (size_t offset = 412; offset < 1012; offset += 4) {
        if (offset == 428 || offset == 432) continue;
        if ((Read32(g + offset) & 0x7F800000u) == 0x7F800000u) return false;
    }
    for (unsigned i = 0; i < 34; ++i)
        if (Read32(model + 0x5C4 + i * 4) >= 24) return false;
    return ReadFloat(g + 444 + 5 * 12) > ReadFloat(g + 444 + 4 * 12);
}

// Keep the entire original title box and add narrow front/back panels outside its edges. The forest and
// logo share one texture: mirror a 12% forest strip, alternating each panel so joins stay continuous.
// Normalize U from [-1, 0] to [0, 1] for the runtime's title-only edge addressing, retaining its image
// mapping without filtering the opposite forest edge into the mirrored joins.
// Panel width and sampled UV width match, retaining the original texel density rather than smearing it.
// Eight panels per edge nearly triple the box width and remain off-screen in the 4:3 title view.
// All model/texture content is read from the player's data; only procedural geometry is shipped here.
inline std::vector<uint8_t> BuildModel(const uint8_t* model, size_t size) {
    if (!Matches(model, size)) return {};
    const uint8_t* source = model + 0xF4;
    std::vector<uint8_t> geometry(kGeometrySize);
    auto* g = geometry.data();
    std::memcpy(g, source, 28);
    Write32(g + 4, kTriangles);
    Write32(g + 8, kVertices);
    std::memcpy(g + kColors, source + 28, 24 * 4);
    std::memcpy(g + kUvs, source + 124, 24 * 8);
    for (unsigned i = 0; i < 24; ++i)
        WriteFloat(g + kUvs + i * 8, ReadFloat(source + 124 + i * 8) + 1);
    std::memcpy(g + kTriangleData, source + 316, 12 * 8);
    std::memcpy(g + kMorph, source + 412, 24);
    std::memcpy(g + kPositions, source + 436, 24 * 12);
    std::memcpy(g + kNormals, source + 724, 24 * 12);
    std::vector<uint32_t> strip;
    for (unsigned i = 0; i < 34; ++i) strip.push_back(Read32(model + 0x5C4 + i * 4));
    constexpr unsigned corners[2][4] = {{4, 5, 6, 7}, {0, 2, 1, 3}};
    for (unsigned quad = 0; quad < kAddedQuads; ++quad) {
        const unsigned side = quad / (kWingPanels * 2);
        const bool rightEdge = (quad / kWingPanels) % 2;
        const unsigned panel = quad % kWingPanels;
        const unsigned base = 24 + quad * 4;
        for (unsigned corner = 0; corner < 4; ++corner) {
            const unsigned row = corner / 2, column = corner % 2, index = base + corner;
            const unsigned left = corners[side][row * 2], right = corners[side][row * 2 + 1];
            const unsigned edge = rightEdge ? right : left;
            const float distance = float(panel + (rightEdge ? column : 1 - column)) * 0.12f;
            const float t = rightEdge ? 1 + distance : -distance;
            std::memcpy(g + kColors + index * 4, source + 28 + edge * 4, 4);
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float a = ReadFloat(source + 436 + left * 12 + axis * 4);
                const float b = ReadFloat(source + 436 + right * 12 + axis * 4);
                WriteFloat(g + kPositions + index * 12 + axis * 4, a + (b - a) * t);
                std::memcpy(g + kNormals + index * 12 + axis * 4, source + 724 + edge * 12 + axis * 4, 4);
            }
            const bool innerTextureEdge = ((panel + (rightEdge ? column : 1 - column)) % 2) == 0;
            const float u0 = ReadFloat(source + 124 + left * 8);
            const float u1 = ReadFloat(source + 124 + right * 8);
            const float sample = innerTextureEdge ? 0 : 0.12f;
            const float u = (rightEdge ? u1 - (u1 - u0) * sample : u0 + (u1 - u0) * sample) + 1;
            WriteFloat(g + kUvs + index * 8, u);
            std::memcpy(g + kUvs + index * 8 + 4, source + 128 + edge * 8, 4);
        }
        const std::array<uint32_t, 4> quadStrip = side == 0
            ? std::array<uint32_t, 4>{base + 3, base + 2, base + 1, base}
            : std::array<uint32_t, 4>{base, base + 2, base + 1, base + 3};
        strip.push_back(strip.back());
        strip.push_back(quadStrip.front());
        strip.insert(strip.end(), quadStrip.begin(), quadStrip.end());
        const std::array<uint16_t, 8> triangles = side == 0
            ? std::array<uint16_t, 8>{uint16_t(base + 1), uint16_t(base), 0, uint16_t(base + 2),
                                     uint16_t(base + 3), uint16_t(base + 1), 0, uint16_t(base + 2)}
            : std::array<uint16_t, 8>{uint16_t(base + 2), uint16_t(base), 0, uint16_t(base + 1),
                                     uint16_t(base + 3), uint16_t(base + 2), 0, uint16_t(base + 1)};
        for (unsigned i = 0; i < triangles.size(); ++i) {
            g[kTriangleData + (12 + quad * 2) * 8 + i * 2] = uint8_t(triangles[i]);
            g[kTriangleData + (12 + quad * 2) * 8 + i * 2 + 1] = uint8_t(triangles[i] >> 8);
        }
    }
    float radiusSquared = 0;
    for (unsigned i = 0; i < kVertices; ++i) {
        float distance = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            const float d = ReadFloat(g + kPositions + i * 12 + axis * 4) - ReadFloat(g + kMorph + axis * 4);
            distance += d * d;
        }
        radiusSquared = std::max(radiusSquared, distance);
    }
    WriteFloat(g + kMorph + 12, std::sqrt(radiusSquared));

    std::vector<uint8_t> output(model, model + 0xF4);
    output.insert(output.end(), geometry.begin(), geometry.end());
    output.insert(output.end(), model + 0x4E8, model + 0x5C4);
    for (uint32_t index : strip) {
        const size_t at = output.size();
        output.resize(at + 4);
        Write32(output.data() + at, index);
    }
    output.insert(output.end(), model + 0x64C, model + size);
    const uint32_t geometryGrowth = uint32_t(kGeometrySize - 0x3F4);
    const uint32_t indexGrowth = (kIndices - 34) * 4;
    for (size_t header : {size_t(0), size_t(0xC0), size_t(0xDC)})
        Write32(output.data() + header + 4, Read32(model + header + 4) + geometryGrowth + indexGrowth);
    Write32(output.data() + 0xEC, uint32_t(kGeometrySize));
    const size_t extension = 0x598 + geometryGrowth, binmesh = 0x5A4 + geometryGrowth;
    Write32(output.data() + extension + 4, 0xA8 + indexGrowth);
    Write32(output.data() + binmesh + 4, 0x9C + indexGrowth);
    Write32(output.data() + binmesh + 20, kIndices);
    Write32(output.data() + binmesh + 24, kIndices);
    return output;
}

} // namespace No100fTitleBackdrop
