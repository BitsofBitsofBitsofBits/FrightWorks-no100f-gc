#pragma once

#include <aurora/window_owner.hpp>
#include <string>
#include <stdexcept>

namespace aurora::window_owner::sdl {
inline bool snapshot_window(SDL_Window* window) {
    return enabled() && !is_owner() && window == native_state().window;
}
inline bool GetWindowSize(SDL_Window* w, int* x, int* y) {
    if (!snapshot_window(w)) return call([=] { return SDL_GetWindowSize(w, x, y); });
    auto s = native_state(); if (x) *x = s.width; if (y) *y = s.height; return true;
}
inline bool GetWindowSizeInPixels(SDL_Window* w, int* x, int* y) {
    if (!snapshot_window(w)) return call([=] { return SDL_GetWindowSizeInPixels(w, x, y); });
    auto s = native_state(); if (x) *x = s.pixelsWidth; if (y) *y = s.pixelsHeight; return true;
}
inline bool GetWindowPosition(SDL_Window* w, int* x, int* y) {
    if (!snapshot_window(w)) return call([=] { return SDL_GetWindowPosition(w, x, y); });
    auto s = native_state(); if (x) *x = s.x; if (y) *y = s.y; return true;
}
inline SDL_WindowFlags GetWindowFlags(SDL_Window* w) {
    if (!snapshot_window(w)) return call([=] { return SDL_GetWindowFlags(w); });
    return native_state().flags;
}
inline float GetWindowDisplayScale(SDL_Window* w) {
    if (!snapshot_window(w)) return call([=] { return SDL_GetWindowDisplayScale(w); });
    return native_state().scale;
}
inline SDL_WindowID GetWindowID(SDL_Window* w) {
    if (!snapshot_window(w)) return call([=] { return SDL_GetWindowID(w); });
    return native_state().id;
}
inline SDL_Window* GetWindowFromID(SDL_WindowID id) {
    if (!enabled() || is_owner()) return SDL_GetWindowFromID(id);
    auto s = native_state();
    if (s.id == id) return s.window;
    return call([=] { return SDL_GetWindowFromID(id); });
}
inline SDL_Window* GetKeyboardFocus() {
    return enabled() && !is_owner() ? native_state().keyboardFocus : SDL_GetKeyboardFocus();
}
inline SDL_Window* GetMouseFocus() {
    return enabled() && !is_owner() ? native_state().mouseFocus : SDL_GetMouseFocus();
}
inline SDL_MouseButtonFlags GetMouseState(float* x, float* y) {
    if (!enabled() || is_owner()) return SDL_GetMouseState(x, y);
    auto s = native_state(); if (x) *x = s.mouseX; if (y) *y = s.mouseY; return s.mouseButtons;
}
inline SDL_MouseButtonFlags GetGlobalMouseState(float* x, float* y) {
    if (!enabled() || is_owner()) return SDL_GetGlobalMouseState(x, y);
    auto s = native_state(); if (x) *x = s.globalX; if (y) *y = s.globalY; return s.globalButtons;
}
inline SDL_MouseButtonFlags GetRelativeMouseState(float* x, float* y) {
    return enabled() && !is_owner() ? relative_mouse(x, y) : SDL_GetRelativeMouseState(x, y);
}
inline bool GetWindowRelativeMouseMode(SDL_Window* w) {
    if (!snapshot_window(w)) return call([=] { return SDL_GetWindowRelativeMouseMode(w); });
    return native_state().relativeMouse;
}
inline bool PollEvent(SDL_Event* event) {
    if (!enabled() || is_owner()) return SDL_PollEvent(event);
    return poll_event(event);
}
inline const bool* GetKeyboardState(int* count) {
    if (!enabled() || is_owner()) return SDL_GetKeyboardState(count);
    thread_local std::array<bool, SDL_SCANCODE_COUNT> keys;
    keys = native_state().keyboard;
    if (count) *count = SDL_SCANCODE_COUNT;
    return keys.data();
}
inline SDL_Keymod GetModState() {
    return enabled() && !is_owner() ? native_state().modifiers : SDL_GetModState();
}
inline bool WaitEvent(SDL_Event* event) {
    if (!enabled() || is_owner()) return SDL_WaitEvent(event);
    while (!PollEvent(event)) {
        check();
        SDL_Delay(1);
    }
    return true;
}
inline void PumpEvents() { if (!enabled() || is_owner()) SDL_PumpEvents(); else check(); }
inline bool SyncWindow(SDL_Window* w) {
    if (snapshot_window(w)) { check(); return true; }
    return call([=] { return SDL_SyncWindow(w); });
}
inline bool Async(std::function<bool()> operation, unsigned key = 0) {
    if (!enabled() || is_owner()) return operation();
    submit([operation = std::move(operation)] {
        if (!operation()) throw std::runtime_error(SDL_GetError());
    }, key);
    return true;
}
inline SDL_Window* CreateWindowWithProperties(SDL_PropertiesID props) {
    return call([=] {
        if (enabled() && native_state().window)
            throw std::runtime_error("Window owner supports one native window, not ImGui multi-viewports");
        auto* w = SDL_CreateWindowWithProperties(props);
        if (w) set_window(w);
        return w;
    });
}
inline void DestroyWindow(SDL_Window* w) {
    call([=] { set_window(nullptr); SDL_DestroyWindow(w); });
}
inline bool ShowWindow(SDL_Window* w) { return Async([=] { return SDL_ShowWindow(w); }, 1); }
inline bool RestoreWindow(SDL_Window* w) { return Async([=] { return SDL_RestoreWindow(w); }, 2); }
inline bool SetWindowSize(SDL_Window* w, int x, int y) {
    return Async([=] { return SDL_SetWindowSize(w, x, y); }, 3);
}
inline bool SetWindowPosition(SDL_Window* w, int x, int y) {
    return Async([=] { return SDL_SetWindowPosition(w, x, y); }, 4);
}
inline bool SetWindowTitle(SDL_Window* w, const char* title) {
    const std::string copy(title);
    return Async([w, copy] { return SDL_SetWindowTitle(w, copy.c_str()); }, 5);
}
inline bool SetWindowMinimumSize(SDL_Window* w, int x, int y) {
    return call([=] { return SDL_SetWindowMinimumSize(w, x, y); });
}
inline bool SetWindowIcon(SDL_Window* w, SDL_Surface* s) {
    return call([=] { return SDL_SetWindowIcon(w, s); });
}
inline SDL_PropertiesID GetWindowProperties(SDL_Window* w) {
    if (enabled() && !is_owner() && w) {
        const auto state = native_state();
        if (w == state.window) return state.properties;
    }
    return call([=] { return SDL_GetWindowProperties(w); });
}
inline bool SetWindowFullscreen(SDL_Window* w, bool f) {
    return call([=] { return SDL_SetWindowFullscreen(w, f); });
}
inline bool SetWindowFullscreenMode(SDL_Window* w, const SDL_DisplayMode* mode) {
    return call([=] { return SDL_SetWindowFullscreenMode(w, mode); });
}
inline const SDL_DisplayMode* GetWindowFullscreenMode(SDL_Window* w) {
    return call([=] { return SDL_GetWindowFullscreenMode(w); });
}
inline SDL_DisplayID GetDisplayForWindow(SDL_Window* w) {
    return call([=] { return SDL_GetDisplayForWindow(w); });
}
inline const SDL_DisplayMode* GetDesktopDisplayMode(SDL_DisplayID id) {
    return call([=] { return SDL_GetDesktopDisplayMode(id); });
}
inline bool GetClosestFullscreenDisplayMode(SDL_DisplayID id, int w, int h, float rate,
                                                       bool high, SDL_DisplayMode* mode) {
    return call([=] { return SDL_GetClosestFullscreenDisplayMode(id, w, h, rate, high, mode); });
}
inline thread_local std::vector<DisplayState> monitorSnapshot;
inline SDL_DisplayID* GetDisplays(int* count) {
    if (!enabled() || is_owner()) return SDL_GetDisplays(count);
    monitorSnapshot = displays();
    const auto& states = monitorSnapshot;
    auto* ids = static_cast<SDL_DisplayID*>(SDL_malloc((states.size() + 1) * sizeof(SDL_DisplayID)));
    if (!ids) throw std::bad_alloc();
    for (size_t i = 0; i < states.size(); ++i) ids[i] = states[i].id;
    ids[states.size()] = 0;
    if (count) *count = static_cast<int>(states.size());
    return ids;
}
inline DisplayState CachedDisplay(SDL_DisplayID id) {
    for (const auto& state : monitorSnapshot) if (state.id == id) return state;
    throw std::runtime_error("Window owner display snapshot does not contain requested display");
}
inline bool GetDisplayBounds(SDL_DisplayID id, SDL_Rect* r) {
    if (enabled() && !is_owner()) { *r = CachedDisplay(id).bounds; return true; }
    return call([=] { return SDL_GetDisplayBounds(id, r); });
}
inline bool GetDisplayUsableBounds(SDL_DisplayID id, SDL_Rect* r) {
    if (enabled() && !is_owner()) {
        const auto state = CachedDisplay(id);
        *r = state.usableBounds;
        return state.usableBoundsValid;
    }
    return call([=] { return SDL_GetDisplayUsableBounds(id, r); });
}
inline float GetDisplayContentScale(SDL_DisplayID id) {
    if (enabled() && !is_owner()) return CachedDisplay(id).scale;
    return call([=] { return SDL_GetDisplayContentScale(id); });
}
inline SDL_Cursor* CreateSystemCursor(SDL_SystemCursor c) {
    return call([=] { return SDL_CreateSystemCursor(c); });
}
inline void DestroyCursor(SDL_Cursor* c) { call([=] { SDL_DestroyCursor(c); }); }
inline bool SetCursor(SDL_Cursor* c) { return Async([=] { return SDL_SetCursor(c); }, 6); }
inline SDL_Cursor* GetCursor() {
    return enabled() && !is_owner() ? native_state().cursor : SDL_GetCursor();
}
inline bool HideCursor() { return Async([] { return SDL_HideCursor(); }, 7); }
inline bool ShowCursor() { return Async([] { return SDL_ShowCursor(); }, 7); }
inline bool CaptureMouse(bool c) { return Async([=] { return SDL_CaptureMouse(c); }, 8); }
inline bool WarpMouseGlobal(float x, float y) {
    return Async([=] { return SDL_WarpMouseGlobal(x, y); }, 9);
}
inline void WarpMouseInWindow(SDL_Window* w, float x, float y) {
    if (!enabled() || is_owner()) SDL_WarpMouseInWindow(w, x, y);
    else submit([=] { SDL_WarpMouseInWindow(w, x, y); }, 9);
}
inline bool StartTextInput(SDL_Window* w) { return Async([=] { return SDL_StartTextInput(w); }, 10); }
inline bool StopTextInput(SDL_Window* w) { return Async([=] { return SDL_StopTextInput(w); }, 10); }
inline bool SetTextInputArea(SDL_Window* w, const SDL_Rect* r, int cursor) {
    const auto copy = *r;
    return Async([=] { return SDL_SetTextInputArea(w, &copy, cursor); }, 11);
}
inline char* GetClipboardText() { return call([] { return SDL_GetClipboardText(); }); }
inline bool SetClipboardText(const char* text) {
    const std::string copy(text);
    return Async([copy] { return SDL_SetClipboardText(copy.c_str()); });
}
}

#define SDL_GetWindowSize aurora::window_owner::sdl::GetWindowSize
#define SDL_GetWindowSizeInPixels aurora::window_owner::sdl::GetWindowSizeInPixels
#define SDL_GetWindowPosition aurora::window_owner::sdl::GetWindowPosition
#define SDL_GetWindowFlags aurora::window_owner::sdl::GetWindowFlags
#define SDL_GetWindowDisplayScale aurora::window_owner::sdl::GetWindowDisplayScale
#define SDL_GetWindowID aurora::window_owner::sdl::GetWindowID
#define SDL_GetWindowFromID aurora::window_owner::sdl::GetWindowFromID
#define SDL_GetKeyboardFocus aurora::window_owner::sdl::GetKeyboardFocus
#define SDL_GetMouseFocus aurora::window_owner::sdl::GetMouseFocus
#define SDL_GetMouseState aurora::window_owner::sdl::GetMouseState
#define SDL_GetKeyboardState aurora::window_owner::sdl::GetKeyboardState
#define SDL_GetModState aurora::window_owner::sdl::GetModState
#define SDL_GetGlobalMouseState aurora::window_owner::sdl::GetGlobalMouseState
#define SDL_GetRelativeMouseState aurora::window_owner::sdl::GetRelativeMouseState
#define SDL_GetWindowRelativeMouseMode aurora::window_owner::sdl::GetWindowRelativeMouseMode
#define SDL_PollEvent aurora::window_owner::sdl::PollEvent
#define SDL_WaitEvent aurora::window_owner::sdl::WaitEvent
#define SDL_PumpEvents aurora::window_owner::sdl::PumpEvents
#define SDL_SyncWindow aurora::window_owner::sdl::SyncWindow
#define SDL_CreateWindowWithProperties aurora::window_owner::sdl::CreateWindowWithProperties
#define SDL_DestroyWindow aurora::window_owner::sdl::DestroyWindow
#define SDL_ShowWindow aurora::window_owner::sdl::ShowWindow
#define SDL_RestoreWindow aurora::window_owner::sdl::RestoreWindow
#define SDL_SetWindowSize aurora::window_owner::sdl::SetWindowSize
#define SDL_SetWindowPosition aurora::window_owner::sdl::SetWindowPosition
#define SDL_SetWindowTitle aurora::window_owner::sdl::SetWindowTitle
#define SDL_SetWindowMinimumSize aurora::window_owner::sdl::SetWindowMinimumSize
#define SDL_SetWindowIcon aurora::window_owner::sdl::SetWindowIcon
#define SDL_GetWindowProperties aurora::window_owner::sdl::GetWindowProperties
#define SDL_SetWindowFullscreen aurora::window_owner::sdl::SetWindowFullscreen
#define SDL_SetWindowFullscreenMode aurora::window_owner::sdl::SetWindowFullscreenMode
#define SDL_GetWindowFullscreenMode aurora::window_owner::sdl::GetWindowFullscreenMode
#define SDL_GetDisplayForWindow aurora::window_owner::sdl::GetDisplayForWindow
#define SDL_GetDesktopDisplayMode aurora::window_owner::sdl::GetDesktopDisplayMode
#define SDL_GetClosestFullscreenDisplayMode aurora::window_owner::sdl::GetClosestFullscreenDisplayMode
#define SDL_GetDisplays aurora::window_owner::sdl::GetDisplays
#define SDL_GetDisplayBounds aurora::window_owner::sdl::GetDisplayBounds
#define SDL_GetDisplayUsableBounds aurora::window_owner::sdl::GetDisplayUsableBounds
#define SDL_GetDisplayContentScale aurora::window_owner::sdl::GetDisplayContentScale
#define SDL_CreateSystemCursor aurora::window_owner::sdl::CreateSystemCursor
#define SDL_DestroyCursor aurora::window_owner::sdl::DestroyCursor
#define SDL_SetCursor aurora::window_owner::sdl::SetCursor
#define SDL_GetCursor aurora::window_owner::sdl::GetCursor
#define SDL_HideCursor aurora::window_owner::sdl::HideCursor
#define SDL_ShowCursor aurora::window_owner::sdl::ShowCursor
#define SDL_CaptureMouse aurora::window_owner::sdl::CaptureMouse
#define SDL_WarpMouseGlobal aurora::window_owner::sdl::WarpMouseGlobal
#define SDL_WarpMouseInWindow aurora::window_owner::sdl::WarpMouseInWindow
#define SDL_StartTextInput aurora::window_owner::sdl::StartTextInput
#define SDL_StopTextInput aurora::window_owner::sdl::StopTextInput
#define SDL_SetTextInputArea aurora::window_owner::sdl::SetTextInputArea
#define SDL_GetClipboardText aurora::window_owner::sdl::GetClipboardText
#define SDL_SetClipboardText aurora::window_owner::sdl::SetClipboardText
