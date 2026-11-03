#pragma once

#include <cstdint>

// The game's pad update (0x8000F5E8, GIHE78 rev 1) turns a GameCube PADStatus into the action bits the rest of the
// game reads. The bits are the engine's own, shared with the PS2 and Xbox versions, which have four shoulder actions:
// gum (PS2 L1, Xbox LT), soap bubble (L2, White), run (R1, RT) and sneak (R2, Black). The GameCube has only Z to spare,
// so its pad update reads Z as a modifier: Z + L is soap bubble and Z + R is sneak, and while Z is held L and R can't
// be gum or run. hle/input/no100f_pad_update.cpp reimplements that function with this mapping, and with the original
// Xbox controls ([controller] layout = "xbox_original") adds Black and White as actions of their own, so they work
// together with the triggers like on the Xbox.
namespace No100fPadActions {

// Action bits.
inline constexpr uint32_t kStart = 0x1;
inline constexpr uint32_t kUp = 0x10, kRight = 0x20, kDown = 0x40, kLeft = 0x80;
inline constexpr uint32_t kGum = 0x100, kBubble = 0x200, kRun = 0x1000, kSneak = 0x2000;
inline constexpr uint32_t kA = 0x10000, kX = 0x20000, kY = 0x40000, kB = 0x80000;  // by GameCube button
inline constexpr uint32_t kZ = 0x100000;  // GC Z held (set with every Z + L / Z + R action)

// PADStatus buttons.
inline constexpr uint16_t kPadLeft = 0x1, kPadRight = 0x2, kPadDown = 0x4, kPadUp = 0x8;
inline constexpr uint16_t kPadZ = 0x10, kPadR = 0x20, kPadL = 0x40;
inline constexpr uint16_t kPadA = 0x100, kPadB = 0x200, kPadX = 0x400, kPadY = 0x800, kPadStart = 0x1000;

// An analog trigger at or past this counts as pressed.
inline constexpr uint8_t kTriggerPressed = 0x18;

// The original mapping: buttons and analog triggers -> action bits.
constexpr uint32_t FromPad(uint16_t buttons, uint8_t triggerLeft, uint8_t triggerRight) {
    uint32_t out = 0;
    if (buttons & kPadLeft) out |= kLeft;
    if (buttons & kPadRight) out |= kRight;
    if (buttons & kPadDown) out |= kDown;
    if (buttons & kPadUp) out |= kUp;
    const bool left = (buttons & kPadL) || triggerLeft >= kTriggerPressed;
    const bool right = (buttons & kPadR) || triggerRight >= kTriggerPressed;
    if (buttons & kPadZ) {
        if (right) out |= kSneak;
        if (left) out |= kBubble;
        out |= kZ;
    } else {
        if (right) out |= kRun;
        if (left) out |= kGum;
    }
    if (buttons & kPadA) out |= kA;
    if (buttons & kPadB) out |= kB;
    if (buttons & kPadY) out |= kY;
    if (buttons & kPadX) out |= kX;
    if (buttons & kPadStart) out |= kStart;
    return out;
}

// Xbox Black / White: the same bits as GC Z + R / Z + L (what the game has always seen for sneak and soap bubble),
// without Z's hold on the triggers.
constexpr uint32_t AddXboxActions(uint32_t actions, bool black, bool white) {
    if (black) actions |= kSneak | kZ;
    if (white) actions |= kBubble | kZ;
    return actions;
}

// Stick axis (PADStatus, -128..127) -> the game's stick value: clamped to +-40, times 3.2, clamped to +-127,
// truncated. Single-precision float like the original (fsubs / fmuls / fctiwz).
inline int8_t Stick(int8_t value) {
    float v = static_cast<float>(value);
    if (v > 40.0f) v = 40.0f;
    else if (v < -40.0f) v = -40.0f;
    float scaled = 3.2f * v;
    if (scaled > 127.0f) scaled = 127.0f;
    else if (scaled < -127.0f) scaled = -127.0f;
    return static_cast<int8_t>(static_cast<int32_t>(scaled));
}

} // namespace No100fPadActions

// pad.cpp: Black / White held on this port in the last PADRead (original Xbox controls only).
namespace No100fPad {
void XboxButtons(uint32_t port, bool& black, bool& white);
}
