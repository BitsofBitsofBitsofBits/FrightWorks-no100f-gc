#pragma once

#include <cstdint>

namespace MusicAttenuation {

// Enables the optional external-media integration (Windows media sessions,
// Core Audio output activity on macOS, or MPRIS over D-Bus on Linux).
// The monitor is started lazily the first time
// this is enabled.
void SetEnabled(bool enabled) noexcept;
void SetMusicVolume(float volume) noexcept;
void SetSoundEffectsVolume(float volume) noexcept;
void SetUiVolume(float volume) noexcept;
void SetVoicesVolume(float volume) noexcept;
bool IsExternalMediaPlaying() noexcept;
bool IsMediaControlAvailable() noexcept;
bool IsMediaControlInitializationComplete() noexcept;

// Native MIXAdjustFader (0x800DD2A0) for the game's sound engine: applies the delta and the
// voice's category volume (music, sound effects, menus, voices) as a fader offset.
void AdjustMixFader(uint32_t voice, int32_t delta);

} // namespace MusicAttenuation
