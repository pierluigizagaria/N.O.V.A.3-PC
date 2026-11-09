#pragma once
#include "input/controls.h"
#include <cstdint>
#include <map>

namespace nova::input {
// `driver` is the historical name of the single jeep owner. Its independent
// capabilities determine which actions are supported; shooter is not a target.
enum class TargetKind { none, normal, mech, machinegun, driver, bike };
// Driver and shooter are independent native capabilities. A vehicle/role/config
// change can keep the component address, so all of this context is identity.
struct DriverIdentity {
    uint32_t vehicle = 0, actor = 0, input_template = 0, capabilities = 0;
    uint32_t configuration = 0, normal = 0, scheme = 0;
    uint32_t accelerator = 0, brake = 0;
    bool operator==(const DriverIdentity &) const = default;
};
struct JeepResources {
    uint32_t vehicle_actor = 0, camera = 0, pivot = 0, camera_owner = 0;
    uint32_t yaw_binding = 0, pitch_binding = 0, weapon = 0, shooter_actor = 0;
    uint32_t animation_actor = 0, animation_states = 0, animation_idle = 0, animation_slot = 0, animation_enabled = 0;
    bool operator==(const JeepResources &) const = default;
};
struct MachineGunIdentity {
    uint32_t normal_player = 0, actor = 0, gun = 0, parent = 0, car = 0;
    uint32_t ai = 0, ai_template = 0, input_template = 0, yaw_binding = 0, pitch_binding = 0;
    uint32_t body_node = 0, pivot = 0, camera = 0, camera_owner = 0;
    uint32_t normal = 0, scheme = 0, weapon = 0, manager = 0;
    bool operator==(const MachineGunIdentity &) const = default;
};
struct BikeIdentity {
    uint32_t player = 0, actor = 0, physics = 0, base = 0, config = 0, input_template = 0, manager = 0;
    uint32_t interface_ = 0, scheme = 0, normal = 0, move = 0, primary = 0;
    uint32_t pivot = 0, camera = 0, saved_camera = 0, scene = 0, camera_owner = 0, visual = 0;
    uint32_t animation = 0, auxiliary = 0, rider = 0, steering = 0, muzzle1 = 0, muzzle2 = 0;
    uint64_t generation = 0;
    bool operator==(const BikeIdentity &) const = default;
};
struct InputTarget {
    TargetKind kind = TargetKind::none;
    uint32_t component = 0;
    DriverIdentity driver{};
    JeepResources jeep{};
    MachineGunIdentity machinegun{};
    BikeIdentity bike{};
    bool operator==(const InputTarget &) const = default;
    explicit operator bool() const { return component != 0; }
};
struct TargetCandidate {
    uint32_t component = 0;
    bool enabled = false, alive = false, interactive = false;
    DriverIdentity driver{};
    JeepResources jeep{};
    MachineGunIdentity machinegun{};
    BikeIdentity bike{};
};
inline InputTarget select_input_target(bool context, TargetCandidate normal, TargetCandidate mech,
                                       TargetCandidate machinegun = {}, TargetCandidate driver = {}, TargetCandidate bike = {}) {
    if (!context || unsigned(normal.enabled) + unsigned(mech.enabled) + unsigned(machinegun.enabled) + unsigned(driver.enabled) + unsigned(bike.enabled) != 1) return {};
    auto candidate = normal.enabled ? normal : mech.enabled ? mech : machinegun.enabled ? machinegun : driver.enabled ? driver : bike;
    if (!candidate.component || !candidate.alive || !candidate.interactive) return {};
    return {normal.enabled ? TargetKind::normal : mech.enabled ? TargetKind::mech :
            machinegun.enabled ? TargetKind::machinegun : driver.enabled ? TargetKind::driver : TargetKind::bike,
            candidate.component,candidate.driver,candidate.jeep,candidate.machinegun,candidate.bike};
}
inline bool normal_release_can_broadcast(InputTarget old,bool normal,bool mech,bool machinegun,bool driver,bool bike,bool has_next) {
    return old.kind==TargetKind::normal && normal && !mech && !machinegun && !driver && !bike && !has_next;
}
inline bool look_target_matches(InputTarget selected, TargetKind kind, uint32_t component, bool enabled) {
    return enabled && selected.kind == kind && selected.component == component && component;
}
inline bool live_input_target(InputTarget old, uint32_t previous_level, uint32_t level,
                              uint32_t normal, uint32_t mech, uint32_t machinegun = 0, uint32_t driver = 0, uint32_t bike = 0) {
    return level && level == previous_level && old.component &&
        old.component == (old.kind == TargetKind::normal ? normal :
                          old.kind == TargetKind::mech ? mech :
                          old.kind == TargetKind::machinegun ? machinegun :
                          old.kind == TargetKind::driver ? driver : old.kind == TargetKind::bike ? bike : 0);
}
inline bool mech_exit_edge(bool &pending, bool enabled) {
    if (!enabled || !pending) return false;
    pending = false;
    return true;
}
// Release the old identity before publishing the new one. The release callback
// checks native lifetime; a level teardown must never dereference a stale target.
template<class Release, class Clear>
bool change_input_target(InputTarget &current, InputTarget next, Release release, Clear clear) {
    if (current == next) return false;
    if (current) release(current);
    clear();
    current = next;
    return true;
}
struct MechActions {
    bool moving = false, primary = false, secondary = false;
    bool primary_release = false, secondary_release = false;
};
struct MechControls { bool move = false, primary = false, secondary = false; };
template<class Event>
void apply_mech_actions(const FrameInput &input, MechControls enabled, MechActions &state, Event event) {
    float x = float(input.down(Action::right)) - float(input.down(Action::left));
    float y = float(input.down(Action::back)) - float(input.down(Action::forward));
    if (x && y) { x *= 0.70710678118f; y *= 0.70710678118f; }
    if ((x || y) && enabled.move) { event(13,x,y); state.moving = true; }
    else if (state.moving) { event(14,0,0); state.moving = false; }
    const bool primary = enabled.primary && input.requested(Action::fire);
    const bool secondary = enabled.secondary && input.requested(Action::aim);
    if (primary != state.primary) {
        event(primary ? 29 : 30,0,0); state.primary = primary; state.primary_release = !primary;
    }
    if (secondary != state.secondary) {
        event(secondary ? 31 : 32,0,0); state.secondary = secondary; state.secondary_release = !secondary;
    }
}
template<class Stop>
void observe_mech_stop(MechActions &state,uint32_t owned_weapon,uint32_t weapon,uint32_t channel,Stop stop) {
    stop();
    // Positive acknowledgement from the exact native release consumer, after
    // FireStop returns. ResetControls clearing a byte is not this evidence.
    if (!owned_weapon || owned_weapon != weapon) return;
    if (channel == 0) state.primary_release = false;
    if (channel == 1) state.secondary_release = false;
}
template<class Reset, class Stop>
void release_mech_actions(MechActions &state, bool native_enabled, Reset reset, Stop stop) {
    // Disable already resets/stops natively. While still enabled, reset input
    // and stop owned channels now. A release edge would call FireStop again on
    // resume, duplicating its native MP messages even when audio has stopped.
    const bool primary = state.primary || state.primary_release;
    const bool secondary = state.secondary || state.secondary_release;
    if (native_enabled && (primary || secondary || state.moving)) {
        reset();
        if (primary) stop(0);
        if (secondary) stop(1);
    }
    state = {};
}
template<class Saved, class Set, class Draw>
void with_hidden_controls(const Saved &saved, Set set, Draw draw) {
    for (auto [control,visible] : saved) { (void)visible; set(control,false); }
    try { draw(); }
    catch (...) { for (auto [control,visible] : saved) set(control,visible); throw; }
    for (auto [control,visible] : saved) set(control,visible);
}
struct NativeHudPresentation {
    bool hide_mobile=false;
    bool show_contextual=false;
};
// Presentation follows native scene/role state, not the eligible input receiver.
// During exit animations all receivers can be disabled while the HUD still draws.
inline NativeHudPresentation native_hud_presentation(bool scene,bool on_foot,bool seated) {
    return {scene,scene && on_foot && !seated};
}
template<class Draw>
void draw_native_interaction(NativeHudPresentation policy,bool local_player,Draw draw) {
    if(local_player && policy.hide_mobile && !policy.show_contextual) return;
    draw();
}
template<class Control,class Visible,class Set,class Draw>
void draw_contextual_buttons(bool hide,Control control,Visible visible,Set set,Draw draw) {
    if(!hide) { draw(); return; }
    std::map<uint32_t,uint8_t> saved;
    for(auto slot:{0x60u,0x64u}) {
        const auto button=control(slot);
        if(button) saved.try_emplace(button,visible(button));
    }
    with_hidden_controls(saved,set,draw);
}
template<class SchemeControl,class InterfaceControl,class Visible,class Set,class Draw>
void draw_interface_controls(bool scope,SchemeControl scheme_control,InterfaceControl interface_control,
                             Visible visible,Set set,Draw draw) {
    if (!scope) { draw(); return; }
    // Contextual feedback belongs to native ShowInteractInterface/Draw. Keep
    // both standard and projected artwork, including aliases in the scheme.
    // Mobile movement/combat controls below remain independently suppressed.
    const auto standard=interface_control(0x60),projected=interface_control(0x64);
    // Power selectors convey the current choice and availability on PC too.
    // Preserve aliases so hiding a touch control cannot blank their artwork.
    const auto power=scheme_control(0x44),multiplayer_power=interface_control(0x5c);
    std::map<uint32_t,uint8_t> buttons;
    auto hide = [&](uint32_t button) {
        if (button && button!=standard && button!=projected && button!=power && button!=multiplayer_power && !buttons.contains(button)) buttons[button] = visible(button);
    };
    for (uint32_t slot = 0x28; slot <= 0x44; slot += 4) hide(scheme_control(slot));
    for (auto offset : {0x10u,0x20u,0x24u,0x28u,0x2cu,0x4cu,0x5cu,0x68u,0x6cu,0x70u,0x74u,0x78u}) {
        hide(interface_control(offset));
    }
    with_hidden_controls(buttons,set,draw);
}
}
