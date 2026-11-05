#pragma once
#include "input/controls.h"
#include <optional>
#include <string>

namespace nova::input {
// A single physical input maps to one shared action. Every implemented action
// overlaps on foot; vehicle aliases are the same action, not independent rows.
enum class BindingKind { key, mouse, wheel_up, wheel_down };
struct Binding {
    BindingKind kind=BindingKind::key;
    uint32_t code=0;
    bool operator==(const Binding &) const = default;
};
const char *action_label(Action);
const char *action_contexts(Action);
bool held_binding_required(Action);
bool bindable_key(SDL_Keycode);
bool valid_binding(Binding);
std::optional<Action> binding_owner(const ControlsConfig &,Binding);
std::string binding_label(Binding);
std::string action_bindings(const ControlsConfig &,Action);
// Provisional conflict policy: reject rather than steal. A completed Change
// replaces all aliases for this row; unrelated rows and look options survive.
std::string rebind(ControlsConfig &,Action,Binding);
void reset_bindings(ControlsConfig &);
std::string serialize_bindings(const ControlsConfig &);
bool same_bindings(const ControlsConfig &,const ControlsConfig &);

// UI-thread only, driven by panel-owned events (never polls/injects OS input).
// Observe while idle so activation keys/buttons must be released before arming.
class BindingCapture {
  public:
    void begin(Action);
    void cancel();
    void cancel_mouse(Uint8 button); // Consume the Cancel click and its matching up.
    void reset(); // Focus/window/visibility transition: discard stale UI state.
    bool active() const { return action_.has_value(); }
    bool armed() const { return armed_; }
    std::optional<Action> action() const { return action_; }
    const std::string &error() const { return error_; }
    // true means no ImGui/backend forwarding. A successful capture is only a
    // copied candidate; persistence and runtime replacement happen after UI lock.
    bool event(const SDL_Event &,ControlsConfig &);
    bool take_completed();
  private:
    std::optional<Action> action_;
    bool armed_=false,completed_=false;
    std::string error_;
    std::set<SDL_Keycode> keys_,swallow_keys_;
    std::set<Uint8> buttons_,swallow_buttons_;
};
} // namespace nova::input
