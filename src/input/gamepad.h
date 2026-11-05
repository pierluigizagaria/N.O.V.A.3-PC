#pragma once
#include "input/controls.h"
namespace nova::input {
// UI/main-thread SDL owner. No OS input synthesis and no guest calls. This unit
// is deliberately not activated by play.cpp until its independent gate passes.
class Gamepad {
  public:
    explicit Gamepad(Controls &,SDL_JoystickID requested=0);
    ~Gamepad();
    Gamepad(const Gamepad &)=delete;
    Gamepad &operator=(const Gamepad &)=delete;
    void enabled(bool);
    void poll(); // Call after pumping SDL; state does not depend on render FPS.
    bool handle(const SDL_Event &); // Preserve short button edges in queue order.
    SDL_JoystickID device() const { return id_; }
    SDL_GamepadButtonLabel label(SDL_GamepadButton) const;
  private:
    Controls &controls_;
    SDL_Gamepad *pad_=nullptr;
    SDL_JoystickID id_=0,requested_=0;
    uint64_t generation_=0;
    bool enabled_=false,left_trigger_=false,right_trigger_=false;
    std::array<Sint16,SDL_GAMEPAD_AXIS_COUNT> axes_{};
    std::bitset<SDL_GAMEPAD_BUTTON_COUNT> buttons_;
    void close();
    void sample();
    void publish();
};
}
