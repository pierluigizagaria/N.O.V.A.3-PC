#pragma once
#include "input/native_driver.h"
#include <array>

namespace nova::input {
inline InputTarget jeep_camera_identity(InputTarget target) {
    // Weapons and animation do not own the controller's temporary raw words.
    target.jeep.weapon = 0;
    target.jeep.animation_actor = target.jeep.animation_states = target.jeep.animation_idle = 0;
    target.jeep.animation_slot = target.jeep.animation_enabled = 0;
    return target;
}
inline InputTarget driver_cleanup_identity(InputTarget target) {
    target.jeep = {};
    target.driver.capabilities &= 1; // Shooter loss does not tear down driving.
    return target;
}
template<class Read,class Byte,class Configuration>
InputTarget identify_jeep(uint32_t level,uint32_t component,Read read,Byte byte,Configuration configuration,bool weapons = true) {
    if (!level || !component || !byte(component,0xd)) return {};
    auto result = identify_driver(level,component,read,byte,configuration);
    if (!result) {
        // Independently proved shooter entry also calls SetCarComponent, whose
        // unconditional +0x25c backlink is the camera caller for both roles.
        if (byte(component,0x1ac) || !byte(component,0x1ad)) return {};
        const auto vehicle = read(component,0x17c), actor = read(component,4), input_template = read(component,0x108);
        const auto normal = read(level,0x17c), scheme = normal ? read(normal,0xc) : 0;
        if (!vehicle || !actor || !input_template || !normal || !scheme ||
            read(vehicle,0x25c) != component || !read(vehicle,4)) return {};
        const auto type = configuration(component);
        if (type > 3) return {};
        result = {TargetKind::driver,component,{vehicle,actor,input_template,2,type,normal,scheme,
            read(normal,0x28),read(normal,0x2c)}};
    }
    const auto vehicle = result.driver.vehicle;
    result.jeep = {read(vehicle,4),read(component,0x11c),read(component,0x114),read(component,0x174),
        read(component,0x100),read(component,0x104)};
    if (!result.jeep.camera || !result.jeep.pivot || !result.jeep.camera_owner ||
        result.jeep.yaw_binding != result.driver.actor+0x68 || result.jeep.pitch_binding != result.driver.actor+0x60) return {};
    if (result.driver.capabilities & 2) {
        auto &r = result.jeep;
        r.shooter_actor = read(vehicle,0x16c);
        if (r.shooter_actor != result.driver.actor) return {};
        if (!weapons) return result;
        r.weapon = read(vehicle,0x1e0);
        r.animation_actor = read(vehicle,0x228);
        r.animation_states = r.animation_actor ? read(r.animation_actor,0x138) : 0;
        r.animation_idle = read(vehicle,0x230); r.animation_slot = read(vehicle,0x244);
        r.animation_enabled = r.animation_actor ? byte(r.animation_actor,0x158) : 0;
        if (!r.weapon) return {};
    }
    return result;
}
inline bool jeep_owner_matches(InputTarget target,uint32_t level,DriverOwner now) {
    // Keep the accepted driver predicate intact; only the shooter-only
    // capability requires this parallel expression of the same owner guards.
    if (target.driver.capabilities & 1) return driver_owner_matches(target,level,now);
    return target.kind == TargetKind::driver && target.component && target.driver.capabilities == 2 &&
        target.driver.vehicle && target.driver.actor && target.driver.input_template && target.driver.normal && target.driver.scheme &&
        now.target == target && level && now.level == level && now.normal == target.driver.normal &&
        now.active_interface == now.normal && now.scheme == target.driver.scheme &&
        !now.normal_enabled && !now.mech_enabled && !now.machinegun_enabled && now.ingame;
}
template<class Read,class Byte>
bool jeep_controller_matches(InputTarget target,uint32_t local_actor,Read read,Byte byte) {
    const auto actor = target.driver.actor, vehicle = target.driver.vehicle, component = target.component;
    return target.kind == TargetKind::driver && component && actor && vehicle && local_actor == actor &&
        byte(component,0xd) && read(component,4) == actor && read(component,0x17c) == vehicle &&
        read(vehicle,0x25c) == component && read(vehicle,4) == target.jeep.vehicle_actor &&
        read(component,0x100) == actor+0x68 && read(component,0x104) == actor+0x60 &&
        target.jeep.yaw_binding == actor+0x68 && target.jeep.pitch_binding == actor+0x60;
}
template<class Read>
bool jeep_weapon_matches(InputTarget target,Read read) {
    return target.jeep.weapon && read(target.driver.vehicle,0x1e0) == target.jeep.weapon;
}
template<class Read,class Byte>
bool jeep_animation_matches(InputTarget target,Read read,Byte byte) {
    const auto vehicle = target.driver.vehicle; const auto &r = target.jeep;
    return r.animation_actor && read(vehicle,0x228) == r.animation_actor &&
        read(r.animation_actor,0x138) == r.animation_states && read(vehicle,0x230) == r.animation_idle &&
        read(vehicle,0x244) == r.animation_slot && byte(r.animation_actor,0x158) == r.animation_enabled;
}
// One original button edge, scoped to one normal-interface Update. The native
// producer owns vehicle-list lookup, role IDs, server rebind and client requests.
struct JeepRoleEdge {
    InputTarget owner;
    uint32_t level=0, driver_button=0, shooter_button=0;
    uint64_t epoch=0;
    unsigned role=2; // 0 driver, 1 shooter; 2 means no pending edge.
};
// Accepted J01 slices03/04: the two virtual Pressed calls in normal Update.
inline bool jeep_role_query_matches(uint32_t update,uint32_t returned,unsigned role) {
    return update && returned>=update && role<2 &&
        returned-update==(role==0 ? 0x74du : 0x64du);
}
template<class Read>
void offer_jeep_role_edge(JeepRoleEdge &edge,const FrameInput &input,InputTarget owner,uint32_t level,Read read) {
    edge={};
    const bool driver=input.edge(Action::slot1), shooter=input.edge(Action::slot2);
    // Provisional: simultaneous role edges are ambiguous; request neither.
    if(driver==shooter || owner.kind!=TargetKind::driver || !owner.component || !level || !owner.driver.normal) return;
    const auto first=read(owner.driver.normal,0x24), second=read(owner.driver.normal,0x20);
    if(!first || !second || first==second) return;
    edge={owner,level,first,second,input.edge_epoch,driver ? 0u : 1u};
}
template<class Live,class Read,class Enabled>
bool consume_jeep_role_edge(JeepRoleEdge &edge,uint32_t button,Live live,Read read,Enabled enabled) {
    if(edge.role>1) return false;
    const auto request=edge;
    const auto wanted=request.role==0 ? request.driver_button : request.shooter_button;
    if(button!=wanted) return false; // Other native button queries keep their behavior.
    edge={}; // Consume before guest queries/reentry, including refusal; never retry.
    // Live proves the full current jeep identity before any retained pointers
    // are read, including same-address role/resource/configuration changes.
    if(!live(request.owner,request.level,request.epoch)) return false;
    const auto normal=request.owner.driver.normal;
    if(read(normal,0x24)!=request.driver_button || read(normal,0x20)!=request.shooter_button) return false;
    const auto vehicle=request.owner.driver.vehicle, actor=request.owner.driver.actor;
    if(!vehicle || !actor) return false;
    const auto driver=read(vehicle,0x168), shooter=read(vehicle,0x16c);
    const bool available=request.role==0 ? shooter==actor && !driver : driver==actor && !shooter;
    return available && enabled(button) && live(request.owner,request.level,request.epoch);
}
template<class Call>
void jeep_role_input_boundary(JeepRoleEdge &edge,Call call) {
    struct Discard { JeepRoleEdge &edge; ~Discard() { edge={}; } } discard{edge};
    call(); // Unqueried edges also expire, including an exceptional native return.
}
struct JeepFireActions {
    std::array<bool,2> held{}, weapon_owned{}, animation_owned{};
    bool input_dirty = false;
};
template<class Live,class Event>
void apply_jeep_fire(const FrameInput &input,bool shooter,bool primary,bool secondary,
                     JeepFireActions &state,Live live,Event event) {
    const std::array<bool,2> requested{shooter && primary && input.requested(Action::fire),
                                      shooter && secondary && input.requested(Action::aim)};
    for (unsigned channel=0; channel<2; ++channel) if (requested[channel] != state.held[channel] && live()) {
        event(29+2*channel+unsigned(!requested[channel]));
        state.held[channel] = requested[channel]; state.input_dirty = true;
        if (requested[channel]) state.weapon_owned[channel] = state.animation_owned[channel] = true;
    }
}
// Identity and the exact native API return are independent requirements. A
// reset, an Update return or one resource's completion cannot acknowledge another.
template<class Call,class Matches>
void observe_jeep_release(JeepFireActions &state,unsigned channel,bool animation,Call call,Matches matches) {
    call();
    if (channel < 2 && matches()) (animation ? state.animation_owned : state.weapon_owned)[channel] = false;
}
template<class Controller,class Reset,class Weapon,class Animation,class Stop,class StopAnimation>
void release_jeep_fire(JeepFireActions &state,Controller controller,Reset reset,Weapon weapon,
                       Animation animation,Stop stop,StopAnimation stop_animation) {
    if (controller()) {
        if (state.input_dirty) reset();
        for (unsigned channel=0;channel<2;++channel) {
            // Each callback rechecks live level/getter/enabled/controller and
            // its own resource after every prior guest call.
            if (state.weapon_owned[channel] && controller() && weapon()) stop(channel);
            if (state.animation_owned[channel] && controller() && animation()) stop_animation(channel);
        }
    }
    state = {};
}
template<class Cleanup,class Prepare,class Consume>
void jeep_fire_boundary(bool selected,bool eligible,bool secondary_available,JeepFireActions &state,
                        Cleanup cleanup,Prepare prepare,Consume consume) {
    if (selected) {
        // HasSecondaryFire also guards the native release branch. Losing it
        // cannot be inferred to have completed a still-owned channel stop.
        if (!eligible || (state.weapon_owned[1] && !secondary_available)) cleanup();
        else prepare();
    }
    consume();
}
// The eligible role buttons use the same draw-only mask as on foot. Keep the
// accepted driver exit/pedal helper and all native Draw/input effects intact.
template<class Control,class Visible,class Set,class Draw>
void draw_jeep_controls(bool scope,Control control,Visible visible,Set set,Draw draw) {
    std::map<uint32_t,uint8_t> saved;
    if(scope) for(auto slot : {0x20u,0x24u}) {
        const auto button=control(slot);
        if(button) saved.try_emplace(button,visible(button));
    }
    with_hidden_controls(saved,set,[&] { draw_driver_controls(scope,control,visible,set,draw); });
}
inline bool jeep_touch_control(InputTarget target,uint32_t drawing,uint32_t move,uint32_t rotate,
                                uint32_t primary,uint32_t secondary) {
    return driver_touch_control(drawing,move,rotate) ||
        ((target.driver.capabilities & 2) && drawing && (drawing == primary || drawing == secondary));
}
template<class Control,class Visible,class Set,class Draw>
void draw_jeep_scheme(bool scope,bool shooter,Control control,Visible visible,Set set,Draw draw) {
    if (!scope || !shooter) { draw_driver_scheme(scope,control,visible,set,draw); return; }
    std::map<uint32_t,uint8_t> saved;
    for (auto slot : {0x28u,0x2cu,0x30u,0x34u}) {
        const auto button = control(slot);
        if (button) saved.try_emplace(button,visible(button));
    }
    with_hidden_controls(saved,set,draw);
}
}
