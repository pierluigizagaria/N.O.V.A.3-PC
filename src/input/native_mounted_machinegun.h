#pragma once
#include "input/native_machinegun.h"

namespace nova::input {
// T07f: ordinary local entry reserves AI to the player and parents the actor
// to the gun. The gun's parent may independently own CarPhysics or a remote Jeep.
template<class Read,class Byte,class Component>
InputTarget identify_machinegun(uint32_t level,uint32_t component,uint32_t player,
                                 Read read,Byte byte,Component get) {
    if (!level || !component || !player || !byte(component,0xd) || byte(component,0x22c)) return {};
    InputTarget result{TargetKind::machinegun,component}; auto &r=result.machinegun;
    r.normal_player=read(component,0x238); r.actor=read(component,4); r.gun=read(component,0x118);
    if (r.normal_player!=player || !r.actor || read(player,4)!=r.actor || !r.gun || read(r.actor,0x38)!=r.gun) return {};
    r.parent=read(r.gun,0x38); r.car=r.parent ? get(r.parent,0x5e) : 0;
    r.ai=read(component,0x11c); r.weapon=read(component,0x120); r.manager=read(player,0x170);
    if (!r.ai || !r.weapon || !r.manager || get(r.gun,0x43)!=r.ai || get(r.gun,0x47)!=r.weapon ||
        read(r.ai,4)!=r.gun || read(r.ai,0x78)!=r.actor) return {};
    r.ai_template=read(r.ai,0x24); r.input_template=read(component,0xfc);
    r.yaw_binding=read(component,0x100); r.pitch_binding=read(component,0x104);
    r.body_node=read(r.ai,0x44); r.pivot=read(component,0x108);
    r.camera=read(component,0x110); r.camera_owner=read(component,0x124);
    r.normal=read(level,0x17c); r.scheme=r.normal ? read(r.normal,0xc) : 0;
    if (!r.ai_template || !r.input_template || r.yaw_binding!=r.ai+0x54 || r.pitch_binding!=r.ai+0x58 ||
        !r.body_node || !r.pivot || !r.camera || !r.camera_owner || !r.normal || !r.scheme) return {};
    return result;
}
// Full selection changes on reparent/rebind, but the same local controller may
// still own queued input. Native ResetControls can safely rebase CURRENT valid
// AI bindings without writing the AI angles or assuming resource teardown.
template<class Read,class Byte>
bool machinegun_root_matches(InputTarget target,uint32_t level,uint32_t player,Read read,Byte byte) {
    const auto c=target.component; const auto &r=target.machinegun;
    return target.kind==TargetKind::machinegun && c && level && player && player==r.normal_player &&
        byte(c,0xd) && !byte(c,0x22c) && read(c,0x238)==player && read(c,4)==r.actor &&
        r.actor && read(player,4)==r.actor && read(level,0x17c)==r.normal &&
        read(level,0x178)==r.normal && r.normal && read(r.normal,0xc)==r.scheme;
}
template<class Read,class Component>
uint32_t machinegun_current_ai(InputTarget target,Read read,Component get) {
    const auto c=target.component; const auto gun=read(c,0x118), ai=read(c,0x11c);
    if (!gun || !ai || get(gun,0x43)!=ai || read(c,0x118)!=gun || read(c,0x11c)!=ai) return 0;
    return read(ai,4)==gun && read(ai,0x78)==target.machinegun.actor ? ai : 0;
}
template<class Read,class Byte,class Component>
bool machinegun_structure_matches(InputTarget target,uint32_t level,uint32_t player,
                                    Read read,Byte byte,Component get) {
    return machinegun_root_matches(target,level,player,read,byte) && machinegun_current_ai(target,read,get) &&
        machinegun_root_matches(target,level,player,read,byte);
}
template<class Read,class Component>
bool machinegun_controller_matches(InputTarget target,Read read,Component get) {
    const auto ai=machinegun_current_ai(target,read,get); const auto c=target.component;
    return ai && read(c,0x100)==ai+0x54 && read(c,0x104)==ai+0x58;
}
template<class Read,class Component>
bool machinegun_weapon_matches(InputTarget target,Read read,Component get) {
    const auto c=target.component; const auto gun=read(c,0x118), weapon=target.machinegun.weapon;
    return gun && weapon && read(c,0x120)==weapon && get(gun,0x47)==weapon &&
        read(c,0x118)==gun && read(c,0x120)==weapon;
}
template<class Structure,class Controller,class Reset,class Weapon,class Stop,class Manager,class Release>
void release_owned_machinegun(MachineGunActions &state,Structure structure,Controller controller,Reset reset,
                               Weapon weapon,Stop stop,Manager manager,Release release) {
    if (state.input_dirty && structure() && controller()) {
        reset();
        // Reset may synchronously change native ownership. Query it again;
        // subsequent resource operations each make their own fresh decision.
        (void)controller();
    }
    if (state.weapon_owned && structure() && weapon()) stop();
    if (state.manager_owned && structure() && manager()) release();
    state={};
}
template<class Call,class Matches>
void observe_owned_machinegun_release(bool &owned,bool expected,Call call,Matches matches) {
    call();
    if (expected && matches()) owned=false;
}
template<class Cleanup,class Consume>
void machinegun_action_boundary(bool selected,bool eligible,Cleanup cleanup,Consume consume) {
    if (selected && !eligible) cleanup();
    consume(); // Native exit/parent/camera and Draw side effects remain native.
}
inline bool mounted_machinegun(InputTarget target) {
    return target.kind==TargetKind::machinegun && target.machinegun.car;
}
inline bool mounted_machinegun_touch(uint32_t drawing,uint32_t move,uint32_t rotate,uint32_t primary,uint32_t secondary) {
    return drawing && (drawing==move || drawing==rotate || drawing==primary || drawing==secondary);
}
template<class Control,class Visible,class Set,class Draw>
void draw_mounted_machinegun_scheme(bool scope,Control control,Visible visible,Set set,Draw draw) {
    if (!scope) { draw(); return; }
    std::map<uint32_t,uint8_t> saved;
    for (auto slot : {0x28u,0x2cu,0x30u,0x34u}) {
        const auto button=control(slot); if (button) saved.try_emplace(button,visible(button));
    }
    with_hidden_controls(saved,set,draw);
}
}

namespace nova::input {
template<class Control,class Visible,class Set,class Draw>
void draw_mounted_machinegun_interface(bool scope,Control control,Visible visible,Set set,Draw draw) {
    if (!scope) { draw(); return; }
    const auto exit=control(0x10);
    std::map<uint32_t,uint8_t> saved;
    if (exit) saved.emplace(exit,visible(exit));
    with_hidden_controls(saved,set,draw);
}
}
