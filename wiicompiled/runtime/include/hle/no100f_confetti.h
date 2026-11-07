// NO100F confetti colours: plain maths with no game state, so any effect can use them.
#pragma once

#include <cstddef>
#include <cstdint>

namespace No100fConfetti {

struct Rgb {
    uint8_t r, g, b;
};

// Bright, saturated colours spread around the colour wheel. Separate colours rather than a blend, so nothing lands on
// a muddy in-between shade.
inline constexpr Rgb kColors[] = {
    {255, 40, 40},  // red
    {255, 140, 0},  // orange
    {255, 235, 0},  // yellow
    {50, 255, 60},  // green
    {0, 230, 255},  // cyan
    {50, 90, 255},  // blue
    {170, 60, 255},  // violet
    {255, 60, 190},  // pink
};
inline constexpr size_t kColorCount = sizeof(kColors) / sizeof(kColors[0]);

// The colour of the index-th item in a group: every colour gets the same share. To scatter them like confetti, use it
// on items whose order has nothing to do with where they end up, as with a firework's randomly aimed particles.
inline constexpr Rgb Color(uint32_t index) {
    return kColors[index % kColorCount];
}

}  // namespace No100fConfetti
