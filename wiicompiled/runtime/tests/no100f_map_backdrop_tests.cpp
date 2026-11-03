#include "hle/no100f_map_backdrop.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace No100fMapBackdrop;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const std::array<Vertex, 4> source{{
            {0, 0, 0.1f, 0x112233FFu, 0, 0, 0x12345678u},
            {0, 480, 0.1f, 0x112233FFu, 0, 1, 0x12345678u},
            {640, 480, 0.1f, 0x112233FFu, 1, 1, 0x12345678u},
            {640, 0, 0.1f, 0x112233FFu, 1, 0, 0x12345678u},
        }};
        Require(MatchesVertices(source), "Accept the pinned UI picture's geometry");
        for (const auto& asset : kAssets) {
            Require(MatchesAsset(asset[0], asset[1]), "Cover all four Map views");
            Require(!MatchesAsset(asset[0], asset[1] ^ 1), "Reject changed Map textures");
        }
        Require(!MatchesAsset(0x03895FD7u, 0x9DEB6D9Au), "Do not change the pause dimming layer");
        Require(!MatchesAsset(0, 0), "Do not change movies, HUD or other UI");
        Require(kForestTexture == kAssets[0][1], "Reuse the resident world-view forest texture");
        for (float aspect : {4.0f / 3.0f, std::nextafter(4.0f / 3.0f, 2.0f),
                             16.0f / 9.0f, 21.0f / 9.0f, 32.0f / 9.0f}) {
            const auto vertices = BuildVertices(source, aspect);
            const float wingWidth = 240.0f * aspect - 320.0f;
            const float inset = std::min(kMapInset, wingWidth);
            const float feather = std::min(32.0f, wingWidth);
            if (aspect == 4.0f / 3.0f)
                Require(std::memcmp(vertices.data(), source.data(), sizeof source) == 0,
                        "Retain the complete 4:3 quad bit-for-bit");
            Require(inset <= 10 && inset / 640 * 256 <= 4,
                    "Limit inner blending to four original source texels");
            Require(vertices[0].x == inset && vertices[3].x == 640 - inset,
                    "Keep the opaque Map interior between the two narrow strips");
            for (unsigned i = 0; i < vertices.size(); ++i) {
                const auto& v = vertices[i];
                Require(v.x >= -wingWidth && v.x <= 640 + wingWidth, "Keep the fade inside the fill");
                Require(v.v == v.y / 480.0f, "Retain vertical Map mapping");
                Require(v.z == source[0].z && v.reciprocalZ == source[0].reciprocalZ,
                        "Preserve the original depth");
                Require((v.color & 0xFFFFFF00u) == (source[0].color & 0xFFFFFF00u),
                        "Do not change the Map tint");
                if (i < 12)
                    Require(std::fabs(v.u * 640 - v.x) < 0.0001f,
                            "Retain exact Map coordinates, including the translucent inside strip");
                if (i < 4) Require(v.color == source[0].color, "Keep the interior fully opaque");
            }
            for (const auto& pair : {std::array<unsigned, 2>{0, 7}, {1, 6}, {3, 8}, {2, 9},
                                     {4, 15}, {5, 14}, {11, 16}, {10, 17}})
                Require(std::memcmp(&vertices[pair[0]], &vertices[pair[1]], sizeof(Vertex)) == 0,
                        "Join every strip without position, UV or alpha discontinuities");
            Require(vertices[4].x == 0 && vertices[10].x == 640 &&
                    vertices[4].u == 0 && vertices[10].u == 1,
                    "Retain both original image edges, without cropping");
            for (unsigned i : {4u, 5u, 10u, 11u})
                Require((vertices[i].color & 0xFFu) == 128,
                        "The original Map/forest boundary must no longer remain fully opaque");
            for (unsigned i : {12u, 13u, 18u, 19u})
                Require((vertices[i].color & 0xFFu) == 0,
                        "End the outside extension transparently");
            Require(vertices[12].x == -feather && vertices[18].x == 640 + feather,
                    "Cover both sides of the original boundary");
            Require(vertices[12].u == kForestMargin && vertices[18].u == 1 - kForestMargin,
                    "Only extend the outer two source texels outside the original picture");
            float coveredWidth = 0;
            for (unsigned quad = 0; quad < vertices.size(); quad += 4)
                coveredWidth += vertices[quad + 3].x - vertices[quad].x;
            Require(std::fabs(coveredWidth - (640 + 2 * feather)) < 0.0001f,
                    "Cover the complete original Map and extensions exactly once");
            for (unsigned triangle = 0; triangle < kIndices.size(); triangle += 3) {
                const auto& a = vertices[kIndices[triangle]];
                const auto& b = vertices[kIndices[triangle + 1]];
                const auto& c = vertices[kIndices[triangle + 2]];
                Require((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0,
                        "Retain the original triangle winding");
            }
            for (bool right : {false, true}) {
                float previousX = right ? 640.0f : 0.0f, previousU = 0;
                for (unsigned panel = 0; float(panel) * kForestPanelWidth < wingWidth; ++panel) {
                    const float offset = float(panel) * kForestPanelWidth;
                    const float end = std::min(float(panel + 1) * kForestPanelWidth, wingWidth);
                    const auto tile = BuildForestPanel(source, right, offset, end);
                    const auto& inner = tile[right ? 0 : 3];
                    const auto& outer = tile[right ? 3 : 0];
                    Require(inner.x == previousX && inner.u == previousU,
                            "Join mirrored forest tiles without gaps or UV discontinuities");
                    Require(std::fabs(std::fabs(outer.u - inner.u) * 640 - (end - offset)) < 0.0001f,
                            "Retain forest density, including the final clipped tile");
                    for (const auto& v : tile) {
                        Require(v.x <= 0 || v.x >= 640, "Keep ordinary forest tiles outside the Map");
                        Require(v.u >= 0 && v.u <= kForestStrip, "Sample only the clear forest strip");
                        Require(v.v == v.y / 480.0f && v.color == source[0].color,
                                "Retain full-height, opaque forest");
                    }
                    previousX = outer.x;
                    previousU = outer.u;
                }
                Require(previousX == (right ? 640 + wingWidth : -wingWidth),
                        "Cover the exact requested aspect");
                const auto underlay = BuildForestPanel(source, right, -inset, 0);
                const auto& inner = underlay[right ? 0 : 3];
                const auto& edge = underlay[right ? 3 : 0];
                Require(edge.x == (right ? 640 : 0) && edge.u == 0,
                        "Continue the forest under the boundary without a UV discontinuity");
                Require(inner.x == (right ? 640 - inset : inset) &&
                        std::fabs(inner.u * 640 - inset) < 0.0001f,
                        "Reflect the forest at original density under the translucent Map strip");
                for (const auto& v : underlay)
                    Require(v.x <= inset || v.x >= 640 - inset,
                            "Never paint forest behind the opaque interior");
            }
        }
        for (uint32_t opacity : {0u, 1u, 32u, 128u, 255u}) {
            auto tinted = source;
            for (auto& v : tinted) v.color = (v.color & 0xFFFFFF00u) | opacity;
            Require(MatchesVertices(tinted), "Accept a uniform source opacity");
            const auto vertices = BuildVertices(tinted, 32.0f / 9.0f);
            Require((vertices[4].color & 0xFFu) == (opacity + 1u) / 2u,
                    "Scale the edge alpha relative to the original opacity");
            Require(vertices[0].color == tinted[0].color, "Preserve the interior's original opacity");
        }
        auto changed = source;
        changed[0].x = 1;
        Require(!MatchesVertices(changed), "Reject changed layout");
        changed = source;
        changed[0].u = 0.1f;
        Require(!MatchesVertices(changed), "Reject changed UV mapping");
        changed = source;
        changed[0].color ^= 1;
        Require(!MatchesVertices(changed), "Do not subdivide a modified per-vertex color gradient");
        changed = source;
        changed[0].reciprocalZ ^= 1;
        Require(!MatchesVertices(changed), "Do not subdivide modified perspective mapping");
        changed = source;
        changed[0].z = std::numeric_limits<float>::quiet_NaN();
        Require(!MatchesVertices(changed), "Reject non-finite depth");
        const uint32_t infinityBits = 0x7F800000u;
        float infinity;
        std::memcpy(&infinity, &infinityBits, sizeof infinity);
        Require(!Finite(infinity) && !Finite(changed[0].z), "Reject non-finite aspects");
        std::cout << "Map backdrop tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
