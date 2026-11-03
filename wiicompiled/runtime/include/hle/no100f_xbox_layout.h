#pragma once

#include <cstdint>

// Original Xbox controls for Night of 100 Frights ([controller] layout = "xbox_original"), built on top of the
// player's GameCube mapping instead of replacing it:
//   GameCube: A jump, B head smash + menu back, X dig/enter/talk, Y display, L gum, R run, Z+L bubble, Z+R sneak.
//   Xbox:     A jump, X head smash, B dig/enter/talk + menu back, Y display, LT gum, RT run, White bubble, Black sneak.
// With the default mapping (B east, X west, Z right bumper) that means: swap GC B/X outside the menus, drop Z, and
// turn the bumpers into the Xbox's White (left) and Black (right) buttons. Those reach the game as actions of their
// own (hle/no100f_pad_actions.h, the pad update override), so they work together with the triggers.
// Based on the Xbox layout BitsofBitsofBitsofBits wrote for FrightWorks PR #12.
namespace No100fXboxLayout {

inline constexpr uint16_t kZ = 0x0010, kR = 0x0020, kL = 0x0040, kB = 0x0200, kX = 0x0400;

// The game's mode (GIHE78 rev 1). In these GC B is "back" and stays on the B position: title and options 2,
// load game 4, the Mystery Machine (save) menu 6, pause 7. Gameplay is 11.
inline constexpr uint32_t kModeAddress = 0x80270E18;
constexpr bool IsMenuMode(uint32_t mode) { return mode == 2 || mode == 4 || mode == 6 || mode == 7; }

// The Monster Gallery (scene H003) keeps them too: on the Xbox, X views the pictures and B leaves a monster's page
// (checked in an Xbox emulator). Scene ID: [[kScenePointer] + 0x6C], four characters.
inline constexpr uint32_t kScenePointer = 0x80245CF4, kSceneIdOffset = 0x6C;
constexpr bool IsMenuScene(uint32_t scene) { return scene == 0x48303033; }  // 'H003'

constexpr uint16_t SwapBX(uint16_t buttons) {
    return static_cast<uint16_t>((buttons & ~(kB | kX)) | ((buttons & kB) << 1) | ((buttons & kX) >> 1));
}

// The GameCube buttons that only the Black / White pad buttons pressed are dropped: with this layout those buttons
// send Black / White instead. `bound` = GameCube buttons mapped to the Black / White pad buttons, `others` = GameCube
// buttons another held pad button sends.
constexpr uint16_t DropBound(uint16_t buttons, uint16_t bound, uint16_t others) {
    return static_cast<uint16_t>(buttons & ~(bound & ~others));
}

struct PortState {
    bool menu = false;
    uint16_t suppressed = 0;  // B/X held when the menu state changed: ignored until released
    bool black = false, white = false;
};

// `gamepad`: the GC buttons from this controller's mapping (keyboard input is added later, unchanged).
// `black` / `white`: the Black / White buttons held ([controller] xbox_black / xbox_white, default right / left bumper),
// kept for the pad update (No100fPad::XboxButtons).
inline uint16_t Filter(PortState& state, uint16_t gamepad, bool menu, bool black, bool white) {
    uint16_t out = static_cast<uint16_t>(gamepad & ~kZ);
    if (!menu) out = SwapBX(out);
    // A B/X still held when the menu state changes (B = back closing a menu) would be the other action.
    if (menu != state.menu) {
        state.menu = menu;
        state.suppressed = out & (kB | kX);
    }
    state.suppressed &= out;
    out = static_cast<uint16_t>(out & ~state.suppressed);
    state.black = black;
    state.white = white;
    return out;
}

} // namespace No100fXboxLayout
