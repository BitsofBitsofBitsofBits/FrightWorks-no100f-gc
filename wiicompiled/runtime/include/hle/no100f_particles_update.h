#pragma once
// NO100F 60 fps ([video] fps60): the maths of particle fade and gravity, kept free of guest memory so tests can run it.
// The reasons are at the top of src/hle/no100f_particles_update.cpp.
#include <cmath>
#include <cstdint>

namespace No100fParticlesUpdate {

// A colour channel after one update: the game cuts rate * dt to a whole number (fctiwz), adds it and clamps.
inline uint8_t FadeChannel(uint8_t channel, float rate, float dt) {
    const float change = rate * dt;
    int32_t whole;
    if (std::isnan(change) || change <= -2147483648.0f) {
        whole = INT32_MIN;
    } else if (change >= 2147483648.0f) {
        whole = INT32_MAX;
    } else {
        whole = static_cast<int32_t>(change);
    }
    const int32_t sum = static_cast<int32_t>(static_cast<uint32_t>(channel) + static_cast<uint32_t>(whole));
    return static_cast<uint8_t>(sum < 0 ? 0 : (sum > 255 ? 255 : sum));
}

// With fps60, gives the time a frame's colour update uses: 0 until a 30 Hz step has built up, then the whole step. All
// calls in one frame get the same answer, so every particle group steps together.
class FadeClock {
public:
    float Step(uint32_t frame, float dt) {
        if (!started_ || frame != frame_) {
            started_ = true;
            frame_ = frame;
            pending_ += dt;
            if (pending_ >= 0.75f / 30.0f) {
                step_ = pending_;
                pending_ = 0.0f;
            } else {
                step_ = 0.0f;
            }
        }
        return step_;
    }

private:
    bool started_ = false;
    uint32_t frame_ = 0;
    float pending_ = 0.0f;
    float step_ = 0.0f;
};

// With fps60, the share of an acceleration a frame's velocity gets. A particle moves by its velocity every frame, and
// emit makes that velocity half as large at 60 frames a second, so its changes must be half as large too.
constexpr float kVelocityScaleFps60 = 0.5f;

}  // namespace No100fParticlesUpdate
