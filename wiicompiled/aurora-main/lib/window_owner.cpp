#include <aurora/window_owner.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <string>

namespace aurora::window_owner {
namespace {
struct Operation { std::function<void()> function; unsigned replaceKey; };
std::atomic<bool> active{false}, finished{false};
std::atomic<bool> closeRequested{false};
std::thread::id owner;
thread_local bool guestThread = false;
std::mutex mutex;
std::condition_variable wake;
std::deque<Operation> operations;
NativeState native;
float relativeX = 0, relativeY = 0;
SDL_MouseButtonFlags relativeButtons = 0;
std::exception_ptr failure;
bool shuttingDown = false;
std::vector<DisplayState> displayStates;
struct OwnedEvent {
    SDL_Event event{};
    std::vector<std::string> strings;
    std::vector<const char*> pointers;
    size_t bytes = 0;
    explicit OwnedEvent(const SDL_Event& source) : event(source) {
        auto text = [&](const char* value) -> const char* {
            if (!value) return nullptr;
            size_t length = 0;
            while (length <= 4096 && value[length]) ++length;
            if (length > 4096 || bytes + length + 1 > 4096)
                throw std::runtime_error("Window owner event payload exceeded 4096 bytes");
            bytes += length + 1;
            if (strings.empty()) strings.reserve(128);
            strings.emplace_back(value, length);
            return strings.back().c_str();
        };
        auto list = [&](const char* const* values, int count) {
            if (count < 0 || count > 128 || (count && !values))
                throw std::runtime_error("Window owner event string list exceeded 128 entries or was invalid");
            for (int i = 0; i < count; ++i) pointers.push_back(text(values[i]));
        };
        switch (event.type) {
        case SDL_EVENT_TEXT_INPUT: event.text.text = text(source.text.text); break;
        case SDL_EVENT_TEXT_EDITING: event.edit.text = text(source.edit.text); break;
        case SDL_EVENT_TEXT_EDITING_CANDIDATES:
            list(source.edit_candidates.candidates, source.edit_candidates.num_candidates);
            event.edit_candidates.candidates = pointers.data();
            break;
        case SDL_EVENT_CLIPBOARD_UPDATE:
            list(source.clipboard.mime_types, source.clipboard.num_mime_types);
            event.clipboard.mime_types = pointers.data();
            break;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_POSITION:
            event.drop.source = text(source.drop.source);
            event.drop.data = text(source.drop.data);
            break;
        default: break;
        }
    }
};
std::deque<OwnedEvent> events, deliveredEvents;
bool nextEventBatch = true;
Uint32 wakeEvent = 0;

void notify_owner() noexcept {
    wake.notify_one();
    if (!active.load(std::memory_order_acquire)) return;
    SDL_Event event{};
    event.type = wakeEvent;
    if (!SDL_PushEvent(&event)) {
        try { throw std::runtime_error(std::string("Failed to wake the window owner: ") + SDL_GetError()); }
        catch (...) {
            std::lock_guard lock(mutex);
            if (!failure) failure = std::current_exception();
        }
    }
}

bool is_motion(const SDL_Event& event) {
    return event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION ||
           event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
}

bool merge_motion(const SDL_Event& event) {
    if (!is_motion(event)) return false;
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        auto& previous = it->event;
        // Do not move motion across a key/button transition or another discrete event.
        if (!is_motion(previous)) break;
        if (previous.type != event.type) continue;
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if (previous.motion.which != event.motion.which ||
                previous.motion.windowID != event.motion.windowID ||
                previous.motion.state != event.motion.state) continue;
            const float dx = previous.motion.xrel + event.motion.xrel;
            const float dy = previous.motion.yrel + event.motion.yrel;
            previous = event;
            previous.motion.xrel = dx;
            previous.motion.yrel = dy;
        } else if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
            if (previous.jaxis.which != event.jaxis.which || previous.jaxis.axis != event.jaxis.axis) continue;
            previous = event;
        } else {
            if (previous.gaxis.which != event.gaxis.which || previous.gaxis.axis != event.gaxis.axis) continue;
            previous = event;
        }
        const auto latest = previous;
        auto position = it.base();
        events.erase(--position);
        events.emplace_back(latest);
        return true;
    }
    return false;
}

bool queue_event(const SDL_Event& event) {
    if (event.type == wakeEvent || event.type == SDL_EVENT_POLL_SENTINEL) return false;
    std::lock_guard lock(mutex);
    if (!merge_motion(event)) {
        if (events.size() == 2048)
            throw std::runtime_error("Window owner event queue exceeded 2048 events; input cannot be dropped safely");
        events.emplace_back(event);
    }
    return (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) ||
           (event.type >= SDL_EVENT_KEY_DOWN && event.type <= SDL_EVENT_TEXT_INPUT) ||
           (event.type >= SDL_EVENT_MOUSE_MOTION && event.type <= SDL_EVENT_MOUSE_WHEEL);
}

bool collect_events() {
    SDL_Event event{};
    bool changed = false;
    for (;;) {
        const int result = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
        if (result < 0) throw std::runtime_error(SDL_GetError());
        if (!result) break;
        changed |= queue_event(event);
    }
    return changed;
}

void publish_displays() {
    int count = 0;
    auto* ids = SDL_GetDisplays(&count);
    if (!ids) throw std::runtime_error(SDL_GetError());
    std::vector<DisplayState> states;
    try {
        if (count < 0 || count > 128) throw std::runtime_error("Window owner display list exceeded 128 displays");
        for (int i = 0; i < count; ++i) {
            DisplayState state;
            state.id = ids[i];
            if (!SDL_GetDisplayBounds(state.id, &state.bounds)) throw std::runtime_error(SDL_GetError());
            state.usableBoundsValid = SDL_GetDisplayUsableBounds(state.id, &state.usableBounds);
            state.scale = SDL_GetDisplayContentScale(state.id);
            if (state.scale <= 0) throw std::runtime_error(SDL_GetError());
            states.push_back(state);
        }
    } catch (...) { SDL_free(ids); throw; }
    SDL_free(ids);
    std::lock_guard lock(mutex);
    displayStates = std::move(states);
}

void publish() {
    NativeState state;
    {
        std::lock_guard lock(mutex);
        state.window = native.window;
    }
    if (state.window) {
        state.id = SDL_GetWindowID(state.window);
        state.properties = SDL_GetWindowProperties(state.window);
        if (!state.properties) throw std::runtime_error(SDL_GetError());
        state.flags = SDL_GetWindowFlags(state.window);
        if (!SDL_GetWindowSize(state.window, &state.width, &state.height) ||
            !SDL_GetWindowSizeInPixels(state.window, &state.pixelsWidth, &state.pixelsHeight) ||
            !SDL_GetWindowPosition(state.window, &state.x, &state.y))
            throw std::runtime_error(SDL_GetError());
        state.scale = SDL_GetWindowDisplayScale(state.window);
        state.relativeMouse = SDL_GetWindowRelativeMouseMode(state.window);
    }
    state.keyboardFocus = SDL_GetKeyboardFocus();
    state.mouseFocus = SDL_GetMouseFocus();
    state.cursor = SDL_GetCursor();
    int keyCount = 0;
    const bool* keyboard = SDL_GetKeyboardState(&keyCount);
    if (!keyboard || keyCount != SDL_SCANCODE_COUNT)
        throw std::runtime_error("Window owner keyboard snapshot size was invalid");
    std::copy_n(keyboard, keyCount, state.keyboard.begin());
    state.modifiers = SDL_GetModState();
    state.mouseButtons = SDL_GetMouseState(&state.mouseX, &state.mouseY);
    state.globalButtons = SDL_GetGlobalMouseState(&state.globalX, &state.globalY);
    float dx = 0, dy = 0;
    const auto buttons = SDL_GetRelativeMouseState(&dx, &dy);
    std::lock_guard lock(mutex);
    native = state;
    relativeX += dx;
    relativeY += dy;
    relativeButtons = buttons;
}
}
bool enabled() noexcept { return active.load(std::memory_order_acquire); }
bool is_owner() noexcept { return std::this_thread::get_id() == owner; }
bool is_guest() noexcept { return enabled() && guestThread; }
void request_close() noexcept { closeRequested.store(true, std::memory_order_release); }
bool consume_close_request() noexcept { return closeRequested.exchange(false, std::memory_order_acq_rel); }
void check() {
    std::lock_guard lock(mutex);
    if (failure && !shuttingDown) std::rethrow_exception(failure);
}
void submit(std::function<void()> operation, unsigned replaceKey) {
    if (!enabled() || is_owner()) { operation(); return; }
    std::unique_lock lock(mutex);
    if (failure && !shuttingDown) std::rethrow_exception(failure);
    if (finished.load(std::memory_order_acquire))
        throw std::runtime_error("Window owner rejected an operation after guest shutdown");
    if (replaceKey) {
        for (auto& pending : operations) {
            if (pending.replaceKey == replaceKey) {
                pending.function = std::move(operation);
                return;
            }
        }
    }
    if (operations.size() == 256) {
        throw std::runtime_error("Window owner command queue exceeded 256 pending operations");
    }
    const bool notify = operations.empty();
    operations.push_back({std::move(operation), replaceKey});
    lock.unlock();
    if (notify) notify_owner();
}
NativeState native_state() {
    std::lock_guard lock(mutex);
    return native;
}
SDL_MouseButtonFlags relative_mouse(float* x, float* y) {
    check();
    std::lock_guard lock(mutex);
    if (x) *x = relativeX;
    if (y) *y = relativeY;
    relativeX = relativeY = 0;
    return relativeButtons;
}
std::vector<DisplayState> displays() {
    std::lock_guard lock(mutex);
    return displayStates;
}
bool poll_event(SDL_Event* event) {
    check();
    std::lock_guard lock(mutex);
    if (nextEventBatch) {
        deliveredEvents.clear();
        nextEventBatch = false;
    }
    if (events.empty()) { nextEventBatch = true; return false; }
    if (deliveredEvents.size() == 2048)
        throw std::runtime_error("Window owner event delivery exceeded 2048 events per poll batch");
    deliveredEvents.push_back(std::move(events.front()));
    events.pop_front();
    if (event) *event = deliveredEvents.back().event;
    return true;
}
void begin_shutdown() {
    std::lock_guard lock(mutex);
    shuttingDown = true;
}
void report_failure(std::exception_ptr error) {
    {
        std::lock_guard lock(mutex);
        if (!failure) failure = error;
    }
    notify_owner();
}
void set_window(SDL_Window* window) {
    if (enabled() && !is_owner())
        throw std::runtime_error("SDL window publication attempted from guest thread");
    {
        std::lock_guard lock(mutex);
        native.window = window;
    }
    if (enabled()) publish();
}
void refresh_window_state() {
    if (!enabled()) return;
    if (!is_owner())
        throw std::runtime_error("Window state publication attempted outside the SDL owner");
    try {
        publish();
        collect_events();
    } catch (...) {
        report_failure(std::current_exception());
    }
}
int run(std::function<int()> game) {
    if (enabled()) throw std::runtime_error("Window owner cannot be entered recursively or concurrently");
    owner = std::this_thread::get_id();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        throw std::runtime_error(SDL_GetError());
    struct SdlLifetime { ~SdlLifetime() { active.store(false, std::memory_order_release); SDL_Quit(); } } lifetime;
    if (!SDL_IsMainThread())
        throw std::runtime_error("Window owner is not the SDL main thread");
    wakeEvent = SDL_RegisterEvents(1);
    if (!wakeEvent) throw std::runtime_error(SDL_GetError());
    {
        std::lock_guard lock(mutex);
        operations.clear();
        events.clear();
        deliveredEvents.clear();
        nextEventBatch = true;
        native = {};
        relativeX = relativeY = 0;
        relativeButtons = 0;
        failure = nullptr;
        shuttingDown = false;
        closeRequested.store(false, std::memory_order_release);
    }
    publish_displays();
    finished.store(false, std::memory_order_release);
    active.store(true, std::memory_order_release);
    int result = 1;
    std::exception_ptr guestFailure;
    std::thread guest([&] {
        guestThread = true;
        try { result = game(); }
        catch (...) { guestFailure = std::current_exception(); }
        guestThread = false;
        finished.store(true, std::memory_order_release);
        notify_owner();
    });
    auto nextDisplayRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!finished.load(std::memory_order_acquire)) {
        std::deque<Operation> pending;
        {
            std::unique_lock lock(mutex);
            if (failure) {
                wake.wait(lock, [] {
                    return !operations.empty() || finished.load(std::memory_order_acquire);
                });
            }
            pending.swap(operations);
        }
        for (auto& operation : pending) {
            try { operation.function(); }
            catch (...) {
                std::lock_guard lock(mutex);
                if (!failure) failure = std::current_exception();
            }
        }
        if (!finished.load(std::memory_order_acquire)) {
            {
                std::lock_guard lock(mutex);
                if (failure) continue;
            }
            if (!SDL_WasInit(SDL_INIT_EVENTS)) continue;
            try {
                if (!pending.empty()) publish();
                // SDL's event wait services native messages and device polling without
                // repeatedly timing out a C++ condition variable. Commands wake it with
                // a private event; timeout-only wakes do not need a new native snapshot.
                if (std::chrono::steady_clock::now() >= nextDisplayRefresh) {
                    publish_displays();
                    nextDisplayRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                }
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    nextDisplayRefresh - std::chrono::steady_clock::now()).count();
                SDL_Event event{};
                SDL_ClearError();
                const bool received = SDL_WaitEventTimeout(&event, static_cast<Sint32>(std::max<int64_t>(1, remaining)));
                if (!received && *SDL_GetError()) throw std::runtime_error(SDL_GetError());
                bool changed = received && queue_event(event);
                changed |= collect_events();
                if (changed) publish();
            }
            catch (...) {
                std::lock_guard lock(mutex);
                if (!failure) failure = std::current_exception();
            }
        }
    }
    guest.join();
    if (guestFailure) std::rethrow_exception(guestFailure);
    if (failure) std::rethrow_exception(failure);
    return result;
}
}
