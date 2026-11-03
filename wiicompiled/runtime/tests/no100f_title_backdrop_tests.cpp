#include "hle/no100f_title_backdrop.h"

#include <iostream>
#include <stdexcept>

using namespace No100fTitleBackdrop;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::vector<uint8_t> Model() {
    std::vector<uint8_t> bytes(1692);
    const auto chunk = [&](size_t offset, uint32_t type, uint32_t size) {
        Write32(bytes.data() + offset, type);
        Write32(bytes.data() + offset + 4, size);
    };
    chunk(0, 0x10, 0x684);
    chunk(0xC0, 0x1A, 0x580);
    chunk(0xDC, 0xF, 0x564);
    chunk(0xE8, 1, 0x3F4);
    chunk(0x598, 3, 0xA8);
    chunk(0x5A4, 0x50E, 0x9C);
    std::memcpy(bytes.data() + 0x564, "text_title", 11);
    auto* g = bytes.data() + 0xF4;
    Write32(g, 0x1003F);
    Write32(g + 4, 12);
    Write32(g + 8, 24);
    Write32(g + 12, 1);
    Write32(g + 428, 1);
    Write32(g + 432, 1);
    Write32(bytes.data() + 0x5B0, 1);
    Write32(bytes.data() + 0x5B4, 1);
    Write32(bytes.data() + 0x5B8, 34);
    Write32(bytes.data() + 0x5BC, 34);
    for (unsigned i = 0; i < 24; ++i) {
        const unsigned corner = i < 4 ? (i == 1 ? 2 : i == 2 ? 1 : i) : i % 4;
        const bool right = corner % 2, bottom = corner / 2;
        Write32(g + 28 + i * 4, 0xFF8090A0 + i % 4);
        WriteFloat(g + 124 + i * 8, right ? 0.0f : -0.9999f);
        WriteFloat(g + 128 + i * 8, bottom ? 0.9999f : 0.0f);
        WriteFloat(g + 436 + i * 12, i < 4 ? -0.02f : 0.0f);
        WriteFloat(g + 440 + i * 12, bottom ? -0.5f : 0.5f);
        WriteFloat(g + 444 + i * 12, right ? 1.0f : -1.0f);
        WriteFloat(g + 724 + i * 12, i < 4 ? -1.0f : 1.0f);
    }
    std::vector<uint32_t> strip;
    for (unsigned face = 0; face < 6; ++face) {
        if (face) {
            strip.push_back(strip.back());
            strip.push_back(face * 4);
        }
        for (unsigned corner = 0; corner < 4; ++corner) strip.push_back(face * 4 + corner);
    }
    for (unsigned i = 0; i < strip.size(); ++i) Write32(bytes.data() + 0x5C4 + i * 4, strip[i]);
    return bytes;
}

int main() {
    try {
        const auto original = Model();
        const auto saved = original;
        const auto model = BuildModel(original.data(), original.size());
        Require(!model.empty() && original == saved, "Build without modifying the original game data");
        Require(model.size() == 7580, "Bound the additional scene-heap stream buffer to 7580 bytes");
        const auto* g = model.data() + 0xF4;
        const auto* source = original.data() + 0xF4;
        Require(std::memcmp(g + kColors, source + 28, 24 * 4) == 0, "Retain ALL original prelighting");
        Require(std::memcmp(g + kUvs, source + 124, 24 * 8) == 0, "Retain ALL original texture coordinates");
        Require(std::memcmp(g + kTriangleData, source + 316, 12 * 8) == 0, "Retain ALL original triangles");
        Require(std::memcmp(g + kPositions, source + 436, 24 * 12) == 0, "Retain ALL original positions");
        Require(std::memcmp(g + kNormals, source + 724, 24 * 12) == 0, "Retain ALL original normals");
        const size_t growth = kGeometrySize - 0x3F4;
        const size_t binmesh = 0x5A4 + growth;
        const size_t indices = binmesh + 32;
        Require(std::memcmp(model.data() + indices, original.data() + 0x5C4, 34 * 4) == 0,
                "Retain the complete original triangle strip");
        for (unsigned i = 0; i < kVertices; ++i) {
            if (i >= 24) {
                const float u = ReadFloat(g + kUvs + i * 8);
                Require(u <= -0.879f || u >= -0.121f, "Wings sample forest edges, not the central logo");
                Require(std::fabs(ReadFloat(g + kPositions + i * 12 + 8)) >= 1.0f,
                        "New panels never overlap the original center");
            }
            float distance = 0;
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float d = ReadFloat(g + kPositions + i * 12 + axis * 4) - ReadFloat(g + kMorph + axis * 4);
                distance += d * d;
            }
            Require(std::sqrt(distance) <= ReadFloat(g + kMorph + 12) + 0.00001f, "Sphere encloses every panel");
        }
        for (unsigned quad = 0; quad < kAddedQuads; ++quad) {
            const unsigned base = 24 + quad * 4;
            const float width = ReadFloat(g + kPositions + (base + 1) * 12 + 8) -
                                ReadFloat(g + kPositions + base * 12 + 8);
            const float uvWidth = std::fabs(ReadFloat(g + kUvs + (base + 1) * 8) - ReadFloat(g + kUvs + base * 8));
            Require(std::fabs(width / 2.0f - uvWidth / 0.9999f) < 0.00001f, "Keep original forest texel density");
            if (quad % kWingPanels) {
                const bool right = (quad / kWingPanels) % 2;
                const unsigned a = base + (right ? 0 : 1), b = base - 4 + (right ? 1 : 0);
                Require(std::fabs(ReadFloat(g + kPositions + a * 12 + 8) -
                                  ReadFloat(g + kPositions + b * 12 + 8)) < 0.00001f, "No gaps between panels");
                Require(Read32(g + kUvs + a * 8) == Read32(g + kUvs + b * 8), "Match texture coordinates at joins");
            }
        }
        unsigned triangles = 0;
        for (unsigned i = 0; i < kIndices; ++i)
            Require(Read32(model.data() + indices + i * 4) < kVertices, "Bound every binmesh index");
        for (unsigned i = 2; i < kIndices; ++i) {
            const auto a = Read32(model.data() + indices + (i - 2) * 4);
            const auto b = Read32(model.data() + indices + (i - 1) * 4);
            const auto c = Read32(model.data() + indices + i * 4);
            if (a != b && b != c && a != c) {
                Require(a / 4 == b / 4 && b / 4 == c / 4, "No triangles connect different panels");
                ++triangles;
            }
        }
        Require(triangles == kTriangles, "Keep declared and actual triangle counts consistent");
        Require(Read32(model.data() + 4) + 12 == model.size() - 12, "Update the containing clump size");
        Require(Read32(model.data() + 0xEC) == kGeometrySize, "Update the geometry struct size");
        Require(Read32(model.data() + binmesh + 20) == kIndices &&
                Read32(model.data() + binmesh + 24) == kIndices, "Update both binmesh index counts");
        Require(std::equal(model.end() - (1692 - 0x64C), model.end(), original.begin() + 0x64C),
                "Preserve atomic/frame metadata and trailing extensions");
        Require(BuildModel(model.data(), model.size()).empty(), "Do not reinject an already modified mesh");
        for (size_t size = 0; size < original.size(); ++size)
            Require(BuildModel(original.data(), size).empty(), "Reject every truncated model");
        for (size_t offset : {size_t(0), size_t(0xC0), size_t(0xDC), size_t(0xE8), size_t(0x598),
                              size_t(0x5A4), size_t(0x564), size_t(0x5BC)}) {
            auto incompatible = original;
            incompatible[offset] ^= 1;
            Require(BuildModel(incompatible.data(), incompatible.size()).empty(), "Preserve incompatible mods");
        }
        auto invalid = original;
        Write32(invalid.data() + 0xF4 + 436, 0x7FC00000);
        Require(BuildModel(invalid.data(), invalid.size()).empty(), "Reject nonfinite geometry even with fast-math");
        std::cout << "Title backdrop tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
