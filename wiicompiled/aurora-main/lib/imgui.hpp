#pragma once

#include <aurora/event.h>

union SDL_Event;

namespace wgpu {
class RenderPassEncoder;
} // namespace wgpu

namespace aurora::imgui {
void create_context() noexcept;
void initialize();
void shutdown();

void process_event(const SDL_Event& event) noexcept;
bool wants_capture_event(const SDL_Event& event) noexcept;
void new_frame(const AuroraWindowSize& size);
// Build this frame's ImGui draw data. Idempotent for the rest of the frame: the lists are built
// once and every presentation slot replays them. Reset by new_frame.
void render_frame_data() noexcept;
void render(const wgpu::RenderPassEncoder& pass) noexcept;
// This frame's draw lists into a capture image `scale` times the window's framebuffer size: only the
// AURORA_CAPTURE_STAMP_WINDOW window's list (stampOnly), or every list but AURORA_CAPTURE_HIDDEN_WINDOW's.
void render_capture(const wgpu::RenderPassEncoder& pass, bool stampOnly, float scale) noexcept;
} // namespace aurora::imgui
