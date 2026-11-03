#include "no100f_devmenu_state.h"

#include <cassert>

int main() {
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
