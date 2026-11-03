#pragma once

// Dev-menu logic that needs no SDL, ImGui or guest memory, so it can be unit tested.

#include <cstdint>
#include <cctype>
#include <string_view>

namespace no100f_devmenu {

// Which button skips what is playing: A for a recorded cutscene, Start for the title intro / attract demo (mode 2).
enum class SkipControl { None, Confirm, Start };

inline SkipControl SkipControlFor(uint32_t mode, uint32_t state, bool recordedCutscene) {
    if (recordedCutscene) return SkipControl::Confirm;
    if (mode == 2 && (state == 0 || state == 1)) return SkipControl::Start;
    return SkipControl::None;
}

// devmenu_textures.txt: the seventh column marks Effects-tab entries. Accept any case, leading spaces and suffixes
// ("effect", " EFFECT\r", "effect:particle").
inline bool IsEffectEntry(std::string_view category) {
    while (!category.empty() && std::isspace(static_cast<unsigned char>(category.front())))
        category.remove_prefix(1);
    constexpr std::string_view prefix = "effect";
    if (category.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(category[i])) != prefix[i]) return false;
    return true;
}

// One queued scene change: waits until the game is in play with no cutscene and no portal pending, submits once,
// then reports arrival; gives up after 60 s.
class SceneRequest {
public:
    enum class Action { Wait, Submit, Arrived, Timeout };

    bool Begin(uint32_t destination, uint64_t now) {
        if (destination == 0 || destination_ != 0) return false;
        destination_ = destination;
        deadline_ = now + 60000;
        submitted_ = false;
        return true;
    }

    Action Update(uint32_t current, bool ready, bool cinematic, bool portalPending, uint64_t now) {
        if (!Active()) return Action::Wait;
        if (now >= deadline_) {
            Cancel();
            return Action::Timeout;
        }
        if (!ready) return Action::Wait;
        if (current == destination_ && (submitted_ || !cinematic)) {
            Cancel();
            return Action::Arrived;
        }
        if (!submitted_ && !cinematic && !portalPending) {
            submitted_ = true;
            return Action::Submit;
        }
        return Action::Wait;
    }

    bool Active() const { return destination_ != 0; }
    bool Submitted() const { return Active() && submitted_; }
    void Cancel() { destination_ = 0; }

private:
    uint32_t destination_ = 0;
    uint64_t deadline_ = 0;
    bool submitted_ = false;
};

// Skip button: press for 100 ms every 500 ms while the same sequence plays, for up to 10 s.
class SkipRequest {
public:
    void Begin(uint64_t now) {
        started_ = now;
        active_ = true;
    }

    bool Press(uint64_t now, bool cinematic) {
        if (!active_) return false;
        if (!cinematic || now - started_ >= 10000) {
            active_ = false;
            return false;
        }
        // Fresh presses survive the movie's initial unskippable interval.
        return (now - started_) % 500 < 100;
    }

    bool Active() const { return active_; }

private:
    uint64_t started_ = 0;
    bool active_ = false;
};

} // namespace no100f_devmenu
