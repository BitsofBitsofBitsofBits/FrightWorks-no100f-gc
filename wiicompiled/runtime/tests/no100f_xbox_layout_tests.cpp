#include "hle/no100f_pad_actions.h"
#include "hle/no100f_xbox_layout.h"
#include "hle/no100f_xbox_text.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace No100fXboxLayout;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}


static void Put32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(uint8_t(value >> shift));
}

// HIPA, DICT > ATOC > AHDR (TEXT at 0x100, 0x80 bytes), then the asset.
static std::vector<uint8_t> MakeHip(const std::string& text, const char* type = "TEXT") {
    std::vector<uint8_t> hip{'H', 'I', 'P', 'A', 0, 0, 0, 0};
    const auto tag = [&](const char* name) { hip.insert(hip.end(), name, name + 4); };
    tag("DICT"); Put32(hip, 8 + 8 + 16);
    tag("ATOC"); Put32(hip, 8 + 16);
    tag("AHDR"); Put32(hip, 16);
    Put32(hip, 0x1234); tag(type); Put32(hip, 0x100); Put32(hip, 0x80);
    hip.resize(0x100, 0);
    Put32(hip, uint32_t(text.size() + 1));
    hip.insert(hip.end(), text.begin(), text.end());
    hip.resize(0x180, 0);
    return hip;
}

static constexpr uint16_t kA = 0x0100, kStart = 0x1000;

int main() {
    try {
        for (uint32_t mode = 0; mode <= 12; ++mode)
            Require(IsMenuMode(mode) == (mode == 2 || mode == 4 || mode == 6 || mode == 7), "menu modes are 2, 4, 6 and 7");
        Require(IsMenuScene(uint32_t('H') << 24 | uint32_t('0') << 16 | uint32_t('0') << 8 | '3'), "Monster Gallery");
        Require(!IsMenuScene(uint32_t('H') << 24 | uint32_t('0') << 16 | uint32_t('0') << 8 | '1'), "hub");
        for (uint32_t value = 0; value <= 0xFFFF; ++value) {
            const auto buttons = static_cast<uint16_t>(value);
            Require(SwapBX(SwapBX(buttons)) == buttons, "swapping twice restores the buttons");
            Require((SwapBX(buttons) & ~(kB | kX)) == (buttons & ~(kB | kX)), "only B and X move");
        }

        // Gameplay: the controller's B (mapped to GC B) is the Xbox's dig/talk = GC X, and the other way round.
        PortState play;
        Require(Filter(play, kB, false, false, false) == kX, "gameplay: B -> GC X");
        Require(Filter(play, 0, false, false, false) == 0, "release");
        Require(Filter(play, kX, false, false, false) == kB, "gameplay: X -> GC B");
        Require(Filter(play, kA | kStart, false, false, false) == (kA | kStart), "A and Start unchanged");
        Require(Filter(play, kZ, false, false, false) == 0, "the bumper mapped to Z does nothing by itself");

        // Menus: B stays GC B (back).
        PortState menu{true};
        Require(Filter(menu, kB, true, false, false) == kB, "menu: B is back");
        Require(Filter(menu, kX, true, false, false) == kX, "menu: X unchanged");

        // B held into a menu (or out of one) must not count as the other action until released.
        PortState open;
        Require(Filter(open, kB, false, false, false) == kX, "talk");
        Require(Filter(open, kB, true, false, false) == 0, "held into the menu: ignored");
        Require(Filter(open, kB | kA, true, false, false) == kA, "other buttons still work");
        Require(Filter(open, 0, true, false, false) == 0, "released");
        Require(Filter(open, kB, true, false, false) == kB, "pressed again: back");
        // ... and B (back) held while the menu closes is not "talk" in the game.
        Require(Filter(open, kB, false, false, false) == 0, "held out of the menu: ignored");
        Require(Filter(open, 0, false, false, false) == 0, "released");
        Require(Filter(open, kB, false, false, false) == kX, "pressed again: talk");

        // The bumpers are only recorded (the pad update adds them as actions); nothing else changes.
        PortState shoulders;
        Require(Filter(shoulders, kL | kR, false, true, true) == (kL | kR), "gum + run with both bumpers");
        Require(shoulders.black && shoulders.white, "bumpers recorded");
        Require(Filter(shoulders, 0, false, false, false) == 0 && !shoulders.black && !shoulders.white, "released");

        // The game's pad update (0x8000F5E8), as disassembled.
        namespace P = No100fPadActions;
        Require(P::FromPad(0, 0, 0) == 0, "nothing");
        Require(P::FromPad(P::kPadLeft | P::kPadRight | P::kPadDown | P::kPadUp, 0, 0) ==
                (P::kLeft | P::kRight | P::kDown | P::kUp), "D-pad");
        Require(P::FromPad(P::kPadA, 0, 0) == 0x10000 && P::FromPad(P::kPadB, 0, 0) == 0x80000 &&
                P::FromPad(P::kPadX, 0, 0) == 0x20000 && P::FromPad(P::kPadY, 0, 0) == 0x40000 &&
                P::FromPad(P::kPadStart, 0, 0) == 0x1, "face buttons and Start");
        Require(P::FromPad(P::kPadL, 0, 0) == P::kGum && P::FromPad(P::kPadR, 0, 0) == P::kRun, "L gum, R run");
        Require(P::FromPad(0, 0x17, 0x17) == 0, "analog triggers below 0x18");
        Require(P::FromPad(0, 0x18, 0x18) == (P::kGum | P::kRun), "analog triggers at 0x18");
        Require(P::FromPad(P::kPadZ, 0, 0) == P::kZ, "Z alone");
        Require(P::FromPad(P::kPadZ | P::kPadL, 0, 0) == (P::kBubble | P::kZ), "Z + L soap bubble");
        Require(P::FromPad(P::kPadZ | P::kPadR, 0, 0) == (P::kSneak | P::kZ), "Z + R sneak");
        Require(P::FromPad(P::kPadZ, 0x18, 0x18) == (P::kBubble | P::kSneak | P::kZ), "Z + analog triggers");
        // Black / White on top of the triggers: what GC Z can't do.
        Require(P::AddXboxActions(P::FromPad(P::kPadR, 0, 0), true, false) == (P::kRun | P::kSneak | P::kZ),
                "run + sneak");
        Require(P::AddXboxActions(P::FromPad(P::kPadL, 0, 0), false, true) == (P::kGum | P::kBubble | P::kZ),
                "gum + bubble");
        Require(P::AddXboxActions(P::kA, false, false) == P::kA, "no bumpers: unchanged");
        // Sticks: clamp to +-40, times 3.2 in single precision, clamp to +-127, truncate.
        Require(P::Stick(0) == 0 && P::Stick(1) == 3 && P::Stick(5) == 16 && P::Stick(-5) == -16, "small values");
        Require(P::Stick(39) == 124 && P::Stick(40) == 127 && P::Stick(127) == 127, "clamped high");
        Require(P::Stick(-39) == -124 && P::Stick(-128) == -127, "clamped low");

        // Tutorial hints (texts as on the disc).
        using No100fXboxText::Rewrite;
        Require(Rewrite("Make tracks Scooby-Doo! You can run faster by holding the R Button while moving!") ==
                "Make tracks Scooby-Doo! You can run faster by holding the RT Button while moving!", "run");
        Require(Rewrite("Now you can press the Z Button and L Button at the same time to trap a monster in a soap "
                        "bubble and jump on top of it!") ==
                "Now you can press the LB Button to trap a monster in a soap bubble and jump on top of it!", "bubble");
        Require(Rewrite("Hold down the Z + R Button while moving forward to try it out.") ==
                "Hold down the RB Button while moving forward to try it out.", "slippers");
        Require(Rewrite("Press the Z Button and R Button at the same time to hide from and sneak past monsters.") ==
                "Press the RB Button to hide from and sneak past monsters.", "knight");
        Require(Rewrite("by holding the Z Button and R Button at the same time while standing still.") ==
                "by holding the RB Button while standing still.", "lampshade");
        Require(Rewrite("Press the $D~$C Button when standing in a flower patch to dig.") ==
                "Press the $D~$C Button when standing in a flower patch to dig.", "button pictures unchanged");

        // Black / White rebound ([controller] xbox_black / xbox_white).
        using No100fXboxText::ButtonName;
        Require(ButtonName("right_shoulder") == "RB" && ButtonName("left_shoulder") == "LB" && ButtonName("north") == "Y" &&
                    ButtonName("left_trigger@30") == "LT" && ButtonName("right_paddle1") == "P1",
                "Xbox button names");
        Require(ButtonName("disabled").empty() && ButtonName("left_stick_up").empty() && ButtonName("nonsense").empty(),
                "no name: hint left as on the disc");
        const No100fXboxText::Names rebound{"Y", "RT"};
        Require(Rewrite("Press the Z Button and R Button at the same time to hide from and sneak past monsters.", rebound) ==
                    "Press the Y Button to hide from and sneak past monsters.", "rebound Black");
        Require(Rewrite("Now you can press the Z Button and L Button at the same time to trap a monster.", rebound) ==
                    "Now you can press the RT Button to trap a monster.", "rebound White");
        const No100fXboxText::Names off{"", ""};
        Require(Rewrite("Hold down the Z + R Button while moving forward to try it out.", off) ==
                    "Hold down the Z + R Button while moving forward to try it out.", "Black off: unchanged");
        Require(Rewrite("by holding the R Button while moving!", off) == "by holding the RT Button while moving!",
                "run hint rewritten either way");
        // The GameCube buttons only the Black / White pad buttons press are dropped.
        constexpr uint16_t Z = 0x0010, R = 0x0020, A = 0x0100;
        using No100fXboxLayout::DropBound;
        Require(DropBound(Z | A, Z, 0) == A, "Black's Z dropped");
        Require(DropBound(Z | A, Z, Z) == (Z | A), "Z kept while another pad button sends it");
        Require(DropBound(R | A, Z, 0) == (R | A), "unbound buttons kept");

        const auto hip = MakeHip("Now you can press the Z Button and L Button at the same time to trap a monster.");
        const auto patches = No100fXboxText::FindPatches(hip.data(), hip.size());
        Require(patches.size() == 1 && patches[0].offset == 0x100 && patches[0].bytes.size() == 0x80, "one TEXT patch");
        const std::string expected = "Now you can press the LB Button to trap a monster.";
        // Applied to reads in two pieces that split the asset.
        std::vector<uint8_t> first(hip.begin(), hip.begin() + 0x110), second(hip.begin() + 0x110, hip.end());
        No100fXboxText::Apply(patches, 0, first);
        No100fXboxText::Apply(patches, 0x110, second);
        first.insert(first.end(), second.begin(), second.end());
        Require(No100fXboxText::ReadBe32(first.data() + 0x100) == expected.size() + 1, "new length");
        Require(std::string(reinterpret_cast<const char*>(first.data() + 0x104)) == expected, "new text");
        Require(std::equal(first.begin(), first.begin() + 0x100, hip.begin()), "header untouched");
        Require(No100fXboxText::FindPatches(MakeHip("Press the R Button", "SND ").data(), 0x180).empty(),
                "only TEXT assets");
        Require(No100fXboxText::FindPatches(MakeHip("Press the $D#$C Button").data(), 0x180).empty(), "nothing to change");
        std::string longText(0x7B, 'x');
        longText.replace(0, 12, "the R Button");
        Require(No100fXboxText::FindPatches(MakeHip(longText).data(), 0x180).empty(), "must fit the asset");
    } catch (const std::exception& error) {
        std::cerr << "no100f_xbox_layout_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "no100f_xbox_layout_tests: passed\n";
    return 0;
}
