#include "input/script.h"
#include "input/controls.h"
#include <set>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace nova::input {
Script Script::parse(std::istream &stream) {
    Script result;
    uint64_t frame = 0;
    std::string line;
    size_t number = 0;
    bool pressed = false;
    std::set<SDL_Keycode> held;
    std::set<Uint8> buttons;
    auto fail = [&] {
        throw std::invalid_argument("input-script line " + std::to_string(number) +
                                    ": invalid command, arguments or press sequence");
    };
    auto advance = [&](uint64_t count) {
        if (count > std::numeric_limits<uint32_t>::max() - frame)
            fail();
        frame += count;
    };
    auto add = [&](SDL_Event event, const std::string &text = {}, bool native_only = false) {
        result.events_.push_back({frame, event, text, number, native_only});
    };
    while (std::getline(stream, line)) {
        ++number;
        std::istringstream words(line);
        std::string command;
        if (!(words >> command) || command.starts_with('#'))
            continue;
        if (command == "frame" || command == "wait") {
            int64_t value = -1;
            if (!(words >> value) || value < 0 || value > UINT32_MAX)
                fail();
            if (command == "frame") {
                if (static_cast<uint64_t>(value) < frame)
                    fail();
                frame = static_cast<uint64_t>(value);
            } else
                advance(static_cast<uint64_t>(value));
        } else if (command == "click" || command == "down" || command == "move" ||
                   command == "up") {
            float x, y;
            if (!(words >> x >> y) || !std::isfinite(x) || !std::isfinite(y) ||
                std::abs(x) > 100000 || std::abs(y) > 100000)
                fail();
            if ((command == "click" || command == "down")
                    ? (pressed || buttons.contains(SDL_BUTTON_LEFT)) : !pressed)
                fail();
            SDL_Event event{};
            if (command == "move") {
                event.type = SDL_EVENT_MOUSE_MOTION;
                event.motion.state = SDL_BUTTON_LMASK;
                event.motion.x = x;
                event.motion.y = y;
                add(event);
            } else {
                event.type =
                    command == "up" ? SDL_EVENT_MOUSE_BUTTON_UP : SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.button = SDL_BUTTON_LEFT;
                event.button.down = command != "up";
                event.button.x = x;
                event.button.y = y;
                add(event);
                pressed = command != "up";
                if (command == "click") {
                    advance(1); // At least one guest step observes a held press.
                    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
                    event.button.down = false;
                    add(event);
                    pressed = false;
                }
            }
        } else if (command == "key" || command == "keydown" || command == "keyup") {
            std::string name;
            if (!(words >> name))
                fail();
            SDL_Keycode key = key_code(name);
            if (!key || (command == "keyup" ? !held.contains(key) : held.contains(key)))
                fail();
            SDL_Event event{};
            event.type = command == "keyup" ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.down = command != "keyup";
            add(event);
            if (command == "keyup") held.erase(key); else held.insert(key);
            if (command == "key") {
                advance(1);
                event.type = SDL_EVENT_KEY_UP;
                event.key.down = false;
                add(event);
                held.erase(key);
            }
        } else if (command == "relative" || command == "wheel") {
            float x = 0, y = 0;
            if (command == "relative") { if (!(words >> x >> y)) fail(); }
            else if (!(words >> y)) fail();
            if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 100000 ||
                std::abs(y) > 100000) fail();
            SDL_Event event{};
            if (command == "relative") {
                event.type = SDL_EVENT_MOUSE_MOTION; event.motion.xrel = x; event.motion.yrel = y;
            } else { event.type = SDL_EVENT_MOUSE_WHEEL; event.wheel.y = y; }
            add(event,{},true);
        } else if (command == "buttondown" || command == "buttonup") {
            std::string name;
            if (!(words >> name)) fail();
            Uint8 button = name == "LEFT" ? SDL_BUTTON_LEFT : name == "RIGHT" ? SDL_BUTTON_RIGHT
                : name == "MIDDLE" ? SDL_BUTTON_MIDDLE : 0;
            bool down = command == "buttondown";
            if (!button || (down ? (buttons.contains(button) ||
                (button == SDL_BUTTON_LEFT && pressed)) : !buttons.contains(button))) fail();
            if (down) buttons.insert(button); else buttons.erase(button);
            SDL_Event event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.button = button; event.button.down = down;
            add(event,{},true);
        } else if (command == "focus") {
            std::string state;
            if (!(words >> state) || (state != "lost" && state != "gained")) fail();
            SDL_Event event{};
            event.type = state == "lost" ? SDL_EVENT_WINDOW_FOCUS_LOST : SDL_EVENT_WINDOW_FOCUS_GAINED;
            add(event);
        } else if (command == "text") {
            std::string value;
            words >> std::ws;
            if (words.peek() != '"' || !(words >> std::quoted(value)) || value.empty() ||
                value.find('\0') != std::string::npos ||
                !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0))
                fail();
            SDL_Event event{};
            event.type = SDL_EVENT_TEXT_INPUT;
            add(event, value);
        } else
            fail();
        words >> std::ws;
        if (!words.eof() && words.peek() != '#')
            fail();
    }
    if (stream.bad())
        throw std::runtime_error("Cannot read input script");
    if (pressed || !held.empty() || !buttons.empty())
        fail();
    return result;
}
bool Script::scheduled(uint64_t frame) const {
    // Immutable schedule is safe to query on the GL thread; next_ is UI-thread only.
    auto found = std::lower_bound(events_.begin(), events_.end(), frame,
                                  [](const Event &event, uint64_t n) { return event.frame < n; });
    return found != events_.end() && found->frame == frame;
}
namespace {
Uint32 script_type() {
    static const Uint32 type = SDL_RegisterEvents(1);
    if (!type) throw std::runtime_error("Cannot reserve script SDL event type");
    return type;
}
}
bool Script::unwrap(SDL_Event &event, bool *native_only) {
    if (event.type != script_type()) return false;
    if (!event.user.data1) throw std::runtime_error("Invalid script event");
    if (native_only) *native_only = event.user.code != 0;
    event = *static_cast<const SDL_Event *>(event.user.data1);
    return true;
}
void Script::push(uint64_t frame, SDL_WindowID window, double elapsed_ms) {
    while (next_ < events_.size() && events_[next_].frame <= frame) {
        auto &entry = events_[next_++];
        SDL_Event &event = entry.event;
        if (event.type == SDL_EVENT_MOUSE_MOTION)
            event.motion.windowID = window;
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            event.button.windowID = window;
        else if (event.type == SDL_EVENT_TEXT_INPUT) {
            event.text.windowID = window;
            event.text.text = entry.text.c_str(); // Storage lives until script/process destruction.
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            event.wheel.windowID = window;
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
            event.window.windowID = window;
        else
            event.key.windowID = window;
        SDL_Event wrapper{};
        wrapper.type = script_type();
        wrapper.user.data1 = &event;
        wrapper.user.code = entry.native_only ? 1 : 0;
        if (!SDL_PushEvent(&wrapper))
            throw std::runtime_error("input-script SDL_PushEvent failed");
        std::printf(
            "input-script line=%zu scheduled=%llu dispatched=%llu elapsed_ms=%.3f type=%u\n",
            entry.line, entry.frame, frame, elapsed_ms, event.type);
    }
}
} // namespace nova::input
