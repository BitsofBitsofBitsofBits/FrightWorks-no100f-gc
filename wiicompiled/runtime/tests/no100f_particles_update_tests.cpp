#include "hle/no100f_particles_update.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace No100fParticlesUpdate;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

namespace {
// A channel at 30 fps, as the game does it: every frame, the frame's time.
uint8_t Fade30(uint8_t channel, float rate, float seconds) {
    const int frames = static_cast<int>(seconds * 30.0f + 0.5f);
    for (int i = 0; i < frames; ++i) channel = FadeChannel(channel, rate, 1.0f / 30.0f);
    return channel;
}

// A channel at 60 fps through the clock.
uint8_t Fade60(uint8_t channel, float rate, float seconds) {
    FadeClock clock;
    const int frames = static_cast<int>(seconds * 60.0f + 0.5f);
    for (int i = 0; i < frames; ++i) channel = FadeChannel(channel, rate, clock.Step(i, 1.0f / 60.0f));
    return channel;
}

// A channel at 60 fps with no clock: the bug.
uint8_t Fade60Raw(uint8_t channel, float rate, float seconds) {
    const int frames = static_cast<int>(seconds * 60.0f + 0.5f);
    for (int i = 0; i < frames; ++i) channel = FadeChannel(channel, rate, 1.0f / 60.0f);
    return channel;
}

// World distance a particle falls in the given time: velocity changes, then moves, as PARS command order 2, 3 does.
float Fall(float acceleration, int framesPerSecond, float scale, float seconds) {
    const float dt = 1.0f / static_cast<float>(framesPerSecond);
    const int frames = static_cast<int>(seconds * static_cast<float>(framesPerSecond) + 0.5f);
    float velocity = 0.0f, position = 0.0f;
    for (int i = 0; i < frames; ++i) {
        velocity += acceleration * (dt * scale);
        position += velocity;
    }
    return position;
}
}  // namespace

int main() {
    try {
        // The bug: the fireworks' two alpha rates, as measured with fps60 on and no fix.
        Require(Fade60Raw(255, -51.0f, 4.1f) == 255, "FW1 does not fade at 60 fps without the clock");
        Require(Fade60Raw(255, -102.0f, 1.27f) == 179, "FW2 alpha at 1.27 s at 60 fps without the clock");

        // Fireworks at the end of their lives, 30 fps and fixed 60 fps.
        Require(Fade30(255, -51.0f, 5.0f) == 105, "FW1 at 30 fps ends at 105");
        Require(Fade60(255, -51.0f, 5.0f) == 105, "FW1 at 60 fps ends at 105");
        Require(Fade30(255, -102.0f, 2.5f) == 30, "FW2 at 30 fps ends at 30");
        Require(Fade60(255, -102.0f, 2.5f) == 30, "FW2 at 60 fps ends at 30");

        // Other rates from the disc, at every point in a life, and channels rising as well as falling.
        for (float rate : {-510.0f, -340.0f, -170.0f, -106.7f, -36.4f, -42.3f, 64.0f, 150.0f, 13.0f}) {
            for (float seconds = 0.0f; seconds < 2.0f; seconds += 1.0f / 30.0f) {
                Require(Fade30(128, rate, seconds) == Fade60(128, rate, seconds), "same fade at 30 and 60 fps");
            }
        }

        // Clamps
        Require(FadeChannel(1, -510.0f, 1.0f / 30.0f) == 0, "clamps at 0");
        Require(FadeChannel(254, 510.0f, 1.0f / 30.0f) == 255, "clamps at 255");

        // Zero life gives an infinite rate: fctiwz saturates and the add wraps, never undefined behaviour.
        const float inf = std::numeric_limits<float>::infinity();
        Require(FadeChannel(200, -inf, 1.0f / 30.0f) == 0, "negative infinity clamps to 0");
        Require(FadeChannel(200, inf, 1.0f / 30.0f) == 0, "positive infinity wraps as on the console");
        Require(FadeChannel(7, inf * 0.0f, 1.0f / 30.0f) == 0, "NaN acts as the most negative word");

        // Slowdown: a frame as long as a 30 Hz step gets the fade on its own.
        {
            FadeClock clock;
            Require(clock.Step(1, 1.0f / 30.0f) == 1.0f / 30.0f, "a whole step fades at once");
            Require(clock.Step(2, 1.0f / 60.0f) == 0.0f, "half a step waits");
            Require(clock.Step(3, 1.0f / 30.0f) > 1.0f / 30.0f, "the wait is kept, not lost");
        }

        // Every group in one frame gets the same answer, and no time is counted twice.
        {
            FadeClock clock;
            Require(clock.Step(10, 1.0f / 60.0f) == 0.0f, "first frame of a step");
            Require(clock.Step(10, 1.0f / 60.0f) == 0.0f, "second group, same frame");
            const float step = clock.Step(11, 1.0f / 60.0f);
            Require(std::fabs(step - 1.0f / 30.0f) < 1e-6f, "second frame gets the whole step");
            Require(clock.Step(11, 1.0f / 60.0f) == step, "second group gets it too");
            Require(clock.Step(12, 1.0f / 60.0f) == 0.0f, "next step starts over");
        }

        // A frame counter that starts at 0 still counts its first frame.
        {
            FadeClock clock;
            clock.Step(0, 1.0f / 60.0f);
            Require(std::fabs(clock.Step(1, 1.0f / 60.0f) - 1.0f / 30.0f) < 1e-6f, "frame 0 counts");
        }

        // Fall: the fireworks' -0.03. Unscaled it falls twice as far at 60 fps; scaled it matches 30 fps.
        for (float seconds : {1.0f, 2.5f, 5.0f}) {
            const float at30 = Fall(-0.03f, 30, 1.0f, seconds);
            Require(std::fabs(Fall(-0.03f, 60, 1.0f, seconds) / at30 - 2.0f) < 0.05f, "unscaled falls twice as far");
            Require(std::fabs(Fall(-0.03f, 60, kVelocityScaleFps60, seconds) / at30 - 1.0f) < 0.03f, "scaled matches");
        }
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "no100f_particles_update_tests passed\n";
    return 0;
}
