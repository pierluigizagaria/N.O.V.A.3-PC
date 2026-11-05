#pragma once
#include "input/native_adapter.h"
#include "input/native_target.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>

namespace nova::input {
// Exact jeep-specific fields, shared with synthetic layout rejection tests.
// Callers prove current level/getter lifetime before reading component memory.
template<class Read,class Byte,class Configuration>
InputTarget identify_driver(uint32_t level,uint32_t component,Read read,Byte byte,Configuration configuration) {
    if (!level || !component || !byte(component,0xd)) return {};
    const auto vehicle = read(component,0x17c), actor = read(component,4), input_template = read(component,0x108);
    const auto normal = read(level,0x17c), scheme = normal ? read(normal,0xc) : 0;
    const uint32_t capabilities = uint32_t(byte(component,0x1ac) != 0) | (uint32_t(byte(component,0x1ad) != 0) << 1);
    if (!(capabilities & 1) || !vehicle || !actor || !input_template || !normal || !scheme ||
        read(vehicle,0x25c) != component || !read(vehicle,4)) return {};
    const auto type = configuration(component);
    if (type > 3) return {};
    return {TargetKind::driver,component,{vehicle,actor,input_template,capabilities,type,normal,scheme,
        read(normal,0x28),read(normal,0x2c)}};
}
template<class Identify>
bool driver_lifetime_matches(InputTarget old,uint32_t previous_level,uint32_t level,uint32_t local_component,Identify identify) {
    return old.kind == TargetKind::driver && old.component && level && level == previous_level &&
        local_component == old.component && identify() == old;
}
struct DriverOwner {
    InputTarget target;
    uint32_t level = 0, normal = 0, active_interface = 0, scheme = 0;
    bool normal_enabled = false, mech_enabled = false, machinegun_enabled = false, ingame = false;
};
inline bool driver_owner_matches(InputTarget old,uint32_t level,DriverOwner now) {
    return old.kind == TargetKind::driver && old.component && (old.driver.capabilities & 1) &&
        old.driver.vehicle && old.driver.actor && old.driver.input_template && old.driver.normal && old.driver.scheme &&
        now.target == old && level && now.level == level && now.normal == old.driver.normal &&
        now.active_interface == now.normal && now.scheme == old.driver.scheme &&
        !now.normal_enabled && !now.mech_enabled && !now.machinegun_enabled && now.ingame;
}
struct DriverActions { bool moving = false; };
template<class Live,class Event>
void apply_driver_actions(const FrameInput &input,bool enabled,unsigned axes,DriverActions &state,Live live,Event event) {
    const float x = enabled && (axes & 1) ? float(input.down(Action::right))-float(input.down(Action::left)) : 0;
    const float y = enabled && (axes & 2) ? float(input.down(Action::back))-float(input.down(Action::forward)) : 0;
    if (!live()) return;
    if (x || y) { event(13,x,y); state.moving = true; }
    else if (state.moving) { event(14,0,0); state.moving = false; }
}
template<class Release>
void release_driver_actions(DriverActions &state,bool same_enabled_context,Release release) {
    if (state.moving && same_enabled_context) release();
    state = {};
}
// Invert only the conversion from PC counts to the native raw input. The
// original consumer still evaluates its acceleration branch and camera limits.
inline double inverse_driver_gain(double value,bool accelerated,double coefficient) {
    if (!accelerated || coefficient <= 0) return value;
    const double lower = std::max(1/coefficient-1,0.0);
    const double upper = std::max(10/coefficient-1,0.0);
    if (value <= lower) return value;
    if (value >= 10*upper) return value/10;
    const double q = value/coefficient;
    return 2*q/(std::sqrt(1+4*q)+1);
}
inline std::optional<Look> driver_mouse_input(float dx,float dy,float scale_x,float scale_y,
                                              bool accelerated,float gain,const MouseLookConfig &config) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(scale_x) || !std::isfinite(scale_y) ||
        (accelerated && !std::isfinite(gain))) return {};
    const double x = scale_x ? double(dx)*config.sensitivity/scale_x : 0;
    const double y = scale_y ? double(dy)*config.sensitivity*(config.invert_y ? -1.0 : 1.0)/scale_y : 0;
    const double raw_x = std::copysign(inverse_driver_gain(std::abs(x),accelerated,gain),x);
    if (!std::isfinite(raw_x) || !std::isfinite(y) || std::abs(raw_x)>std::numeric_limits<float>::max() ||
        std::abs(y)>std::numeric_limits<float>::max()) return {};
    return Look{static_cast<float>(raw_x),static_cast<float>(y)};
}
// Preserve native writes (including its one-shot zero) and lifetime/context
// changes. Never restore an old input word over a new native owner's input.
template<class Live,class Read,class Write,class Consume>
void driver_mouse_boundary(bool &pending,bool eligible,std::optional<Look> sample,Live live,
                            Read read,Write write,Consume consume) {
    if (!pending || !eligible || !sample) { consume(); return; }
    const auto before = read();
    const auto supplied = *sample;
    write(supplied); pending = false;
    auto restore = [&] {
        if (!live()) return;
        const auto after = read();
        Look result = after;
        if (std::bit_cast<uint32_t>(after.yaw) == std::bit_cast<uint32_t>(supplied.yaw)) result.yaw = before.yaw;
        if (std::bit_cast<uint32_t>(after.pitch) == std::bit_cast<uint32_t>(supplied.pitch)) result.pitch = before.pitch;
        write(result);
    };
    try { consume(); } catch (...) { restore(); throw; }
    restore();
}
template<class Live>
std::optional<uint32_t> driver_pedal_input(InputTarget owner,uint32_t button,bool active,bool release,
                                           bool enabled,const FrameInput &input,Live live) {
    if (owner.kind != TargetKind::driver || owner.driver.configuration != 1 || !button ||
        (button != owner.driver.accelerator && button != owner.driver.brake)) return {};
    if ((!active && !release) || !live()) return {};
    if (release) return 0;
    return enabled && input.down(button == owner.driver.accelerator ? Action::forward : Action::back);
}
struct DriverPedalRelease {
    InputTarget owner;
    uint32_t level = 0;
    bool owned = false, pending = false;
};
template<class Live,class Consume,class Neutral>
void driver_pedal_boundary(DriverPedalRelease &state,Live live,Consume consume,Neutral neutral) {
    const auto owner = state.owner;
    const auto level = state.level;
    consume();
    // This is completion of the actual input-timer consumer, not a weapon-stop
    // inference from reset bytes or the unrelated camera hook returning.
    if (state.owned && state.owner == owner && state.level == level && live() && neutral()) state = {};
}
inline bool driver_touch_control(uint32_t drawing,uint32_t move,uint32_t rotate) {
    return drawing && (drawing == move || drawing == rotate);
}
template<class Control,class Visible,class Set,class Draw>
void draw_driver_scheme(bool scope,Control control,Visible visible,Set set,Draw draw) {
    std::map<uint32_t,uint8_t> saved;
    if (scope) for (auto slot : {0x28u,0x2cu}) {
        const auto button = control(slot);
        if (button) saved.try_emplace(button,visible(button));
    }
    with_hidden_controls(saved,set,draw);
}
template<class Control,class Visible,class Set,class Draw>
void draw_driver_controls(bool scope,Control control,Visible visible,Set set,Draw draw) {
    if (!scope) { draw(); return; }
    std::map<uint32_t,uint8_t> saved;
    for (auto slot : {0x10u,0x28u,0x2cu}) {
        const auto button = control(slot);
        if (button) saved.try_emplace(button,visible(button));
    }
    with_hidden_controls(saved,set,draw);
}
}
