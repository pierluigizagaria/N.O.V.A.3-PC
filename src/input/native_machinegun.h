#pragma once
#include "input/native_adapter.h"
#include "input/native_target.h"
#include <algorithm>
#include <limits>

namespace nova::input {
// The parent may be a static attachment. Only the native vehicle predicate
// (parent with CarPhysics) excludes it; a weapon ID is not a mode discriminator.
inline bool stationary_machinegun(bool parent_car_physics, bool jeep_enabled, bool exiting) {
    return !parent_car_physics && !jeep_enabled && !exiting;
}
struct MachineGunOwner {
    uint32_t level = 0, component = 0, normal = 0, active_interface = 0, scheme = 0;
    bool normal_enabled = false, mech_enabled = false, ingame = false;
};
inline bool machinegun_owner_matches(InputTarget target,uint32_t level,uint32_t normal,uint32_t scheme,
                                     MachineGunOwner now) {
    return target.kind == TargetKind::machinegun && target.component && level && normal && scheme &&
        now.level == level && now.component == target.component && now.normal == normal &&
        now.active_interface == normal && now.scheme == scheme && !now.normal_enabled && !now.mech_enabled && now.ingame;
}
inline bool machinegun_look_available(int duration, int delay, int dt) {
    // UpdateFromInput precedes Update's delay decrement. Positive duration and
    // expired delay select native LookAt interpolation, including its last step.
    return duration <= 0 || int64_t(delay) - int64_t(dt) > 0;
}
inline Look machinegun_mouse_delta(float dx,float dy,float pitch,float minimum,float maximum,
                                   const MouseLookConfig &config) {
    auto delta = look_delta(dx,dy,0,std::numeric_limits<float>::max(),config);
    delta.pitch = std::clamp(pitch + delta.pitch,minimum,maximum) - pitch;
    return delta;
}
struct MachineGunActions {
    bool primary = false, secondary = false, input_dirty = false;
    bool weapon_owned = false, manager_owned = false;
};
struct MachineGunCleanup {
    bool enabled = false, exiting = false, input_valid = false;
    bool weapon_matches = false, manager_matches = false;
};
template<class Live, class Event>
void apply_machinegun_actions(const FrameInput &input,bool primary_enabled,bool secondary_enabled,
                              MachineGunActions &state,Live live,Event event) {
    const bool primary = primary_enabled && input.requested(Action::fire);
    const bool secondary = secondary_enabled && input.requested(Action::aim);
    if (primary != state.primary && live()) {
        event(primary ? 29 : 30);
        state.primary = primary; state.input_dirty = true;
        if (primary) state.weapon_owned = state.manager_owned = true;
    }
    if (secondary != state.secondary && live()) {
        event(secondary ? 31 : 32);
        state.secondary = secondary; state.input_dirty = true;
    }
}
template<class Call>
void observe_machinegun_release(bool &owned,uint32_t cached,uint32_t actual,uint32_t channel,Call call) {
    call();
    if (cached && cached == actual && channel == 0) owned = false;
}
template<class Reset, class Stop, class Release>
void release_machinegun_actions(MachineGunActions &state,MachineGunCleanup live,
                                Reset reset,Stop stop,Release release) {
    // Exit owns the pitch target/delta and its eventual Disable owns both stops.
    // ResetControls here would destroy the native exit animation's input.
    if (live.enabled && !live.exiting) {
        if (state.input_dirty && live.input_valid) reset();
        if (state.weapon_owned && live.weapon_matches) stop();
        if (state.manager_owned && live.manager_matches) release();
    }
    state = {};
}
}
