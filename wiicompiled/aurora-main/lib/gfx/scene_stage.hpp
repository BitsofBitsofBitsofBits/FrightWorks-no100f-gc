#pragma once

#include "common.hpp"
#include "../webgpu/gpu.hpp"

namespace aurora::gfx::scene_stage {
// The scene stage: a point in each frame where the game's solid scene is finished and its see-through world draws
// (the world's second, blended pass, fog, particles, glow) have not been drawn yet, nor its HUD. Effects that belong
// to the solid scene run there, on the EFB's colour, reading its depth: ambient occlusion, later lighting.
//
// The game never marks that point, so it is taken from the cut-out modes' see-through tail: when the tail is flushed
// (common.cpp flush_deferred_draws, or at the next copy or the frame's end), the cut-out fringes stay in the pass,
// the pass ends, the stage runs, and the see-through draws start a new pass that keeps the colour and depth. When a
// frame flushes more than once, the stage runs at the last flush. A frame with no see-through tail (cut-out mode
// "game" or "cut", or nothing see-through) has no stage.

// Whether anything uses the stage: with nothing, the pass is not split and nothing is recorded.
bool wanted() noexcept;

// Ambient occlusion: how much the solid scene is darkened where surfaces crowd each other (creases, corners, under
// things), 0 = off, 1 = full. Clamped to [0, 1]; takes effect at the next frame.
void set_ambient_occlusion(float strength) noexcept;
float ambient_occlusion() noexcept;

// Debug view: 0 off, 1 tint (everything drawn before the stage is tinted blue; what is drawn after keeps its colour),
// 2 depth (the solid scene's distance from the camera, near = white), 3 the ambient occlusion alone (white = none),
// all with what is drawn after the stage on top.
void set_view(int mode) noexcept;
int view() noexcept;

// The solid scene's camera: the effective projection and the render viewport of the last perspective draw that wrote
// depth. The stage turns depth back into positions with it.
struct Camera {
  Mat4x4<float> projection{};
  webgpu::Viewport viewport{0.f, 0.f, 1.f, 1.f, 0.f, 1.f};
};
void note_camera(const Mat4x4<float>& projection, const webgpu::Viewport& viewport) noexcept;
const Camera& camera() noexcept;

// The stage itself, encoded between the two EFB passes: draws over `color` (the EFB, `samples` per pixel, `size`),
// reading `depth` (its depth buffer).
void encode(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& color, const wgpu::TextureView& depth,
            wgpu::Extent3D size, uint32_t samples, const Camera& camera);

// Releases the stage's GPU objects (before the device goes).
void shutdown() noexcept;
} // namespace aurora::gfx::scene_stage
