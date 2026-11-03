#include "no100f_devmenu_state.h"

#include <cassert>
#include <iterator>
#include <string_view>

int main() {
    using namespace no100f_devmenu;
    constexpr const char* labels[] = {
        "Map", "Shovel", "Springs", "Helmet", "Lamp", "Slippers", "Knight Armor", "Galoshes (Boots)", "Super Smash",
        "Super Sonic Smash (Stun)", "Umbrella", "Plunger Boots", "Bubblegum", "Soap Bubbles",
        "Flower Pot", "Diving Helmet",
    };
    constexpr const char* keysByBit[] = {
        "BubbleGum", "SoapBar", "StickyBoots", "Plungers", "Slippers", "LampShade", "BlackKnight",
        "FlowerPot", "DivingHelmet", "Spring", "LightningBolt", "FootballHelmet", "Umbrella", "Shovel",
        "LightningBoltStun",
    };
    static_assert(std::size(kPowerupList) == std::size(labels));
    static_assert(kAllPowerups == 0x7FFFu);
    static_assert(kMainGamePowerups == 0x7E7Fu);
    assert(std::string_view(kUnusedPowerupHeading) == "--UNUSED--");
    uint32_t seen = 0;
    for (size_t i = 0; i < std::size(kPowerupList); ++i) {
        const auto& power = kPowerupList[i];
        assert(std::string_view(power.label) == labels[i]);
        assert(power.unused == (i >= 14));
        if (power.bit == 0) {
            assert(i == 0 && std::string_view(power.key) == "Map");
            assert(!PowerupEnabled(power, 0));
            assert(PowerupEnabled(power, 1));
            assert(WithPowerup(power, 0, true) == 1);
            assert(WithPowerup(power, 1, false) == 0);
            continue;
        }
        assert((seen & power.bit) == 0);
        seen |= power.bit;
        unsigned bit = 0;
        while ((1u << bit) != power.bit) {
            ++bit;
            assert(bit < std::size(keysByBit));
        }
        assert(std::string_view(power.key) == keysByBit[bit]);
        for (uint32_t current : {0u, 0x80000000u, 0xFFFFFFFFu, 0xA5A51234u}) {
            const uint32_t enabled = WithPowerup(power, current, true);
            const uint32_t disabled = WithPowerup(power, current, false);
            assert(PowerupEnabled(power, enabled));
            assert(!PowerupEnabled(power, disabled));
            assert((enabled & ~power.bit) == (current & ~power.bit));
            assert((disabled & ~power.bit) == (current & ~power.bit));
        }
    }
    assert(seen == kAllPowerups);
    constexpr uint32_t unused = kAllPowerups & ~kMainGamePowerups;
    assert(WithPowerupBits(0x80000000u, kMainGamePowerups, kMainGamePowerups) == 0x80007E7Fu);
    assert((WithPowerupBits(unused, kMainGamePowerups, kMainGamePowerups) & unused) == unused);
    assert(WithPowerupBits(0xFFFFFFFFu, kAllPowerups, 0) == 0xFFFF8000u);
    assert(WithPowerupBits(0x80000000u, kAllPowerups, 0xFFFFFFFFu) == 0x80007FFFu);

    using no100f_devmenu::SkipControl;
    assert(no100f_devmenu::SkipControlFor(2, 0, false) == SkipControl::Start);
    assert(no100f_devmenu::SkipControlFor(2, 1, false) == SkipControl::Start);
    assert(no100f_devmenu::SkipControlFor(2, 2, false) == SkipControl::None);
    assert(no100f_devmenu::SkipControlFor(11, 1, true) == SkipControl::Confirm);
    assert(no100f_devmenu::SkipControlFor(11, 1, false) == SkipControl::None);
    assert(no100f_devmenu::IsEffectEntry("effect"));
    assert(no100f_devmenu::IsEffectEntry(" EFFECT\r"));
    assert(no100f_devmenu::IsEffectEntry("effect:particle"));
    assert(!no100f_devmenu::IsEffectEntry(""));
    assert(!no100f_devmenu::IsEffectEntry("texture"));
    using no100f_devmenu::SceneRequest;
    using Action = SceneRequest::Action;
    SceneRequest travel;
    assert(!travel.Begin(0, 100));
    assert(travel.Begin(2, 100));
    assert(!travel.Begin(3, 200));
    assert(travel.Update(1, false, false, false, 300) == Action::Wait);
    assert(travel.Update(1, true, true, false, 400) == Action::Wait);
    assert(travel.Update(1, true, false, true, 500) == Action::Wait);
    assert(travel.Update(1, true, false, false, 600) == Action::Submit);
    assert(travel.Submitted());
    assert(travel.Update(1, true, false, false, 700) == Action::Wait);
    assert(travel.Update(2, false, false, false, 800) == Action::Wait);
    assert(travel.Update(2, true, true, false, 900) == Action::Arrived);
    assert(!travel.Active());
    assert(!travel.Submitted());
    assert(travel.Begin(3, 1000));
    assert(travel.Update(2, false, false, false, 61000) == Action::Timeout);
    assert(!travel.Active());
    assert(travel.Begin(3, 62000));
    travel.Cancel();
    assert(!travel.Active());

    no100f_devmenu::SkipRequest skip;
    assert(!skip.Press(0, true));
    skip.Begin(100);
    assert(skip.Press(100, true));
    assert(!skip.Press(200, true));
    assert(skip.Press(600, true));
    assert(!skip.Press(601, false));
    assert(!skip.Active());
    assert(!skip.Press(700, true));
    skip.Begin(1000);
    assert(skip.Press(10499, true) == false);
    assert(!skip.Press(11000, true));
    assert(!skip.Active());
}
