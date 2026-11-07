#include "hle/no100f_rainbow.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace No100fRainbow;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static bool Same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

int main() {
    try {
        // The ends are red and violet, and anything past them stays there.
        Require(Same(Color(0.0f), Rgb{255, 30, 30}), "position 0 is not red");
        Require(Same(Color(1.0f), Rgb{190, 40, 255}), "position 1 is not violet");
        Require(Same(Color(-3.0f), Color(0.0f)), "a position below 0 is not clamped");
        Require(Same(Color(7.0f), Color(1.0f)), "a position above 1 is not clamped");
        Require(Same(Color(std::numeric_limits<float>::quiet_NaN()), Color(0.0f)), "a NaN position is not handled");

        // Exactly on a stop, halfway between stops, and no jump anywhere along the way.
        Require(Same(Color(0.4f), Rgb{255, 240, 0}), "position 0.4 is not yellow");
        Require(Same(Color(0.6f), Rgb{40, 230, 40}), "position 0.6 is not green");
        const Rgb between = Color(0.1f);
        Require(between.r == 255 && between.g == 80 && between.b == 15, "position 0.1 is not halfway from red to orange");
        Rgb last = Color(0.0f);
        for (int i = 1; i <= 1000; ++i) {
            const Rgb now = Color(static_cast<float>(i) / 1000.0f);
            Require(std::abs(now.r - last.r) <= 3 && std::abs(now.g - last.g) <= 3 && std::abs(now.b - last.b) <= 3,
                    "the rainbow jumps between neighbouring positions");
            last = now;
        }

        // A burst runs from red straight up, through the middle when level, to violet straight down.
        Require(BurstPosition(0.0f, 1.0f, 0.0f) == 0.0f, "straight up is not the start of the rainbow");
        Require(BurstPosition(0.0f, -1.0f, 0.0f) == 1.0f, "straight down is not the end of the rainbow");
        Require(BurstPosition(1.0f, 0.0f, 0.0f) == 0.5f, "level is not the middle of the rainbow");
        Require(BurstPosition(0.0f, 0.15f, 0.0f) == 0.0f, "the speed changed the colour");
        Require(BurstPosition(0.0f, 0.0f, 0.0f) == 0.5f, "a particle that isn't moving has no colour");

        // With the game's own aim (the up column of fn_8002EAD8, angles (rand - 0.5) * 6.2657), every colour gets a share.
        constexpr int kParticles = 200000;
        constexpr int kBands = 6;
        constexpr float kAngleRange = 6.2657f;
        int bands[kBands] = {};
        std::mt19937 random(1);
        std::uniform_real_distribution<float> unit(-0.5f, 0.5f);
        for (int i = 0; i < kParticles; ++i) {
            const float a = unit(random) * kAngleRange;
            const float b = unit(random) * kAngleRange;
            const float position = BurstPosition(std::cos(a) * std::sin(b), std::cos(a) * std::cos(b), -std::sin(a));
            int band = static_cast<int>(position * kBands);
            bands[band < kBands ? band : kBands - 1]++;
        }
        for (const int count : bands) {
            const float share = static_cast<float>(count) / kParticles;
            Require(share > 0.14f && share < 0.19f, "a colour band does not get an even share of a burst");
        }

        std::cout << "no100f_rainbow_tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
