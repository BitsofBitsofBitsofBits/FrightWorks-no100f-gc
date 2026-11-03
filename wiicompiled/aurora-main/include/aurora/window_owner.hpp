#pragma once

#include <SDL3/SDL.h>
#include <functional>
#include <future>
#include <type_traits>
#include <vector>
#include <array>

namespace aurora::window_owner {
// SDL stays on the native main thread; guest CPU/fibers stay on one worker.
// Neither snapshots nor asynchronous window commands wait for a Windows modal loop.
class CloseRequested final : public std::exception {
public:
    const char* what() const noexcept override { return "Window close requested"; }
};
struct NativeState {
    SDL_Window* window = nullptr;
    SDL_Window* keyboardFocus = nullptr;
    SDL_Window* mouseFocus = nullptr;
    SDL_Cursor* cursor = nullptr;
    SDL_WindowID id = 0;
    // Property ID and native handles remain valid only until the owned window is destroyed.
    SDL_PropertiesID properties = 0;
    SDL_WindowFlags flags = 0;
    int width = 0, height = 0, pixelsWidth = 0, pixelsHeight = 0, x = 0, y = 0;
    float scale = 1, mouseX = 0, mouseY = 0, globalX = 0, globalY = 0;
    SDL_MouseButtonFlags mouseButtons = 0, globalButtons = 0;
    bool relativeMouse = false;
    std::array<bool, SDL_SCANCODE_COUNT> keyboard{};
    SDL_Keymod modifiers = SDL_KMOD_NONE;
};
struct DisplayState {
    SDL_DisplayID id = 0;
    SDL_Rect bounds{}, usableBounds{};
    bool usableBoundsValid = false;
    float scale = 1;
};
bool enabled() noexcept;
bool is_owner() noexcept;
bool is_guest() noexcept;
int run(std::function<int()> game);
// Queue admission is not SDL completion. Async failures are rethrown at guest event/frame boundaries.
void submit(std::function<void()> operation, unsigned replaceKey = 0);
void check();
void request_close() noexcept;
bool consume_close_request() noexcept;
void set_window(SDL_Window* window);
void refresh_window_state();
// Snapshots never wait for SDL or throw a pending owner error on a render/presenter thread.
NativeState native_state();
SDL_MouseButtonFlags relative_mouse(float* x, float* y);
std::vector<DisplayState> displays();
bool poll_event(SDL_Event* event);
void begin_shutdown();
void report_failure(std::exception_ptr error);

template <class F> auto call(F operation) -> std::invoke_result_t<F> {
    using Result = std::invoke_result_t<F>;
    if (!enabled() || is_owner()) return operation();
    auto result = std::make_shared<std::promise<Result>>();
    auto future = result->get_future();
    submit([result, operation = std::move(operation)]() mutable {
        try {
            if constexpr (std::is_void_v<Result>) {
                operation();
                result->set_value();
            } else {
                result->set_value(operation());
            }
        } catch (...) { result->set_exception(std::current_exception()); }
    });
    return future.get();
}
}
