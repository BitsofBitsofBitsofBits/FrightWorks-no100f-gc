#include <aurora/window_owner.hpp>
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
            strings.emplace_back(value, length);
            return strings.back().c_str();
        };
        strings.reserve(128);
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

void collect_events() {
    SDL_Event event{};
    for (;;) {
        const int result = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST);
        if (result < 0) throw std::runtime_error(SDL_GetError());
        if (!result) break;
        OwnedEvent copy(event);
        std::lock_guard lock(mutex);
        if (events.size() == 2048)
            throw std::runtime_error("Window owner event queue exceeded 2048 events; input cannot be dropped safely");
        events.push_back(std::move(copy));
    }
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
void check() {
    std::lock_guard lock(mutex);
    if (failure && !shuttingDown) std::rethrow_exception(failure);
}
void submit(std::function<void()> operation, unsigned replaceKey) {
    if (!enabled() || is_owner()) { operation(); return; }
    std::lock_guard lock(mutex);
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
    operations.push_back({std::move(operation), replaceKey});
    wake.notify_one();
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
    std::lock_guard lock(mutex);
    if (!failure) failure = error;
    wake.notify_one();
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
        wake.notify_one();
    });
    auto nextDisplayRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!finished.load(std::memory_order_acquire)) {
        std::deque<Operation> pending;
        {
            std::unique_lock lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(1), [] {
                return !operations.empty() || finished.load(std::memory_order_acquire);
            });
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
            SDL_PumpEvents();
            try {
                publish();
                collect_events();
                if (std::chrono::steady_clock::now() >= nextDisplayRefresh) {
                    publish_displays();
                    nextDisplayRefresh = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                }
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
