#include "hle/no100f_confetti.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace No100fConfetti;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static bool Same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// Position on the colour wheel in degrees.
static float Hue(Rgb c) {
    const float r = c.r, g = c.g, b = c.b;
    const float high = std::max({r, g, b});
    const float span = high - std::min({r, g, b});
    float hue = high == r ? (g - b) / span : high == g ? 2.0f + (b - r) / span : 4.0f + (r - g) / span;
    hue *= 60.0f;
    return hue < 0.0f ? hue + 360.0f : hue;
}

int main() {
    try {
        // Bright and saturated: never muddy or pastel.
        for (const Rgb& c : kColors) {
            const int high = std::max({c.r, c.g, c.b});
            const int low = std::min({c.r, c.g, c.b});
            Require(high == 255, "a colour is not at full brightness");
            Require(low <= 60, "a colour is washed out");
        }

        // Spread around the wheel: no two colours close together and no big gap.
        float hues[kColorCount];
        for (size_t i = 0; i < kColorCount; ++i) hues[i] = Hue(kColors[i]);
        std::sort(hues, hues + kColorCount);
        for (size_t i = 0; i < kColorCount; ++i) {
            const float next = i + 1 < kColorCount ? hues[i + 1] : hues[0] + 360.0f;
            Require(next - hues[i] >= 20.0f, "two colours are nearly the same hue");
            Require(next - hues[i] <= 75.0f, "the colours leave a gap on the colour wheel");
        }

        // A group mixes all the colours with the same share each, at any size a burst can have (count 100, variance 50).
        for (uint32_t size = 50; size <= 150; ++size) {
            uint32_t shares[kColorCount] = {};
            for (uint32_t i = 0; i < size; ++i) {
                const Rgb c = Color(i);
                for (size_t k = 0; k < kColorCount; ++k) {
                    if (Same(c, kColors[k])) ++shares[k];
                }
            }
            const auto [low, high] = std::minmax_element(shares, shares + kColorCount);
            Require(*low >= size / kColorCount && *high - *low <= 1, "a colour does not get its share of a group");
        }

        // Neighbours differ, and a large index still gives a colour.
        for (uint32_t i = 0; i < 64; ++i) Require(!Same(Color(i), Color(i + 1)), "two neighbours have the same colour");
        Require(Same(Color(0xFFFFFFFFu), kColors[0xFFFFFFFFu % kColorCount]), "a large index does not wrap");

        std::cout << "no100f_confetti_tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
