#pragma once
#include "input/native_target.h"
#include <atomic>

namespace nova::input {
// Bike has no native relative-look consumer. State 0 accepts lateral input and
// primary presses; its speed, road following, camera and targeting stay native.
template<class Read,class Byte,class Component,class Control>
InputTarget identify_bike(uint32_t level,uint32_t component,uint32_t player,uint64_t generation,
                          Read read,Byte byte,Component get,Control control) {
    if (!level || !component || !player || !byte(component,0xd) || read(component,0x120)!=0) return {};
    InputTarget result{TargetKind::bike,component}; auto &r=result.bike;
    r.player=player; r.actor=read(component,4);
    if (!r.actor || read(player,4)!=r.actor || get(r.actor,0x2e)!=component) return {};
    r.normal=read(level,0x17c); r.interface_=read(level,0x180);
    if (!r.normal || !r.interface_ || read(level,0x178)!=r.interface_) return {};
    r.scheme=read(r.interface_,0xc); if (!r.scheme) return {};
    r.physics=read(component,0x108); r.base=read(component,0x128);
    if (!r.physics || !r.base || get(r.actor,0x2a)!=r.physics || get(r.actor,0x29)!=r.base) return {};
    r.config=read(component,0x124); r.input_template=read(component,0xfc); r.manager=read(player,0x170);
    if (!r.config || !r.input_template || !r.manager || read(component,0x100)!=r.actor+0x68 || read(component,0x104)!=r.actor+0x60) return {};
    r.pivot=read(component,0x12c); r.camera=read(component,0x130); r.saved_camera=read(component,0x134);
    r.scene=read(component,0x144); r.camera_owner=read(component,0x148); r.visual=read(component,0x14c);
    r.animation=read(component,0x10c); r.auxiliary=read(component,0x150); r.rider=read(component,0x154);
    r.steering=read(component,0x158); r.muzzle1=read(component,0x1fc); r.muzzle2=read(component,0x200);
    if (!r.pivot || !r.camera || !r.scene || !r.camera_owner || !r.visual || !r.animation || !r.steering || !r.muzzle1 || !r.muzzle2) return {};
    r.move=control(r.scheme,0x28); r.primary=control(r.scheme,0x30); r.generation=generation;
    return result;
}
// Cleanup owns the current local controller, separately from replacement visual,
// camera/config/physics resources. Never call an old actor, interface or control.
template<class Read,class Byte>
bool bike_structure_matches(InputTarget owner,uint32_t level,uint32_t player,Read read,Byte byte) {
    const auto c=owner.component; const auto &r=owner.bike;
    return owner.kind==TargetKind::bike && c && level && player==r.player && player &&
        read(player,4)==r.actor && read(c,4)==r.actor && byte(c,0xd) &&
        read(level,0x17c)==r.normal && read(level,0x178)==r.interface_ && read(level,0x180)==r.interface_ &&
        r.interface_ && read(r.interface_,0xc)==r.scheme &&
        read(c,0x100)==r.actor+0x68 && read(c,0x104)==r.actor+0x60;
}
template<class Read,class Byte>
bool bike_root_matches(InputTarget owner,uint32_t level,uint32_t player,Read read,Byte byte) {
    return bike_structure_matches(owner,level,player,read,byte) && read(owner.component,0xfc) && read(player,0x170);
}
inline bool bike_enable_transition(bool current,bool requested) { return current!=requested; }
inline bool bike_state_transition(uint32_t current,uint32_t requested) { return requested==2 || current!=requested; }
struct BikeActions {
    bool moving=false, primary=false;
    uint32_t move_generation=0,primary_generation=0,move_bits=0;
    bool move_raw=false,primary_raw=false,primary_neutral=false;
};
struct BikeHostEventTag { uint32_t receiver=0,event=0,depth=0; bool available=false; };
inline bool claim_bike_host_event(BikeHostEventTag &tag,uint32_t receiver,uint32_t event,uint32_t depth) {
    if(!tag.available || tag.receiver!=receiver || tag.event!=event || tag.depth!=depth) return false;
    tag.available=false; return true;
}
struct BikeWriterEpochs {
    std::atomic<uint32_t> receiver{0},lane{0},move{0},primary{0};
    std::atomic<bool> foreign{false};
};
inline bool bike_move_available(bool enabled,uint32_t axes) { return enabled && (axes&1)!=0; }
inline bool bike_move_intent(uint32_t type,uint32_t key) {
    return type==7 || type==13 || type==14 || (type>=20 && type<=23) || (type==1 && (key==10 || key==13));
}
inline bool bike_primary_intent(uint32_t type,uint32_t key) {
    return type==29 || type==30 || type==33 || type==34 || (type==1 && key==15);
}
template<class Call,class Same,class Carry>
uint32_t observe_bike_writer(BikeWriterEpochs &epochs,bool tracked,bool host,bool foreign,uint32_t type,uint32_t key,
                          Call call,Same same,Carry carry) {
    if(foreign) { epochs.foreign=true; ++epochs.move; ++epochs.primary; }
    else if(tracked && !host) {
        if(bike_move_intent(type,key)) ++epochs.move;
        if(bike_primary_intent(type,key)) ++epochs.primary;
    }
    const auto move=epochs.move.load(); const uint32_t result=call();
    if(tracked && !foreign && !epochs.foreign && type!=64 && same() && move==epochs.move &&
        (host || !bike_move_intent(type,key))) carry(move);
    return result;
}
template<class Call>
void observe_bike_reset(BikeWriterEpochs &epochs,bool tracked,bool foreign,bool all,Call call) {
    if(tracked) { ++epochs.move; if(all) ++epochs.primary; if(foreign) epochs.foreign=true; }
    call();
}
inline void revoke_bike_stale(BikeActions &state,uint32_t move,uint32_t primary,bool foreign) {
    if(foreign || state.move_generation!=move) { state.moving=false; state.move_raw=false; }
    if(foreign || state.primary_generation!=primary) { state.primary=false; state.primary_raw=false; }
}
inline void bike_carry_readback(BikeActions &state,uint32_t generation,uint32_t bits) {
    if(state.moving && state.move_generation==generation) { state.move_bits=bits; state.move_raw=true; }
}
template<class Read>
void validate_bike_raw(BikeActions &state,Read read) {
    if(state.moving && state.move_raw && read(0xc)!=state.move_bits) { state.moving=false; state.move_raw=false; }
    if(state.primary && state.primary_raw && (read(0x20)!=(state.primary_neutral ? 0u : 1u) ||
        read(0x21)>1 || (state.primary_neutral && read(0x21)!=0))) { state.primary=false; state.primary_raw=false; }
}
// The caller proves current active state-0 core/API absence and each channel's
// origin. These are input words only; pending API cancellation is not ACKed here.
template<class CanWrite,class Read,class Write>
void neutralize_owned_bike_input(BikeActions &state,CanWrite can_write,Read read,Write write) {
    if(can_write()) validate_bike_raw(state,read);
    if(state.moving && state.move_raw && can_write() && state.moving && state.move_raw) {
        if(read(0xc)==state.move_bits) { if(state.move_bits) write(0xc,0); state.move_bits=0; }
        else { state.moving=false; state.move_raw=false; }
    }
    if(state.primary && state.primary_raw && can_write() && state.primary && state.primary_raw) {
        if(read(0x20)==1 && read(0x21)<=1) {
            // One channel, no intervening guest query between its two input bytes.
            write(0x20,0); write(0x21,0); state.primary_neutral=true;
        }
    }
}
// Central API cancellation guard shared by every release path and its fixture.
// A true result says the native call returned, not that release is acknowledged.
template<class Validate,class Sync,class Raw,class Call>
bool bike_cancel_boundary(BikeActions &state,uint32_t type,Validate validate,Sync sync,Raw raw,Call call) {
    const bool valid=validate(); sync();
    if(valid) raw();
    if(!valid || !(type==14 ? state.moving : state.primary)) return false;
    call(); return true;
}
template<class Root,class Event,class Ack>
void release_bike_actions(BikeActions &state,Root root,Event event,Ack ack) {
    // These cancel queued input only; there is no Bike FireStop obligation.
    if (state.moving && root() && state.moving) { if(event(14,0.f) && ack(14)) state.moving=false; }
    if (state.primary && root() && state.primary) { if(event(30,0.f) && ack(30)) state.primary=false; }
}
inline bool same_bike_core(InputTarget a,uint32_t a_level,InputTarget b,uint32_t b_level) {
    return a && b && a_level==b_level && a.component==b.component && a.bike.player==b.bike.player &&
        a.bike.actor==b.bike.actor && a.bike.normal==b.bike.normal &&
        a.bike.interface_==b.bike.interface_ && a.bike.scheme==b.bike.scheme;
}
struct BikeCancellation { InputTarget owner{}; uint32_t level=0; BikeActions actions{}; };
inline void retain_bike_cancellation(BikeCancellation &pending,InputTarget owner,uint32_t level,BikeActions &actions) {
    if(!pending.owner) { pending.owner=owner; pending.level=level; }
    const auto &old=pending.owner.bike; const auto &now=owner.bike;
    if(pending.owner.component==owner.component && pending.level==level && old.actor==now.actor && old.player==now.player &&
        old.normal==now.normal && old.interface_==now.interface_ && old.scheme==now.scheme) {
        if(actions.moving) {
            pending.actions.moving=true; pending.actions.move_generation=actions.move_generation;
            pending.actions.move_bits=actions.move_bits; pending.actions.move_raw=actions.move_raw;
        }
        if(actions.primary) {
            pending.actions.primary=true; pending.actions.primary_generation=actions.primary_generation;
            pending.actions.primary_raw=actions.primary_raw; pending.actions.primary_neutral=actions.primary_neutral;
        }
    }
    actions={};
}
template<class Retained,class Root,class Event,class Ack>
void clean_bike_cancellation(BikeCancellation &pending,Retained retained,Root root,Event event,Ack ack) {
    if(!pending.owner) return;
    if(!retained()) { pending={}; return; }
    release_bike_actions(pending.actions,root,event,ack);
    if(!pending.actions.moving && !pending.actions.primary) pending={};
}
template<class Live,class Move,class Primary,class Event>
void apply_bike_actions(const FrameInput &input,BikeActions &state,Live live,Move move,Primary primary,Event event) {
    const float x=float(input.down(Action::right))-float(input.down(Action::left));
    if (x && live() && move() && live()) { state.moving=true; event(13,x); }
    else if (state.moving && live()) { if(event(14,0.f)) state.moving=false; }
    // A quick down/up is still one press. Holding never synthesizes repeats.
    if (input.edge(Action::fire) && live() && primary() && live()) { state.primary=true; event(29,0.f); }
    if (state.primary && !input.down(Action::fire) && !input.edge(Action::fire) && live()) {
        if(event(30,0.f)) state.primary=false;
    }
}
template<class Live,class Enabled,class Root,class Event,class Ack>
void validate_bike_primary(BikeActions &state,Live live,Enabled enabled,Root root,Event event,Ack ack) {
    if (state.primary && (!live() || !enabled() || !live())) {
        if (root() && state.primary) { if(event(30,0.f) && ack()) state.primary=false; }
    }
}
template<class Draw>
void draw_bike_artwork(bool hidden,uint32_t alpha,Draw draw) { draw(hidden ? 0u : alpha); }
inline bool bike_touch(uint32_t drawing,uint32_t move,uint32_t primary) {
    return drawing && (drawing==move || drawing==primary);
}
template<class Live,class Owns,class Control,class Visible,class Set,class Draw>
void draw_bike_scheme(bool scope,Live live,Owns owns,Control control,Visible visible,Set set,Draw draw) {
    if (!scope || !live()) { draw(); return; }
    std::map<uint32_t,uint8_t> saved;
    for (auto slot : {0x28u,0x30u}) {
        if(!live()) break;
        const auto p=control(slot);
        if(p && live()) saved.try_emplace(p,visible(p));
    }
    for(auto [p,visible_] : saved) { (void)visible_; if(live()) set(p,false); }
    auto restore=[&] { for(auto [p,visible_] : saved) if(owns()) set(p,visible_); };
    try { draw(); } catch(...) { restore(); throw; }
    restore(); // A replaced Draw owner may already have destroyed these controls.
}
}
