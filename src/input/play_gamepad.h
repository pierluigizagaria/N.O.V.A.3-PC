#pragma once
#include "input/gamepad.h"
#include <cstdio>
#include <exception>
#include <memory>

namespace nova::input {
// UI ownership only. Native temporary permission must NOT become physical release.
struct PlayGamepadContext {
    bool ready=false, focused=false, text=false, panel=false, binding_capture=false;
    bool transition=false, quitting=false;
    bool permits() const {
        return ready && focused && !text && !panel && !binding_capture && !transition && !quitting;
    }
};
class PlayGamepad {
  public:
    using Factory=std::unique_ptr<Gamepad> (*)(Controls &,SDL_JoystickID);
    static bool physical_session(bool scripted,bool diagnostics,bool bounded,bool observer) {
        return !scripted && !diagnostics && !bounded && !observer;
    }
    // Construct after SDL/window, destroy before SDL and Controls, always on UI.
    // The factory is a host-only optional initialization boundary; no guest call.
    explicit PlayGamepad(Controls &controls,bool physical,SDL_JoystickID requested=0,Factory factory=&create) {
        if(!physical) return; // No SDL init, enumeration, open, poll or publication.
        try { pad_=factory(controls,requested); }
        catch(const std::exception &error) {
            failed_=true;
            std::fprintf(stderr,"gamepad unavailable; keyboard/mouse remain active: %.240s\n",error.what());
        }
    }
    static bool event(Uint32 type) {
        return type==SDL_EVENT_GAMEPAD_ADDED || type==SDL_EVENT_GAMEPAD_REMOVED ||
            type==SDL_EVENT_GAMEPAD_REMAPPED || type==SDL_EVENT_GAMEPAD_AXIS_MOTION ||
            type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || type==SDL_EVENT_GAMEPAD_BUTTON_UP;
    }
    void context(PlayGamepadContext value) {
        const bool next=value.permits();
        if(pad_ && !next && enabled_) { enabled_=false; pad_->enabled(false); }
    }
    void suspend() { context({}); }
    // Route before SDL_GetWindowFromEvent: gamepad events carry no window ID.
    bool handle(const SDL_Event &value,PlayGamepadContext state) {
        if(!event(value.type)) return false;
        context(state);
        if(pad_) pad_->handle(value);
        return true; // Foreign/disabled pad events never enter mouse/menu routing.
    }
    void poll(PlayGamepadContext state) {
        context(state);
        if(!pad_) return;
        pad_->poll(); // Fresh physical state after the queue and final UI changes.
        if(state.permits() && !enabled_) {
            // Never rearm from the cached neutral of an earlier focus/capture epoch.
            enabled_=true; pad_->enabled(true);
        }
    }
    void shutdown() { pad_.reset(); enabled_=false; } // Explicit before play's TerminateProcess.
    SDL_JoystickID device() const { return pad_ ? pad_->device() : 0; }
    bool available() const { return pad_!=nullptr; }
    bool failed() const { return failed_; }
  private:
    static std::unique_ptr<Gamepad> create(Controls &controls,SDL_JoystickID requested) {
        return std::make_unique<Gamepad>(controls,requested);
    }
    std::unique_ptr<Gamepad> pad_;
    bool enabled_=false,failed_=false;
};
}
