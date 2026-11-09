#pragma once
#include <SDL3/SDL.h>
#include "input/mouse_sensitivity.h"
#include "input/gamepad_axes.h"
#include <array>
#include <atomic>
#include <bitset>
#include <filesystem>
#include <istream>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace nova::input {
enum class Action { forward, left, back, right, sprint, jump, reload, grenade, interact,
                    melee, power, fire, aim, next, previous, slot1, slot2, slot3, slot4,
                    slot5, chat, scores, previous_power, next_power, next_grenade, count };
constexpr size_t action_count = static_cast<size_t>(Action::count);
struct GamepadSample {
    uint32_t device=0;
    uint64_t generation=0;
    std::bitset<action_count> held;
    GamepadAxes axes;
    bool neutral=true;
};
const char *action_name(Action action);
SDL_Keycode key_code(const std::string &name);
struct ControlsConfig : MouseLookConfig {
    float aim_multiplier = 1.f;
    bool touch_autoaim = false;
    std::map<SDL_Keycode, Action> keys;
    std::map<Uint8, Action> buttons;
    Action wheel_up = Action::next, wheel_down = Action::previous;
    ControlsConfig();
    static ControlsConfig parse(std::istream &stream);
    static ControlsConfig read(const std::filesystem::path &path);
};
struct Look { float yaw, pitch; };
Look look_delta(float dx, float dy, float pitch, float limit, const MouseLookConfig &config);
struct ContinuousGamepadInput {
    GamepadSample sample;
    std::bitset<action_count> keyboard_held;
    uint64_t revision=0;
    bool source_present=false, admitted=false;
};
struct FrameInput {
    std::bitset<action_count> held;
    std::vector<Action> pressed;
    float dx = 0, dy = 0;
    GamepadAxes gamepad; // Persistent normalized positions; never added to dx/dy.
    uint64_t gamepad_revision=0;
    std::bitset<action_count> gamepad_only_held;
    std::vector<Action> gamepad_only_pressed;
    bool gamepad_edge(Action a) const {
        for(auto edge : gamepad_only_pressed) if(edge==a) return true;
        return false;
    }
    uint64_t edge_epoch=0; // Revocation snapshot for sampled one-shot commands.
    bool down(Action a) const { return held.test(static_cast<size_t>(a)); }
    bool requested(Action a) const { return down(a) || edge(a); }
    bool edge(Action a) const {
        for (auto pressed_action : pressed) if (a == pressed_action) return true;
        return false;
    }
};
bool input_event(Uint32 type);
bool permit_input(bool scripted_session, bool allow_physical, bool script_source, Uint32 type);
bool capture_policy(bool gameplay, bool focused, bool text_entry);
// UI produces input, GL consumes it. State transitions discard stale presses/deltas.
class Controls {
  public:
    explicit Controls(ControlsConfig config) : config(std::move(config)),
        sensitivity_{this->config.sensitivity,this->config.aim_multiplier} {}
    ControlsConfig config; // Startup scalars stay immutable; runtime maps use replace_bindings.
    ControlsConfig snapshot_config() const;
    void replace_bindings(const ControlsConfig &);
    MouseLookConfig look_config(float native_factor=1.f, bool native_aim=false) const;
    void replace_sensitivity(MouseSensitivity);
    std::atomic<bool> gameplay{false};
    bool handle(const SDL_Event &event);
    void context(bool focused, bool text_entry);
    void overlay(bool visible) { overlay_=visible; if(visible) { cancel_panel_holds(); clear(); clear_gamepad(); } }
    bool active() const {
        return !overlay_.load() && capture_policy(gameplay.load(),focused_.load(),text_.load());
    }
    bool pointer_capture() const { return active() && !communication_panel_.load(); }
    bool communication_panel() const { return communication_panel_.load(); }
    void communication_panel(bool visible) { communication_panel_=visible; }
    // Keep physically held movement through opening/closing communication UI.
    void clear_except_movement();
    void clear();
    uint64_t edge_epoch() const { return edge_epoch_.load(); }
    void clear_gamepad(); // Revoke only this source; keyboard/mouse stay owned.
    void submit_gamepad(const GamepadSample &);
    // Non-consuming continuous snapshot: no mouse counts or one-shot edges.
    ContinuousGamepadInput continuous_gamepad() const;
    bool gamepad_current(uint64_t revision) const;
    uint64_t gamepad_revision() const;
    bool revoke_stale_gamepad(FrameInput &) const;
    void bind_gamepad_receiver(bool supported,bool terminal);
    FrameInput take();
    // Drain one command before native gameplay consumers take the frame input.
    bool take_pressed(Action action);
    bool panel_held(Action action) const;
  private:
    void button(Action action, bool down);
    void cancel_panel_holds();
    mutable std::mutex mutex_;
    MouseSensitivity sensitivity_;
    FrameInput pending_;
    std::atomic<uint64_t> edge_epoch_{0};
    GamepadSample gamepad_;
    std::vector<Action> gamepad_pressed_;
    bool gamepad_rearm_=true, gamepad_receiver_supported_=true;
    uint64_t gamepad_revision_=1;
    std::set<SDL_Keycode> keys_down_;
    std::set<Uint8> buttons_down_;
    // Physical ownership survives ordinary gameplay-to-modal clear().
    std::map<SDL_Keycode,Action> panel_keys_;
    std::map<Uint8,Action> panel_buttons_;
    std::set<SDL_Keycode> rearm_keys_;
    std::set<Uint8> rearm_buttons_;
    bool bound_down(Action action) const;
    std::atomic<bool> focused_{true}, text_{false};
    std::atomic<bool> overlay_{false};
    std::atomic<bool> communication_panel_{false};
};
}
