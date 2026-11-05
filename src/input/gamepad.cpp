#include "input/gamepad.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nova::input {
namespace {
void main_thread() { if(!SDL_IsMainThread()) throw std::logic_error("Gamepad requires the SDL main thread"); }
float axis(Sint16 value) { return value<0 ? value/32768.0f : value/32767.0f; }
void stick(Sint16 raw_x,Sint16 raw_y,float &x,float &y) {
    x=axis(raw_x); y=axis(raw_y);
    const float length=std::hypot(x,y);
    // Provisional radial dead zone, independent of mouse configuration.
    constexpr float dead=.25f;
    if(length<=dead) { x=y=0; return; }
    const float scale=(std::min(length,1.0f)-dead)/((1-dead)*length);
    x*=scale; y*=scale;
}
bool trigger(Sint16 value,bool was_down) { return value>=(was_down ? 13107 : 16384); }
}
Gamepad::Gamepad(Controls &controls,SDL_JoystickID requested) : controls_(controls),requested_(requested) {
    main_thread();
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
}
Gamepad::~Gamepad() {
    SDL_assert(SDL_IsMainThread()); close(); SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
void Gamepad::close() {
    controls_.clear_gamepad();
    if(pad_) SDL_CloseGamepad(pad_);
    pad_=nullptr; id_=0; axes_={}; buttons_.reset(); left_trigger_=right_trigger_=false;
}
void Gamepad::enabled(bool value) {
    main_thread();
    if(enabled_!=value) { enabled_=value; controls_.clear_gamepad(); }
    if(pad_) publish();
}
void Gamepad::sample() {
    for(int i=0;i<SDL_GAMEPAD_AXIS_COUNT;++i) axes_[i]=SDL_GetGamepadAxis(pad_,static_cast<SDL_GamepadAxis>(i));
    for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT;++i) buttons_.set(i,SDL_GetGamepadButton(pad_,static_cast<SDL_GamepadButton>(i)));
    publish();
}
void Gamepad::publish() {
    if(!enabled_ || !pad_) { controls_.clear_gamepad(); return; }
    GamepadSample state; state.device=id_; state.generation=generation_;
    stick(axes_[SDL_GAMEPAD_AXIS_LEFTX],axes_[SDL_GAMEPAD_AXIS_LEFTY],state.axes.move_x,state.axes.move_y);
    stick(axes_[SDL_GAMEPAD_AXIS_RIGHTX],axes_[SDL_GAMEPAD_AXIS_RIGHTY],state.axes.look_x,state.axes.look_y);
    left_trigger_=trigger(axes_[SDL_GAMEPAD_AXIS_LEFT_TRIGGER],left_trigger_);
    right_trigger_=trigger(axes_[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER],right_trigger_);
    for(auto [button,action] : {std::pair{SDL_GAMEPAD_BUTTON_SOUTH,Action::jump},
        std::pair{SDL_GAMEPAD_BUTTON_EAST,Action::interact},std::pair{SDL_GAMEPAD_BUTTON_WEST,Action::reload},
        std::pair{SDL_GAMEPAD_BUTTON_NORTH,Action::next},std::pair{SDL_GAMEPAD_BUTTON_LEFT_STICK,Action::sprint},
        std::pair{SDL_GAMEPAD_BUTTON_RIGHT_STICK,Action::melee},std::pair{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,Action::grenade},
        std::pair{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,Action::power},std::pair{SDL_GAMEPAD_BUTTON_DPAD_LEFT,Action::previous}})
        state.held.set(static_cast<size_t>(action),buttons_.test(static_cast<size_t>(button)));
    state.held.set(static_cast<size_t>(Action::aim),left_trigger_);
    state.held.set(static_cast<size_t>(Action::fire),right_trigger_);
    state.neutral=buttons_.none() && state.axes.move_x==0 && state.axes.move_y==0 &&
        state.axes.look_x==0 && state.axes.look_y==0 && axes_[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]<=4096 &&
        axes_[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]<=4096;
    controls_.submit_gamepad(state);
}
void Gamepad::poll() {
    main_thread();
    if(pad_ && !SDL_GamepadConnected(pad_)) close();
    if(!pad_) {
        int count=0; auto ids=SDL_GetGamepads(&count);
        SDL_JoystickID selected=0;
        for(int i=0;ids && i<count;++i)
            if((!requested_ || requested_==ids[i]) && (!selected || ids[i]<selected)) selected=ids[i];
        SDL_free(ids);
        if(selected) {
            pad_=SDL_OpenGamepad(selected);
            if(pad_) { id_=selected; ++generation_; controls_.clear_gamepad(); }
        }
    }
    if(pad_) sample();
}
bool Gamepad::handle(const SDL_Event &event) {
    main_thread();
    if(event.type==SDL_EVENT_GAMEPAD_REMOVED && event.gdevice.which==id_) { close(); return true; }
    if(event.type==SDL_EVENT_GAMEPAD_ADDED) { if(!pad_) poll(); return true; }
    if(!pad_) return false;
    if(event.type==SDL_EVENT_GAMEPAD_REMAPPED && event.gdevice.which==id_) {
        ++generation_; controls_.clear_gamepad(); sample(); return true;
    }
    if(event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.which==id_ && event.gaxis.axis<SDL_GAMEPAD_AXIS_COUNT) {
        axes_[event.gaxis.axis]=event.gaxis.value; publish(); return true;
    }
    if((event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type==SDL_EVENT_GAMEPAD_BUTTON_UP) &&
       event.gbutton.which==id_ && event.gbutton.button<SDL_GAMEPAD_BUTTON_COUNT) {
        buttons_.set(event.gbutton.button,event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN); publish(); return true;
    }
    return false;
}
SDL_GamepadButtonLabel Gamepad::label(SDL_GamepadButton button) const {
    main_thread(); return pad_ ? SDL_GetGamepadButtonLabel(pad_,button) : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
}
}
