#include "host_context.h"
#include <aurora/window_owner_sdl.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace owner = aurora::window_owner;
using namespace std::chrono_literals;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Predicate> static void Wait(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        Require(std::chrono::steady_clock::now() < deadline, "Timed out waiting for fixture");
        std::this_thread::yield();
    }
}

struct FiberClose {
    HostContext::Handle scheduler{};
    std::exception_ptr error;
};

static void FiberEntry(void* argument) {
    auto& state = *static_cast<FiberClose*>(argument);
    try { throw owner::CloseRequested(); }
    catch (...) { state.error = std::current_exception(); }
    HostContext::Switch(state.scheduler);
    std::terminate();
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "lifecycle";
    const std::string expected = mode == "event-overflow"
        ? "Window owner event queue exceeded 2048 events; input cannot be dropped safely"
        : mode == "async-error" ? "deliberate owner failure" : "";
    try {
        owner::run([&] {
            Require(owner::is_guest() && !owner::is_owner(), "Guest must not own SDL");
            owner::call([] {
                Require(owner::is_owner() && !owner::is_guest(), "SDL must stay on the owner");
            });
            const auto props = SDL_CreateProperties();
            Require(props != 0, "Create window properties");
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_HIDDEN);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 640);
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 480);
            auto* window = SDL_CreateWindowWithProperties(props);
            SDL_DestroyProperties(props);
            Require(window != nullptr, "Create hidden fixture window");

            if (mode == "idle-cpu") {
                const auto cpu = [] {
                    FILETIME creation{}, exit{}, kernel{}, user{};
                    Require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user),
                            "Read owner thread CPU time");
                    const auto ticks = [](FILETIME time) {
                        return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
                    };
                    return double(ticks(kernel) + ticks(user)) / 1e7;
                };
                const auto before = owner::call(cpu);
                std::this_thread::sleep_for(500ms);
                Require(owner::call(cpu) - before < 0.05, "Idle owner must block, not burn a core on timed waits");
            } else if (mode == "deferred-close") {
                []() noexcept { owner::request_close(); }();
                Require(owner::consume_close_request(), "Transfer quit past the noexcept UI callback");
                Require(!owner::consume_close_request(), "Consume a quit request only once");
            } else if (mode == "motion") {
                owner::call([&] {
                    owner::refresh_window_state();
                });
                SDL_Event event{};
                while (SDL_PollEvent(&event)) {}
                owner::call([&] {
                    for (int i = 0; i < 5000; ++i) {
                        SDL_Event motion{};
                        motion.type = SDL_EVENT_MOUSE_MOTION;
                        motion.motion.which = 1;
                        motion.motion.windowID = SDL_GetWindowID(window);
                        motion.motion.x = float(i);
                        motion.motion.xrel = 1;
                        Require(SDL_PushEvent(&motion), "Push mouse motion");
                        motion.motion.which = 2;
                        Require(SDL_PushEvent(&motion), "Push second mouse motion");
                        SDL_Event axis{};
                        axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
                        axis.gaxis.which = 3;
                        axis.gaxis.axis = i % 2;
                        axis.gaxis.value = static_cast<Sint16>(i);
                        Require(SDL_PushEvent(&axis), "Push gamepad axis");
                        axis.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
                        axis.jaxis.which = 4;
                        axis.jaxis.axis = i % 2;
                        axis.jaxis.value = static_cast<Sint16>(i);
                        Require(SDL_PushEvent(&axis), "Push joystick axis");
                        SDL_Event complete{};
                        complete.type = SDL_EVENT_GAMEPAD_UPDATE_COMPLETE;
                        complete.gdevice.which = 3;
                        Require(SDL_PushEvent(&complete), "Push gamepad update boundary");
                        complete.type = SDL_EVENT_JOYSTICK_UPDATE_COMPLETE;
                        complete.jdevice.which = 4;
                        Require(SDL_PushEvent(&complete), "Push joystick update boundary");
                    }
                    SDL_Event key{};
                    key.type = SDL_EVENT_KEY_DOWN;
                    key.key.scancode = SDL_SCANCODE_A;
                    Require(SDL_PushEvent(&key), "Push key barrier");
                    SDL_Event motion{};
                    motion.type = SDL_EVENT_MOUSE_MOTION;
                    motion.motion.which = 1;
                    motion.motion.windowID = SDL_GetWindowID(window);
                    motion.motion.x = 7000;
                    motion.motion.xrel = 2;
                    Require(SDL_PushEvent(&motion), "Push motion after key");
                    SDL_Event button{};
                    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    button.button.which = 1;
                    button.button.windowID = SDL_GetWindowID(window);
                    button.button.button = SDL_BUTTON_LEFT;
                    Require(SDL_PushEvent(&button), "Push mouse button barrier");
                    motion.motion.x = 8000;
                    motion.motion.xrel = 3;
                    motion.motion.state = SDL_BUTTON_LMASK;
                    motion.common.timestamp = 0;
                    Require(SDL_PushEvent(&motion), "Push motion after button");
                    owner::refresh_window_state();
                });
                int mouseCount = 0, gamepadCount = 0, joystickCount = 0;
                int gamepadComplete = 0, joystickComplete = 0;
                bool sawKey = false, sawButton = false;
                Uint64 lastTimestamp = 0;
                while (SDL_PollEvent(&event)) {
                    Require(event.common.timestamp >= lastTimestamp, "Keep retained motion in chronological order");
                    lastTimestamp = event.common.timestamp;
                    if (event.type == SDL_EVENT_KEY_DOWN) {
                        Require(mouseCount == 2, "Keep motion before key transition");
                        sawKey = true;
                    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                        Require(mouseCount == 3 && sawKey, "Keep button after preceding motion and key");
                        sawButton = true;
                    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                        if (!sawKey) {
                            Require(event.motion.x == 4999 && event.motion.xrel == 5000,
                                    "Keep latest position and accumulated relative motion per mouse");
                        } else if (!sawButton) {
                            Require(event.motion.x == 7000 && event.motion.xrel == 2,
                                    "Do not merge across a key transition");
                        } else {
                            Require(event.motion.x == 8000 && event.motion.xrel == 3,
                                    "Do not merge across a button transition");
                        }
                        ++mouseCount;
                    } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                        Require(event.gaxis.value == (event.gaxis.axis == 0 ? 4998 : 4999),
                                "Keep latest gamepad value per axis");
                        ++gamepadCount;
                    } else if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
                        Require(event.jaxis.value == (event.jaxis.axis == 0 ? 4998 : 4999),
                                "Keep latest joystick value per axis");
                        ++joystickCount;
                    } else if (event.type == SDL_EVENT_GAMEPAD_UPDATE_COMPLETE) {
                        Require(event.gdevice.which == 3, "Preserve the gamepad update device");
                        ++gamepadComplete;
                    } else if (event.type == SDL_EVENT_JOYSTICK_UPDATE_COMPLETE) {
                        Require(event.jdevice.which == 4, "Preserve the joystick update device");
                        ++joystickComplete;
                    }
                }
                Require(mouseCount == 4 && gamepadCount == 2 && joystickCount == 2 && sawKey && sawButton,
                        "Coalesce a stalled frame without losing independent devices or discrete input");
                Require(gamepadComplete == 1 && joystickComplete == 1, "Retain the latest device update boundary");
            } else if (mode == "snapshot") {
                std::atomic<bool> published{false}, release{false};
                owner::submit([&] {
                    if (!SDL_SetWindowSize(window, 800, 600))
                        throw std::runtime_error(SDL_GetError());
                    owner::refresh_window_state();
                    published = true;
                    Wait([&] { return release.load(); });
                });
                try {
                    Wait([&] { return published.load(); });
                    int width = 0, height = 0;
                    SDL_GetWindowSize(window, &width, &height);
                    Require(width == 800 && height == 600,
                            "Publish resize before the owner operation/pump returns");
                    Require(owner::native_state().pixelsWidth == 800, "Publish pixel dimensions");
                    release = true;
                } catch (...) {
                    release = true;
                    owner::call([] {});
                    throw;
                }
                owner::call([] {});
            } else if (mode == "command-queue") {
                std::atomic<bool> entered{false}, release{false};
                int lastCommand = -1;
                owner::submit([&] {
                    entered = true;
                    Wait([&] { return release.load(); });
                });
                try {
                    Wait([&] { return entered.load(); });
                    for (int i = 0; i < 1000; ++i)
                        owner::submit([&, i] { lastCommand = i; }, 123);
                    for (int i = 0; i < 255; ++i) owner::submit([] {});
                    bool caught = false;
                    try { owner::submit([] {}); }
                    catch (const std::runtime_error& error) {
                        caught = std::string(error.what()) ==
                            "Window owner command queue exceeded 256 pending operations";
                    }
                    Require(caught, "Fail explicitly at the command queue bound");
                    release = true;
                } catch (...) {
                    release = true;
                    throw;
                }
                // Allow the full batch to drain before submitting a barrier.
                Wait([] {
                    try { owner::call([] {}); return true; }
                    catch (const std::runtime_error& error) {
                        if (std::string(error.what()) !=
                            "Window owner command queue exceeded 256 pending operations") throw;
                        return false;
                    }
                });
                Require(lastCommand == 999, "Coalesce commands to the newest value");
            } else if (mode == "event-overflow" || mode == "async-error") {
                if (mode == "async-error") {
                    owner::submit([] { throw std::runtime_error("deliberate owner failure"); });
                    try {
                        Wait([] { owner::check(); return false; });
                    } catch (...) {
                        owner::begin_shutdown();
                        SDL_DestroyWindow(window);
                        throw;
                    }
                }
                owner::call([&] {
                    for (int i = 0; i < 2049; ++i) {
                        SDL_Event event{};
                        event.type = SDL_EVENT_KEY_DOWN;
                        event.key.scancode = SDL_SCANCODE_A;
                        Require(SDL_PushEvent(&event), "Push overflow fixture event");
                    }
                    owner::refresh_window_state();
                });
                // Pending errors must not prevent the owner from completing teardown commands.
                owner::begin_shutdown();
                SDL_DestroyWindow(window);
                return 0;
            } else if (mode == "fiber-close") {
                FiberClose state;
                Require(HostContext::InitializeScheduler(&state.scheduler), "Create scheduler");
                auto fiber = HostContext::Create(64 * 1024, FiberEntry, &state);
                Require(fiber != nullptr, "Create guest fiber");
                HostContext::Switch(fiber);
                Require(!HostContext::IsCurrent(fiber), "Unwind close on scheduler stack");
                HostContext::Destroy(fiber);
                HostContext::ShutdownScheduler(state.scheduler);
                bool caught = false;
                try { std::rethrow_exception(state.error); }
                catch (const owner::CloseRequested&) { caught = true; }
                Require(caught, "Transfer the close request out of the fiber");
            } else {
                bool caught = false;
                try { owner::call([] { throw std::runtime_error("synchronous fixture failure"); }); }
                catch (const std::runtime_error&) { caught = true; }
                Require(caught, "Return synchronous failures to the guest");
                std::string payload = "original text";
                owner::call([&] {
                    SDL_Event event{};
                    event.type = SDL_EVENT_TEXT_INPUT;
                    event.text.windowID = SDL_GetWindowID(window);
                    event.text.text = payload.c_str();
                    Require(SDL_PushEvent(&event), "Push text event");
                    owner::refresh_window_state();
                    payload = "changed";
                });
                bool seen = false;
                SDL_Event event{};
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_EVENT_TEXT_INPUT) {
                        Require(std::string(event.text.text) == "original text", "Own queued text lifetime");
                        seen = true;
                    }
                }
                Require(seen, "Deliver text event");
                owner::call([] {});
            }
            SDL_DestroyWindow(window);
            Require(owner::native_state().window == nullptr, "Invalidate destroyed window snapshot");
            return 0;
        });
        Require(expected.empty(), "Expected an owner error");
        Require(!owner::enabled() && !SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS),
                "Join the guest before SDL teardown");
        std::cout << "PASS window owner " << mode << '\n';
        return 0;
    } catch (const std::exception& error) {
        if (!expected.empty() && error.what() == expected && !owner::enabled() &&
            !SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            std::cout << "PASS window owner " << mode << ": " << error.what() << '\n';
            return 0;
        }
        std::cerr << "FAIL window owner " << mode << ": " << error.what() << '\n';
        return 1;
    }
}
