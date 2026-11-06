#pragma once

#include "common.hpp"
#include "../webgpu/gpu.hpp"

namespace aurora::gfx::motion {
// What the EFB motion targets (see motion_vectors_active) are used for when a frame is presented: motion blur, and a
// debug view of the vectors. Either one turns the motion vectors on (request_motion_vectors); with both off they cost
// nothing.

// Motion blur: how many frames of motion each pixel is smeared over (0.5 = a film camera's 180-degree shutter; more
// than 1 is stylised, longer than any real shutter), 0 = off. Clamped to [0, 4].
void set_blur(float shutter) noexcept;
float blur() noexcept;

// Debug view: 0 off, 1 the motion as color instead of the picture, 2 over the picture; `scale` = the motion in
// presented pixels per frame that shows full color (<= 0 leaves it).
void set_view(int mode, float scale) noexcept;
int view() noexcept;
float view_scale() noexcept;

// The picture to present for `source` (the frame's display copy): blurred and/or replaced by the view, or `source`
// itself when neither is on. `frameFraction` scales the blur: how many of the targets' motions make a game frame's
// (more than 1 for an interpolated image, whose motion covers only part of the frame).
webgpu::PresentSource encode(const wgpu::CommandEncoder& encoder, const webgpu::PresentSource& source,
                             const MotionTargets& targets, float frameFraction);

// Releases the passes' GPU objects (before the device goes).
void shutdown() noexcept;
} // namespace aurora::gfx::motion
