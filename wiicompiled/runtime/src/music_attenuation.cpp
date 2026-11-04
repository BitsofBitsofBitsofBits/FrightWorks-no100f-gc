#include "music_attenuation.h"

#include "memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#elif defined(__APPLE__)
#include "external_audio_macos.h"
#elif defined(__linux__)
#include <dlfcn.h>

#include <string>
#include <vector>
#endif

namespace MusicAttenuation {
namespace {

// NO100F sound engine (iSnd over the GameCube MIX library). Every frame the engine works out each playing sound's
// volume (fn_800D928C: the sound's own volume, 3D distance, the game's own music/SFX option and master volume) and
// sets it as the voice's MIX fader with MIXAdjustFader (0x800DD2A0, only caller). Sound records: 64 x 0x48 bytes
// from 0x80237E18; 0-5 are disc streams (voice = word 0 of the 0x104-byte stream slot at 0x8024C258), 6-63 are
// ARAM sounds (voice = word 0 of the 0x20-byte slot at 0x8024BB18). Record +0x00 = sound id, +0x04 = play serial,
// +0x14 = flags. Flag 0x400 puts a sound on the engine's music volume: zMusic's tracks and the menu music, but also
// every streamed-sound asset of a level bank (the engine's lookup fn_800D98C8 marks those: dialog lines and the
// in-engine cutscene soundtracks).
constexpr uint32_t kSoundRecords = 0x80237E18u;
constexpr uint32_t kSoundRecordStride = 0x48u;
constexpr uint32_t kSoundRecordCount = 64u;
constexpr uint32_t kSoundRecordSerialOffset = 0x04u;
constexpr uint32_t kSoundRecordFlagsOffset = 0x14u;
constexpr uint16_t kSoundFlagMusic = 0x400u;
// Sound banks, one per loaded HIP with sounds, searched last to first like fn_800D98C8: count at 0x8027101C
// (r13-0x7264), 0x108-byte entries from 0x8024C870, +0x104 = header {+0 sounds, +8 streamed sounds, entries of 0x64
// bytes from +0xC with the sound id at +0x60}. Bank 0 = boot.HIP, 1 = mnu4.HIP (menu sounds, loaded all game),
// 2 and up = the level's.
constexpr uint32_t kSoundBankCount = 0x8027101Cu;
constexpr uint32_t kSoundBanks = 0x8024C870u;
constexpr uint32_t kSoundBankStride = 0x108u;
constexpr uint32_t kSoundBankMax = 12u;
constexpr uint32_t kSoundBankHeaderOffset = 0x104u;
constexpr uint32_t kSoundEntryStride = 0x64u;
constexpr uint32_t kSoundEntryIdOffset = 0x60u;
constexpr int kMenuSoundBank = 1;
constexpr uint32_t kStreamSlots = 0x8024C258u;
constexpr uint32_t kStreamSlotStride = 0x104u;
constexpr uint32_t kStreamSlotCount = 6u;
constexpr uint32_t kSampleSlots = 0x8024BB18u;
constexpr uint32_t kSampleSlotStride = 0x20u;
constexpr uint32_t kSampleSlotCount = 58u;
// MIX channels: one 0x60-byte entry per AX voice index (AXVPB +0x18); +0x04 mode flags, +0x1C fader (0.1 dB).
constexpr uint32_t kMixChannels = 0x8024D680u;
constexpr uint32_t kMixChannelStride = 0x60u;
constexpr uint32_t kMixChannelModeOffset = 0x04u;
constexpr uint32_t kMixChannelFaderOffset = 0x1Cu;
constexpr uint32_t kMixModeUpdateMix = 0x40000000u;
constexpr uint32_t kAxVoiceIndexOffset = 0x18u;
// MIXUpdateSettings clamps input + fader to [-904, +60]; this takes any fader to silence.
constexpr int32_t kMixSilentOffset = -904 - 60;

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_externalMediaPlaying{false};
std::atomic<bool> g_mediaControlAvailable{false};
std::atomic<bool> g_mediaControlInitializationComplete{false};
std::atomic<float> g_musicVolume{1.0f};
std::atomic<float> g_soundEffectsVolume{1.0f};
std::atomic<float> g_uiVolume{1.0f};
std::atomic<float> g_voicesVolume{1.0f};
std::once_flag g_monitorStart;

float ClampVolume(float volume) noexcept {
    if (volume <= 1.0f) {
        return volume < 0.0f ? 0.0f : volume;
    }
    return 1.0f;
}

enum class SoundCategory {
    Music,
    SoundEffects,
    Ui,
    Voices,
};

// Category of each record's current sound, keyed by its play serial (touched only from the guest thread).
std::array<uint32_t, kSoundRecordCount> g_recordSerial = [] {
    std::array<uint32_t, kSoundRecordCount> serials{};
    serials.fill(~0u);
    return serials;
}();
std::array<SoundCategory, kSoundRecordCount> g_recordCategory{};

// The sound record playing on an AX voice, or -1 when the voice is not one of the engine's (Bink movie audio).
int FindSoundRecord(uint32_t voice) noexcept {
    for (uint32_t i = 0; i < kStreamSlotCount; ++i) {
        if (Memory::Read32(kStreamSlots + i * kStreamSlotStride) == voice) {
            return static_cast<int>(i);
        }
    }
    for (uint32_t i = 0; i < kSampleSlotCount; ++i) {
        if (Memory::Read32(kSampleSlots + i * kSampleSlotStride) == voice) {
            return static_cast<int>(kStreamSlotCount + i);
        }
    }
    return -1;
}

// The bank a sound id is found in by the engine's own lookup order, or -1.
int FindSoundBank(uint32_t id) noexcept {
    const uint32_t count = std::min(Memory::Read32(kSoundBankCount), kSoundBankMax);
    for (uint32_t bank = count; bank-- > 0;) {
        const uint32_t header = Memory::Read32(kSoundBanks + bank * kSoundBankStride + kSoundBankHeaderOffset);
        if (header == 0) {
            continue;
        }
        const uint32_t sounds = Memory::Read32(header) + Memory::Read32(header + 8);
        for (uint32_t i = 0; i < sounds; ++i) {
            if (Memory::Read32(header + 0xC + i * kSoundEntryStride + kSoundEntryIdOffset) == id) {
                return static_cast<int>(bank);
            }
        }
    }
    return -1;
}

SoundCategory CategoryForSound(uint32_t id, uint16_t flags) noexcept {
    const int bank = FindSoundBank(id);
    if (flags & kSoundFlagMusic) {
        return bank > kMenuSoundBank ? SoundCategory::Voices : SoundCategory::Music;
    }
    return bank == kMenuSoundBank ? SoundCategory::Ui : SoundCategory::SoundEffects;
}

SoundCategory CategoryForRecord(int index) noexcept {
    const uint32_t record = kSoundRecords + static_cast<uint32_t>(index) * kSoundRecordStride;
    const uint32_t serial = Memory::Read32(record + kSoundRecordSerialOffset);
    if (g_recordSerial[index] != serial) {
        g_recordSerial[index] = serial;
        g_recordCategory[index] =
            CategoryForSound(Memory::Read32(record), Memory::Read16(record + kSoundRecordFlagsOffset));
    }
    return g_recordCategory[index];
}

float CategoryVolume(SoundCategory category) noexcept {
    switch (category) {
    case SoundCategory::Music:
        return g_musicVolume.load(std::memory_order_acquire);
    case SoundCategory::SoundEffects:
        return g_soundEffectsVolume.load(std::memory_order_acquire);
    case SoundCategory::Ui:
        return g_uiVolume.load(std::memory_order_acquire);
    case SoundCategory::Voices:
        return g_voicesVolume.load(std::memory_order_acquire);
    }
    return 1.0f;
}

// A linear volume as a MIX fader offset in 0.1 dB.
int32_t FaderOffset(float volume) noexcept {
    if (volume >= 1.0f) {
        return 0;
    }
    if (volume <= 0.0f) {
        return kMixSilentOffset;
    }
    return std::max(kMixSilentOffset, static_cast<int32_t>(std::lround(200.0f * std::log10(volume))));
}

bool ShouldAttenuate() noexcept {
    return g_enabled.load(std::memory_order_acquire) &&
           g_externalMediaPlaying.load(std::memory_order_acquire);
}

#if defined(_WIN32)
void MonitorWindowsMediaSessions() noexcept {
    using namespace winrt::Windows::Media::Control;
    using namespace std::chrono_literals;

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        g_mediaControlAvailable.store(true, std::memory_order_release);
        g_mediaControlInitializationComplete.store(true, std::memory_order_release);

        for (;;) {
            bool playing = false;
            try {
                for (const auto& session : manager.GetSessions()) {
                    const auto info = session.GetPlaybackInfo();
                    if (info.PlaybackStatus() ==
                        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                        playing = true;
                        break;
                    }
                }
            } catch (const winrt::hresult_error&) {
                // Sessions can disappear between enumeration and the playback
                // query. Treat that sample as inactive and retry shortly.
            }
            g_externalMediaPlaying.store(playing, std::memory_order_release);
            std::this_thread::sleep_for(250ms);
        }
    } catch (const winrt::hresult_error& error) {
        g_mediaControlAvailable.store(false, std::memory_order_release);
        g_externalMediaPlaying.store(false, std::memory_order_release);
        g_mediaControlInitializationComplete.store(true, std::memory_order_release);
        std::wcerr << L"[audio] Windows media-session monitoring unavailable: "
                   << error.message().c_str() << std::endl;
    }
}
#elif defined(__linux__)

// For linux - watches MPRIS players on the D-Bus session bus and drives the
// same g_externalMediaPlaying / g_mediaControl* atomics.
//
// libdbus-1 is loaded with dlopen rather than linked so a machine with no
// session bus (headless, D-Bus absent, some containers) can still run the game,
// here, the monitor reports "unavailable" like the Windows does
// when the media-session API is missing.

extern "C" {

struct MprisDBusConnection;
struct MprisDBusMessage;

// Mirrors DBusError from dbus-errors.h (frozen public layout).
struct MprisDBusError {
    const char* name;
    const char* message;
    unsigned int dummy1 : 1;
    unsigned int dummy2 : 1;
    unsigned int dummy3 : 1;
    unsigned int dummy4 : 1;
    unsigned int dummy5 : 1;
    void* padding1;
};

// Mirrors DBusMessageIter from dbus-message.h
struct MprisDBusIter {
    void* dummy1;
    void* dummy2;
    uint32_t dummy3;
    int dummy4;
    int dummy5;
    int dummy6;
    int dummy7;
    int dummy8;
    int dummy9;
    int dummy10;
    int dummy11;
    int pad1;
    int pad2;
    void* pad3;
};

} // extern "C"

constexpr int kMprisBusSession = 0;   // DBUS_BUS_SESSION
constexpr int kMprisTypeInvalid = 0;  // DBUS_TYPE_INVALID
constexpr int kMprisTypeString = 's'; // DBUS_TYPE_STRING
constexpr int kMprisTypeArray = 'a';  // DBUS_TYPE_ARRAY
constexpr int kMprisTypeVariant = 'v';// DBUS_TYPE_VARIANT
// Short blocking timeout so a wedged player can never stall the poll thread for
// the full libdbus default (25s).
constexpr int kMprisSendTimeoutMs = 500;
constexpr char kMprisNamePrefix[] = "org.mpris.MediaPlayer2.";

struct MprisDBus {
    void* handle = nullptr;

    MprisDBusConnection* (*bus_get_private)(int, MprisDBusError*) = nullptr;
    void (*connection_close)(MprisDBusConnection*) = nullptr;
    void (*connection_unref)(MprisDBusConnection*) = nullptr;
    void (*connection_set_exit_on_disconnect)(MprisDBusConnection*, unsigned int) = nullptr;
    void (*error_init)(MprisDBusError*) = nullptr;
    void (*error_free)(MprisDBusError*) = nullptr;
    MprisDBusMessage* (*message_new_method_call)(const char*, const char*, const char*,
                                                const char*) = nullptr;
    unsigned int (*message_append_args)(MprisDBusMessage*, int, ...) = nullptr;
    MprisDBusMessage* (*send_with_reply_and_block)(MprisDBusConnection*, MprisDBusMessage*, int,
                                                  MprisDBusError*) = nullptr;
    void (*message_unref)(MprisDBusMessage*) = nullptr;
    unsigned int (*message_iter_init)(MprisDBusMessage*, MprisDBusIter*) = nullptr;
    unsigned int (*message_iter_next)(MprisDBusIter*) = nullptr;
    void (*message_iter_recurse)(MprisDBusIter*, MprisDBusIter*) = nullptr;
    int (*message_iter_get_arg_type)(MprisDBusIter*) = nullptr;
    void (*message_iter_get_basic)(MprisDBusIter*, void*) = nullptr;

    template <typename Fn>
    bool Bind(Fn& fn, const char* symbol) noexcept {
        fn = reinterpret_cast<Fn>(dlsym(handle, symbol));
        return fn != nullptr;
    }

    bool Load() noexcept {
        handle = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            return false;
        }
        const bool ok =
            Bind(bus_get_private, "dbus_bus_get_private") &&
            Bind(connection_close, "dbus_connection_close") &&
            Bind(connection_unref, "dbus_connection_unref") &&
            Bind(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect") &&
            Bind(error_init, "dbus_error_init") &&
            Bind(error_free, "dbus_error_free") &&
            Bind(message_new_method_call, "dbus_message_new_method_call") &&
            Bind(message_append_args, "dbus_message_append_args") &&
            Bind(send_with_reply_and_block, "dbus_connection_send_with_reply_and_block") &&
            Bind(message_unref, "dbus_message_unref") &&
            Bind(message_iter_init, "dbus_message_iter_init") &&
            Bind(message_iter_next, "dbus_message_iter_next") &&
            Bind(message_iter_recurse, "dbus_message_iter_recurse") &&
            Bind(message_iter_get_arg_type, "dbus_message_iter_get_arg_type") &&
            Bind(message_iter_get_basic, "dbus_message_iter_get_basic");
        if (!ok) {
            dlclose(handle);
            handle = nullptr;
            return false;
        }
        return true;
    }

    // org.freedesktop.DBus.ListNames
    bool ListNames(MprisDBusConnection* conn, std::vector<std::string>& out) noexcept {
        MprisDBusMessage* msg = message_new_method_call(
            "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
        if (msg == nullptr) {
            return false;
        }
        MprisDBusError err;
        error_init(&err);
        MprisDBusMessage* reply = send_with_reply_and_block(conn, msg, kMprisSendTimeoutMs, &err);
        message_unref(msg);
        if (reply == nullptr) {
            error_free(&err);
            return false;
        }
        error_free(&err);

        MprisDBusIter iter;
        MprisDBusIter sub;
        if (message_iter_init(reply, &iter) &&
            message_iter_get_arg_type(&iter) == kMprisTypeArray) {
            message_iter_recurse(&iter, &sub);
            while (message_iter_get_arg_type(&sub) == kMprisTypeString) {
                const char* name = nullptr;
                message_iter_get_basic(&sub, &name);
                if (name != nullptr) {
                    out.emplace_back(name);
                }
                message_iter_next(&sub);
            }
        }
        message_unref(reply);
        return true;
    }

    // org.freedesktop.DBus.Properties.Get(Player, "PlaybackStatus") for one
    // MPRIS player - the reply is a variant wrapping a string (eg: "Playing").
    bool PlaybackStatus(MprisDBusConnection* conn, const char* busName, std::string& out) noexcept {
        MprisDBusMessage* msg = message_new_method_call(
            busName, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "Get");
        if (msg == nullptr) {
            return false;
        }
        const char* iface = "org.mpris.MediaPlayer2.Player";
        const char* prop = "PlaybackStatus";
        if (!message_append_args(msg, kMprisTypeString, &iface, kMprisTypeString, &prop,
                                 kMprisTypeInvalid)) {
            message_unref(msg);
            return false;
        }
        MprisDBusError err;
        error_init(&err);
        MprisDBusMessage* reply = send_with_reply_and_block(conn, msg, kMprisSendTimeoutMs, &err);
        message_unref(msg);
        if (reply == nullptr) {
            error_free(&err);
            return false;
        }
        error_free(&err);

        bool haveValue = false;
        MprisDBusIter iter;
        MprisDBusIter variant;
        if (message_iter_init(reply, &iter) &&
            message_iter_get_arg_type(&iter) == kMprisTypeVariant) {
            message_iter_recurse(&iter, &variant);
            if (message_iter_get_arg_type(&variant) == kMprisTypeString) {
                const char* value = nullptr;
                message_iter_get_basic(&variant, &value);
                if (value != nullptr) {
                    out.assign(value);
                    haveValue = true;
                }
            }
        }
        message_unref(reply);
        return haveValue;
    }
};

void MonitorLinuxMprisSessions() noexcept {
    using namespace std::chrono_literals;

    MprisDBus dbus;
    if (!dbus.Load()) {
        g_mediaControlAvailable.store(false, std::memory_order_release);
        g_externalMediaPlaying.store(false, std::memory_order_release);
        g_mediaControlInitializationComplete.store(true, std::memory_order_release);
        std::cerr << "[audio] MPRIS media monitoring unavailable: libdbus-1 could not be loaded"
                  << std::endl;
        return;
    }

    MprisDBusError err;
    dbus.error_init(&err);
    MprisDBusConnection* conn = dbus.bus_get_private(kMprisBusSession, &err);
    if (conn == nullptr) {
        g_mediaControlAvailable.store(false, std::memory_order_release);
        g_externalMediaPlaying.store(false, std::memory_order_release);
        g_mediaControlInitializationComplete.store(true, std::memory_order_release);
        std::cerr << "[audio] MPRIS media monitoring unavailable: "
                  << (err.message != nullptr ? err.message : "no D-Bus session bus") << std::endl;
        dbus.error_free(&err);
        return;
    }
    dbus.error_free(&err);
    // A private bus connection defaults to terminating the process when the bus drops.
    dbus.connection_set_exit_on_disconnect(conn, 0u);

    for (bool firstPoll = true;; firstPoll = false) {
        bool playing = false;
        std::vector<std::string> names;
        const bool queried = dbus.ListNames(conn, names);
        if (queried) {
            for (const auto& name : names) {
                if (name.compare(0, sizeof(kMprisNamePrefix) - 1, kMprisNamePrefix) != 0) {
                    continue;
                }
                std::string status;
                if (dbus.PlaybackStatus(conn, name.c_str(), status) && status == "Playing") {
                    playing = true;
                    break;
                }
            }
        }
        g_externalMediaPlaying.store(playing, std::memory_order_release);
        g_mediaControlAvailable.store(queried, std::memory_order_release);
        if (firstPoll) {
            g_mediaControlInitializationComplete.store(true, std::memory_order_release);
            if (!queried) {
                std::cerr << "[audio] MPRIS media monitoring unavailable: session bus query failed"
                          << std::endl;
            }
        }
        std::this_thread::sleep_for(250ms);
    }
}
#endif

#if defined(__APPLE__)
void MonitorMacOSAudio() noexcept {
    using namespace std::chrono_literals;
    for (;;) {
        const auto status = QueryMacOSExternalAudio();
        g_externalMediaPlaying.store(status.playing, std::memory_order_release);
        g_mediaControlAvailable.store(status.available, std::memory_order_release);
        g_mediaControlInitializationComplete.store(true, std::memory_order_release);
        std::this_thread::sleep_for(250ms);
    }
}
#endif

void StartMonitor() noexcept {
#if defined(_WIN32)
    // The process owns this monitor for its remaining lifetime. Keeping it
    // detached avoids shutdown ordering between WinRT and static audio state.
    std::thread(MonitorWindowsMediaSessions).detach();
#elif defined(__linux__)
    // Detached so there is no shutdown ordering to manage against static audio state.
    std::thread(MonitorLinuxMprisSessions).detach();
#elif defined(__APPLE__)
    std::thread(MonitorMacOSAudio).detach();
#else
    g_mediaControlAvailable.store(false, std::memory_order_release);
    g_mediaControlInitializationComplete.store(true, std::memory_order_release);
#endif
}

} // namespace

void SetEnabled(bool enabled) noexcept {
    if (enabled) {
        std::call_once(g_monitorStart, StartMonitor);
    }
    g_enabled.store(enabled, std::memory_order_release);
}

bool IsExternalMediaPlaying() noexcept {
    return g_externalMediaPlaying.load(std::memory_order_acquire);
}

bool IsMediaControlAvailable() noexcept {
    return g_mediaControlAvailable.load(std::memory_order_acquire);
}

bool IsMediaControlInitializationComplete() noexcept {
    return g_mediaControlInitializationComplete.load(std::memory_order_acquire);
}

void SetMusicVolume(float volume) noexcept {
    g_musicVolume.store(ClampVolume(volume), std::memory_order_release);
}

void SetSoundEffectsVolume(float volume) noexcept {
    g_soundEffectsVolume.store(ClampVolume(volume), std::memory_order_release);
}

void SetUiVolume(float volume) noexcept {
    g_uiVolume.store(ClampVolume(volume), std::memory_order_release);
}

void SetVoicesVolume(float volume) noexcept {
    g_voicesVolume.store(ClampVolume(volume), std::memory_order_release);
}

void AdjustMixFader(uint32_t voice, int32_t delta) {
    // MIXAdjustFader: fader += delta, then flag the channel for MIXUpdateSettings. The engine asks for
    // delta = wanted - MIXGetFader(voice) every frame, so adding the category offset here gives wanted + offset
    // (the offset from the frame before cancels out) and a slider change reaches playing sounds on the next frame.
    int32_t offset = 0;
    const int record = FindSoundRecord(voice);
    if (record >= 0) {
        const SoundCategory category = CategoryForRecord(record);
        offset = (category == SoundCategory::Music && ShouldAttenuate()) ? kMixSilentOffset
                                                                         : FaderOffset(CategoryVolume(category));
    }
    const uint32_t channel = kMixChannels + Memory::Read32(voice + kAxVoiceIndexOffset) * kMixChannelStride;
    const int32_t fader = static_cast<int32_t>(Memory::Read32(channel + kMixChannelFaderOffset));
    Memory::Write32(channel + kMixChannelFaderOffset, static_cast<uint32_t>(fader + delta + offset));
    Memory::Write32(channel + kMixChannelModeOffset,
                    Memory::Read32(channel + kMixChannelModeOffset) | kMixModeUpdateMix);
}

} // namespace MusicAttenuation
