// NO100F rainbow colours: plain maths with no game state, so any effect can use them.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace No100fRainbow {

struct Rgb {
    uint8_t r, g, b;
};

// The colour at a position through the rainbow: 0 is red, 1 is violet, and anything outside that is clamped.
inline Rgb Color(float position) {
    static constexpr Rgb kStops[] = {
        {255, 30, 30}, {255, 130, 0}, {255, 240, 0}, {40, 230, 40}, {40, 90, 255}, {190, 40, 255},
    };
    constexpr size_t kSpans = sizeof(kStops) / sizeof(kStops[0]) - 1;
    const float clamped = position > 0.0f ? (position < 1.0f ? position : 1.0f) : 0.0f;  // NaN becomes 0
    const float scaled = clamped * static_cast<float>(kSpans);
    const size_t span = scaled < static_cast<float>(kSpans) ? static_cast<size_t>(scaled) : kSpans - 1;
    const float along = scaled - static_cast<float>(span);
    const auto mix = [along](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * along + 0.5f);
    };
    const Rgb& a = kStops[span];
    const Rgb& b = kStops[span + 1];
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

// Where a firework particle travelling in direction (x, y, z) sits in a rainbow that runs from the top of the burst to
// the bottom, so a burst shows the same bands from any side and overlapping particles keep their colour. The game aims
// these particles with two evenly random angles, so up = cos a * cos b crowds around level. Half of up plus half of
// its square root evens that out: each of the six colours gets 15 to 18 percent of the particles (a straight line in
// up gives the middle two 24 percent each and the ends 12).
inline float BurstPosition(float x, float y, float z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (!(length > 0.0f)) return 0.5f;
    const float up = y / length;
    const float spread = 0.5f * (std::fabs(up) + std::sqrt(std::fabs(up)));
    return 0.5f - 0.5f * (up < 0.0f ? -spread : spread);
}

}  // namespace No100fRainbow
