#include <aurora/window_owner_sdl.hpp>
#include <SDL3/SDL_gamepad.h>

namespace {
// ImGui takes the south button as confirm and the east one as back. Give it the buttons printed A and B wherever they
// are: on the right and bottom on Nintendo pads, A and left on a GameCube pad (SDL's labels). Sony pads keep the
// positions. Its west button (tap: leave a child window, hold: window switcher) has no job in these menus and stays
// up. Menus only: the game's own mapping and the F10 rebind prompt read the pad directly.
bool MenuGamepadButton(SDL_Gamepad* gamepad, SDL_GamepadButton button) {
  if (button == SDL_GAMEPAD_BUTTON_WEST) return false;
  static constexpr SDL_GamepadButton kFaces[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
                                                 SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH};
  static constexpr SDL_GamepadButtonLabel kLabels[] = {SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_B,
                                                       SDL_GAMEPAD_BUTTON_LABEL_X, SDL_GAMEPAD_BUTTON_LABEL_Y};
  for (int i = 0; i < 4; ++i) {
    if (kFaces[i] != button) continue;
    for (const SDL_GamepadButton face : kFaces) {
      if (SDL_GetGamepadButtonLabel(gamepad, face) == kLabels[i]) return SDL_GetGamepadButton(gamepad, face);
    }
  }
  return SDL_GetGamepadButton(gamepad, button);
}
} // namespace

#define SDL_GetGamepadButton MenuGamepadButton
#include IMGUI_SDL_OWNER_SOURCE
