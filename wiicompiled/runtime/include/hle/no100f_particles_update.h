#pragma once
// NO100F 60 fps ([video] fps60): the timing of particle updates, kept free of guest memory so tests can run it.
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

// With fps60, gives the time of a frame's 30 Hz step: 0 until a step has built up, then the whole step. All calls in one
// frame get the same answer, so every particle group steps together.
class StepClock {
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

// With fps60, the share of its time a command that changes velocity gets. A particle moves by its velocity every frame,
// and emit makes that velocity half as large at 60 frames a second, so its changes must be half as large too.
constexpr float kVelocityScaleFps60 = 0.5f;

struct CommandTime {
    bool run;
    float time;
};

// With fps60, whether a PARS command runs this frame and the time it gets, from the frame's time and the 30 Hz step's
// (StepClock: 0 when no step ended this frame). Commands not named here don't depend on the rate.
inline CommandTime CommandTimeFps60(uint32_t type, float dt, float step) {
    switch (type) {
    case 2:   // acceleration
    case 4:   // push from a point (fog, updraft, hissing smoke)
    case 20:  // wind (rain)
    case 6:   // speed kill: speed squared against limit * time. Half the speed needs a quarter of the 30 fps limit, and
              // half the frame time is exactly that.
        return {true, dt * kVelocityScaleFps60};
    case 27:  // pull to the player (warp, web): also multiplies velocity per call, which no share of dt can fix
        return {step > 0.0f, step * kVelocityScaleFps60};
    case 13:  // texture animation: one frame per call, its timer reset with no carry
    case 15:  // random turn of the velocity: a random walk, so half the angle twice as often wanders too little
    case 21:  // spin: cuts angle + rate * time to a whole number
        return {step > 0.0f, step};
    default:
        return {true, dt};
    }
}

}  // namespace No100fParticlesUpdate
