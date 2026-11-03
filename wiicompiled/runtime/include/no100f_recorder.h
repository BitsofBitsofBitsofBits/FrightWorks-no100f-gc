#pragma once

#include <string>

// F3 video recording, part of the developer tools ([game] dev_tools; the dev menu passes F3 on). The frames come from
// the renderer (aurora_set_video_capture), so only the game and the DEV stamp are in the video, or the F8/F10 menus too
// with [game] capture_overlay; ffmpeg encodes them to Videos\ beside the launcher (like Screenshots\), with a .txt next
// to each video: build, picture, Scooby's position at the start and the end, the [video] and [game] settings.
// [game] record_audio (default on) adds the game's own sound. Windows only; ffmpeg comes from PATH, beside the game or
// launcher (the launcher downloads it there) or tools\debug\local\ffmpeg; it is never shipped.
namespace no100f_recorder {
// F3: start a recording, or stop the one running. Game thread.
void Toggle() noexcept;
// Once per frame: stops at the time limit, when no frames come or ffmpeg stopped taking them. Game thread.
void Update() noexcept;
bool Recording() noexcept;
// Inside the ImGui frame: the camcorder light (top right) while recording or saving; never in captures.
void DrawIndicator() noexcept;
// The game is quitting: stop and finish the file before the process ends.
void FinishForExit() noexcept;
// A message for the corner notice ("Saving the video...", "Video saved: ..."), or "" when nothing new.
std::string TakeNotice();
} // namespace no100f_recorder
