#include "host_context.h"
#include <aurora/window_owner_sdl.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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

            if (mode == "snapshot") {
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
                        event.type = SDL_EVENT_USER;
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
