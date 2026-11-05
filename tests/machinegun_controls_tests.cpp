#include "input/native_controls.h"
#include "bionic/runtime.h"
#include "fixtures/machinegun_contract.h"
#include "fixtures/mech_contract.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
using namespace nova::input;
using nova::abi::invoke;
namespace {
void check(bool condition,const char *message) { if(!condition) throw std::runtime_error(message); }
void close_to(float a,float b,const char *message) { check(std::isfinite(a) && std::abs(a-b)<.0001f,message); }
uint32_t address(auto &value) { return reinterpret_cast<uint32_t>(&value); }
struct Harness;
Harness *current=nullptr;
uint32_t stop_original=0,release_original=0,trampoline=0;
struct Harness {
    Controls controls{ControlsConfig{}};
    MachineGunContract gun{},other{};
    InputTarget target;
    MachineGunActions actions;
    FrameInput frame;
    uint32_t consumer,event,update,reset,stop,release,stop_site,release_site;
    uint32_t owned_weapon=0,owned_manager=0;
    bool pending=false,live=true,rotate=true,enabled=true,exit_pending=false;
    MachineGunOwner owner;
    int duration=0,delay=0;
    unsigned prepares=0,readbacks=0;
    Harness(const nova::ElfImage &image) {
        current=this; controls.gameplay=true; target={TargetKind::machinegun,address(gun)};
        owner={1,address(gun),2,2,3,false,false,true};
        gun.yaw_limit=45; gun.pitch_minimum=-12; gun.pitch_maximum=27;
        gun.actor_yaw=101; gun.actor_pitch=-66; gun.weapon.allowed[0]=gun.weapon.allowed[1]=1;
        other=gun;
        consumer=nova::abi::require_function(image,"machinegun_consume");
        event=nova::abi::require_function(image,"machinegun_event");
        update=nova::abi::require_function(image,"machinegun_update");
        reset=nova::abi::require_function(image,"machinegun_reset");
        stop=nova::abi::require_function(image,"machinegun_stop");
        release=nova::abi::require_function(image,"machinegun_release");
        stop_site=nova::abi::require_function(image,"machinegun_primary_stop");
        release_site=nova::abi::require_function(image,"machinegun_primary_release");
    }
    ~Harness() { if(current==this) current=nullptr; }
    bool eligible() const {
        return live && enabled && !gun.exiting && machinegun_owner_matches(target,1,2,3,owner);
    }
    void button(Uint32 type,Uint8 button) {
        SDL_Event e{}; e.type=type; e.button.button=button; controls.handle(e);
    }
    void key(Uint32 type,SDL_Keycode code) {
        SDL_Event e{}; e.type=type; e.key.key=code; controls.handle(e);
    }
    void motion(float x,float y) {
        SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.xrel=x; e.motion.yrel=y; controls.handle(e);
    }
    void drain(bool primary=true,bool secondary=true) {
        frame=controls.active() ? controls.take() : FrameInput{};
        owned_weapon=address(gun.weapon); owned_manager=address(gun.manager);
        exit_pending=frame.edge(Action::interact);
        apply_machinegun_actions(frame,primary,secondary,actions,[&] { return eligible(); },
            [&](uint32_t type) { invoke(event,{address(gun),type}); });
        pending=eligible() && (frame.dx!=0 || frame.dy!=0);
    }
    void boundary(MachineGunContract &object,unsigned dt,uint32_t original=0) {
        mouse_look_boundary(pending,[&] {
            return controls.active() && eligible() && rotate &&
                look_target_matches(target,TargetKind::machinegun,address(object),enabled) &&
                machinegun_look_available(duration,delay,static_cast<int>(dt));
        },[&] {
            auto delta=machinegun_mouse_delta(frame.dx,frame.dy,object.pitch,
                object.pitch_minimum,object.pitch_maximum,controls.config);
            queue_mouse_look(*reinterpret_cast<GuestLookInput *>(&object.look),object.yaw,object.pitch,delta);
            ++prepares;
        },[&] { invoke(original ? original : consumer,{address(object),dt}); },[&] { ++readbacks; });
    }
    void cleanup(bool weapon_matches=true,bool manager_matches=true,bool input_valid=true) {
        release_machinegun_actions(actions,{enabled,gun.exiting!=0,input_valid,weapon_matches,manager_matches},
            [&] { invoke(reset,{address(gun)}); },
            [&] { invoke(stop,{address(gun.weapon),0,1}); },
            [&] { invoke(release,{address(gun.manager),0}); });
        owned_weapon=owned_manager=0;
    }
    void change(InputTarget next,bool same_level=true,bool same_identity=true) {
        change_input_target(target,next,[&](InputTarget old) {
            check(old==target,"cleanup precedes target publication");
            if(live_input_target(old,1,same_level ? 1 : 2,11,22,same_identity ? address(gun) : address(other))) cleanup();
        },[&] { controls.clear(); actions={}; frame={}; pending=false; exit_pending=false; owned_weapon=owned_manager=0; });
        controls.gameplay=bool(next);
    }
};
void __cdecl stop_observer(uint32_t weapon,uint32_t channel,uint32_t messages) {
    auto call=[&] { invoke(stop_original,{weapon,channel,messages}); };
    if(!current) { call(); return; }
    observe_machinegun_release(current->actions.weapon_owned,current->owned_weapon,weapon,channel,call);
}
void __cdecl release_observer(uint32_t manager,uint32_t channel) {
    auto call=[&] { invoke(release_original,{manager,channel}); };
    if(!current) { call(); return; }
    observe_machinegun_release(current->actions.manager_owned,current->owned_manager,manager,channel,call);
}
void __cdecl replacement(uint32_t object,uint32_t dt) {
    current->boundary(*reinterpret_cast<MachineGunContract *>(object),dt,trampoline);
}
void selection() {
    for(unsigned mask=0;mask<128;++mask) {
        TargetCandidate normal{11,(mask&1)!=0,true,true},mech{22,(mask&2)!=0,true,true};
        TargetCandidate gun{33,(mask&4)!=0,(mask&8)!=0,(mask&16)!=0};
        const bool context=(mask&32)!=0;
        const bool car=(mask&64)!=0;
        gun.alive=gun.alive && stationary_machinegun(car,false,false);
        auto target=select_input_target(context,normal,mech,gun);
        unsigned count=unsigned(normal.enabled)+unsigned(mech.enabled)+unsigned(gun.enabled);
        bool expected=context && count==1 && (!gun.enabled || (gun.alive && gun.interactive));
        check(bool(target)==expected,"exclusive supported selection across 128 contexts");
        if(target) check(target.component==(normal.enabled ? 11u : mech.enabled ? 22u : 33u),"exact owner identity");
    }
    check(stationary_machinegun(false,false,false),"unparented and static-parent stationary contexts allowed");
    check(!stationary_machinegun(true,false,false) && !stationary_machinegun(false,true,false) &&
        !stationary_machinegun(false,false,true),"car parent / enabled jeep / exit rejected");
    check(!live_input_target({TargetKind::machinegun,33},1,2,11,22,33) &&
        !live_input_target({TargetKind::machinegun,33},1,1,11,22,34),"fresh level/getter lifetime check");
    const MachineGunOwner valid{1,33,2,2,3,false,false,true};
    check(machinegun_owner_matches({TargetKind::machinegun,33},1,2,3,valid),"live native owner accepted");
    for(unsigned field=0;field<8;++field) {
        auto now=valid;
        switch(field) {
        case 0: now.level=9; break; case 1: now.component=9; break;
        case 2: now.normal=9; break; case 3: now.active_interface=9; break;
        case 4: now.scheme=9; break; case 5: now.normal_enabled=true; break;
        case 6: now.mech_enabled=true; break; default: now.ingame=false; break;
        }
        check(!machinegun_owner_matches({TargetKind::machinegun,33},1,2,3,now),"production live owner checks every identity/interface/competing-enable/state field");
    }
    for(int duration : {-1,0,1,100}) for(int delay : {-20,0,15,16,17}) {
        check(machinegun_look_available(duration,delay,16)==(duration<=0 || delay>16),"actual native timeout and final-step boundary");
    }
    std::puts("selection: 128 ownership combinations, stationary/static-parent/car/jeep/exit and exact interpolation timing passed");
}
void action_contract(const nova::ElfImage &image) {
    for(unsigned mask=0;mask<8;++mask) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain();
        h.cleanup((mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
        check(h.gun.weapon.stops==unsigned((mask&1)!=0) && h.gun.manager.releases==unsigned((mask&2)!=0),
            "independent current identities release each still-valid owner, never the stale one");
        check(h.gun.resets==unsigned((mask&4)!=0),"reset requires live current angle bindings");
    }
    for(unsigned stage=0;stage<4;++stage) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain();
        invoke(h.update,{address(h.gun),16});
        h.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT); h.drain();
        if(stage==1) invoke(h.stop_site,{address(h.gun.weapon),0,1});
        if(stage==2) invoke(h.update,{address(h.gun),16});
        if(stage==3) invoke(h.reset,{address(h.gun)});
        check(h.actions.weapon_owned==(stage!=1 && stage!=2) && h.actions.manager_owned==(stage!=2),
            "independent actual-call acknowledgement, never release bytes or elapsed frame");
        h.cleanup(); h.cleanup(); invoke(h.update,{address(h.gun),16});
        check(h.gun.weapon.stops==1 && h.gun.weapon.messages==1 && h.gun.manager.releases==1,
            "pending/partially consumed/consumed/reset-only release each stops both native objects exactly once");
        check(!h.gun.weapon.firing && !h.gun.manager.firing && !h.gun.released[0],"no held action or residual release on resume");
    }
    {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain(); invoke(h.update,{address(h.gun),16});
        h.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT); h.drain();
        h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain();
        check(!h.gun.released[0] && h.actions.weapon_owned && h.actions.manager_owned,
            "native event29 cancels the old release and the new press owns both APIs");
        invoke(h.update,{address(h.gun),16}); h.cleanup(); invoke(h.update,{address(h.gun),16});
        check(h.gun.weapon.stops==1 && h.gun.manager.releases==1 && !h.gun.weapon.firing,
            "skipped consumer then repress preserves new firing ownership through loss and resume");
    }
    Harness wrong(image); wrong.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); wrong.drain();
    invoke(wrong.stop_site,{address(wrong.other.weapon),0,1});
    invoke(wrong.release_site,{address(wrong.other.manager),0});
    invoke(wrong.stop_site,{address(wrong.gun.weapon),1,1});
    invoke(wrong.release_site,{address(wrong.gun.manager),1});
    check(wrong.actions.weapon_owned && wrong.actions.manager_owned,"wrong object/channel never acknowledges either owner");
    bool owned=true; bool called=false;
    observe_machinegun_release(owned,0,0,0,[&] { check(owned,"ack must follow actual call"); called=true; });
    check(called && owned,"zero cache never acknowledges");
    observe_machinegun_release(owned,5,5,0,[&] { check(owned,"ownership survives until native return"); });
    check(!owned,"actual completed matching call acknowledges");
    for(bool secondary_allowed : {false,true}) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); h.drain(true,secondary_allowed);
        invoke(h.update,{address(h.gun),16}); h.drain(true,secondary_allowed); invoke(h.update,{address(h.gun),16});
        check(h.gun.weapon.shots[1]==unsigned(secondary_allowed),"secondary press once, held does not repeat");
        h.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT); h.drain(); h.cleanup();
        check(!h.gun.weapon.stops && !h.gun.manager.releases,"secondary has no invented native stop channel");
    }
    Harness click(image); click.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); click.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT);
    click.drain(); invoke(click.update,{address(click.gun),16}); click.drain(); invoke(click.update,{address(click.gun),16});
    check(click.gun.events[31]==1 && click.gun.events[32]==1 && click.gun.weapon.shots[1]==1,"brief click retains native edge");
    Harness gated(image); gated.key(SDL_EVENT_KEY_DOWN,SDLK_W); gated.key(SDL_EVENT_KEY_DOWN,SDLK_SPACE);
    gated.key(SDL_EVENT_KEY_DOWN,SDLK_R); gated.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT);
    gated.drain(false,false); check(!gated.gun.events[29] && !gated.gun.events[13],"disabled control and unsupported on-foot actions ignored");
    gated.drain(); gated.gun.weapon.allowed[0]=0; invoke(gated.update,{address(gated.gun),16});
    check(!gated.gun.weapon.shots[0],"native weapon eligibility authoritative");
    MachineGunActions state; FrameInput frame; frame.held.set(static_cast<size_t>(Action::fire)); frame.held.set(static_cast<size_t>(Action::aim));
    bool live=true; unsigned events=0;
    apply_machinegun_actions(frame,true,true,state,[&] { return live; },[&](uint32_t) { ++events; live=false; });
    check(events==1 && !state.secondary,"live eligibility rechecked between independent action dispatches");
    apply_machinegun_actions(frame,true,true,state,[&] { return live; },[&](uint32_t) { ++events; });
    check(events==1,"inactive owner receives no new gameplay event");
    std::puts("actions: native primary pair, separate completion ACKs, secondary edge, brief click, gates and mid-dispatch loss passed");
}
void transitions(const nova::ElfImage &image) {
    {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain();
        h.controls.overlay(true); check(!h.controls.active(),"overlay independently gates MG");
        h.cleanup(); h.cleanup(); h.controls.overlay(false); h.controls.clear(); h.drain();
        check(h.gun.weapon.stops==1 && h.gun.manager.releases==1 && !h.gun.held[0],"overlay releases actual MG weapon/manager ownership once");
    }
    for(auto kind : {TargetKind::none,TargetKind::normal,TargetKind::mech,TargetKind::machinegun}) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.motion(80,40); h.drain();
        h.change({kind,kind==TargetKind::none ? 0u : kind==TargetKind::normal ? 11u : kind==TargetKind::mech ? 22u : address(h.other)});
        check(!h.pending && h.frame.held.none() && h.frame.pressed.empty() && !h.controls.take().dx,"ownership transition drops held/edge/look");
        check(h.gun.weapon.stops==1 && h.gun.manager.releases==1,"transition releases valid old owner once");
    }
    for(bool identity : {false,true}) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain();
        h.change({},!identity,identity);
        check(!h.gun.resets && !h.gun.weapon.stops && !h.gun.manager.releases,"changed level/getter never dereferences old owner");
    }
    for(bool exit : {false,true}) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain(); invoke(h.update,{address(h.gun),16});
        h.gun.exiting=exit; h.gun.look.pitch_target=0; h.gun.look.pitch_delta=-1;
        auto saved=h.gun.look;
        h.controls.context(false,false); h.cleanup(); h.cleanup();
        if(exit) {
            check(!std::memcmp(&saved,&h.gun.look,sizeof(saved)),"exit/pause preserves native pitch target/delta and all pending input");
            check(!h.gun.resets && !h.gun.weapon.stops && !h.gun.manager.releases,"exit is native-owned, no duplicate host cleanup");
            // Model the eventual native Disable, not an action requested by the host.
            invoke(h.reset,{address(h.gun)}); invoke(h.stop,{address(h.gun.weapon),0,1}); invoke(h.release,{address(h.gun.manager),0});
        }
        h.controls.context(true,false); h.controls.clear(); h.drain(); invoke(h.update,{address(h.gun),16});
        check(h.gun.weapon.stops==1 && h.gun.manager.releases==1,"focus/exit native cleanup stays once-only on resume");
    }
    Harness disabled(image); disabled.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); disabled.drain(); disabled.enabled=false; disabled.cleanup();
    check(!disabled.gun.resets && !disabled.gun.weapon.stops,"already-disabled native teardown not repeated");
    bool pending=true;
    check(!mech_exit_edge(pending,false) && pending && mech_exit_edge(pending,true) && !mech_exit_edge(pending,true),"E preserves native exit enablement and consumes once");
    std::puts("transitions: normal/mech/unsupported/stale identity, focus, native exit-pitch preservation and once-only E passed");
}
void look_contract(const nova::ElfImage &image) {
    Harness h(image);
    std::array<uint8_t,6> original{}; std::memcpy(original.data(),reinterpret_cast<void *>(h.consumer),original.size());
    {
        nova::abi::GuestHook hook(image,"machinegun_consume",{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&replacement));
        trampoline=hook.original();
        for(auto counts : {std::pair{40.f,0.f},std::pair{0.f,-60.f},std::pair{25.f,15.f},std::pair{0.f,-1000.f},std::pair{0.f,1000.f},std::pair{1000.f,0.f}}) {
            const float old_yaw=h.gun.yaw,old_pitch=h.gun.pitch;
            auto n=h.prepares;
            h.motion(counts.first/2,counts.second/2); h.motion(counts.first/2,counts.second/2); h.drain();
            invoke(h.consumer,{address(h.other),16}); check(h.prepares==n && h.pending,"wrong native owner cannot consume counts");
            auto delta=machinegun_mouse_delta(counts.first,counts.second,old_pitch,-12,27,h.controls.config);
            invoke(h.consumer,{address(h.gun),16}); invoke(h.consumer,{address(h.gun),16});
            check(h.prepares==n+1 && h.readbacks==h.prepares && !h.pending,"both accumulated axes consumed once at actual detour");
            close_to(h.gun.yaw,std::clamp(old_yaw+delta.yaw,-45.f,45.f),"native gun yaw bound");
            close_to(h.gun.pitch,old_pitch+delta.pitch,"asymmetric native gun pitch range");
            close_to(h.gun.actor_yaw,101,"host never writes actor yaw"); close_to(h.gun.actor_pitch,-66,"host never writes actor pitch");
            h.drain(); auto saved=h.gun.look; invoke(h.consumer,{address(h.gun),33});
            check(!std::memcmp(&saved,&h.gun.look,sizeof(saved)),"following no-motion frame leaves input fields alone");
        }
        h.gun.pitch=0; h.controls.config.invert_y=true; h.motion(0,40); h.drain(); invoke(h.consumer,{address(h.gun),7}); close_to(h.gun.pitch,4,"invert-Y config");
        h.gun.pitch=0;
        for(unsigned i=0;i<4;++i) { h.motion(0,10); h.drain(); invoke(h.consumer,{address(h.gun),i+1}); }
        close_to(h.gun.pitch,4,"count partition independent of dt");
        for(unsigned guard=0;guard<6;++guard) {
            h.live=guard!=0; h.enabled=guard!=1; h.rotate=guard!=2;
            h.duration=guard>=3 ? 50 : 0; h.delay=guard==3 ? -1 : guard==4 ? 0 : 16;
            h.motion(10,10); h.drain(); auto saved=h.gun.look; auto n=h.prepares;
            invoke(h.consumer,{address(h.gun),16});
            check(h.prepares==n,"live/enable/rotate/interpolation guard blocks preparation");
            check(!std::memcmp(&saved,&h.gun.look,sizeof(saved)),"blocked look preserves guest pending fields");
            h.pending=false; // Same production begin_frame discard, not deferred replay.
        }
        h.live=h.enabled=h.rotate=true; h.duration=50; h.delay=17;
        h.motion(10,10); h.drain(); auto n=h.prepares; invoke(h.consumer,{address(h.gun),16});
        check(h.prepares==n+1,"positive native delay does not over-gate mouse");
        h.duration=0; h.owner.active_interface=99; h.motion(100,100); h.drain(); n=h.prepares;
        invoke(h.consumer,{address(h.gun),16}); check(h.prepares==n,"interface changed after selection blocks actual look seam");
        h.pending=false; h.owner.active_interface=2; h.drain(); invoke(h.consumer,{address(h.gun),16});
        check(h.prepares==n,"discarded ineligible counts never replay after interface restoration");
        h.duration=50; h.delay=0; h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.drain();
        invoke(h.update,{address(h.gun),16}); check(h.gun.weapon.shots[0]==1 && h.controls.active(),"ordinary target following does not drop fire/capture");
    }
    check(!std::memcmp(original.data(),reinterpret_cast<void *>(h.consumer),original.size()),"machinegun hook restoration");
    bool rejected=false;
    try { nova::abi::GuestHook bad(image,"machinegun_consume",{0x55,0x89,0xe5,0x83,0xe4,0xf8},reinterpret_cast<uint32_t>(&replacement)); }
    catch(const std::runtime_error &) { rejected=true; }
    check(rejected && !std::memcmp(original.data(),reinterpret_cast<void *>(h.consumer),original.size()),"mismatched prologue rejected before mutation");
    std::puts("look: real aligned hook/trampoline, X/Y/mixed/zero/repeated/asymmetric limits/inversion/guards/actor preservation passed");
}
void visibility(const nova::ElfImage &image) {
    const auto setter=nova::abi::require_function(image,"mech_set_draw");
    for(bool gameplay : {false,true}) for(bool throw_draw : {false,true}) {
        MechDrawControl fire{1,1,1,0},exit{0,0,0,0},info{1,1,0,0};
        const bool scope=native_hud_presentation(gameplay,false,true).hide_mobile;
        unsigned draws=0,queries=0;
        try {
            draw_interface_controls(scope,[&](uint32_t slot) {
                ++queries; return slot==0x30 ? address(fire) : 0u;
            },[&](uint32_t offset) {
                ++queries; return offset==0x10 ? address(exit) : offset==0x4c ? address(fire) : 0u;
            },[&](uint32_t p) { return static_cast<uint8_t>(reinterpret_cast<MechDrawControl *>(p)->visible); },
            [&](uint32_t p,bool visible) { invoke(setter,{p,visible}); },[&] {
                ++draws;
                check(fire.visible==unsigned(!scope) && !exit.visible,"actual shared dispatch hides scoped controls");
                check(fire.enabled==1 && fire.pressed==1 && exit.enabled==0 && info.visible==1,"informative neighbor and action state retained");
                if(throw_draw) throw std::runtime_error("synthetic draw failure");
            });
        } catch(const std::runtime_error &e) { check(throw_draw && std::string(e.what())=="synthetic draw failure","unexpected draw failure"); }
        check(draws==1 && fire.visible==1 && exit.visible==0,"normal and throwing renderer restore exact flags");
        check(fire.sets==unsigned(scope)*2 && exit.sets==unsigned(scope)*2 && info.sets==0,"duplicate controls deduplicated and neighboring HUD untouched");
        check(queries==unsigned(scope)*22,"out-of-scope draw does no control queries");
    }
    std::puts("visibility: production owner gate and full interface dispatch, neighboring HUD, aliases and exception restoration passed");
}
void contextual_visibility() {
    for(unsigned flags=0;flags<128;++flags) {
        const bool scope=flags&1,alias=flags&2,show=flags&4;
        const bool enabled=flags&8,visible=flags&16,throw_draw=flags&32;
        const bool contextual=flags&64;
        std::map<uint32_t,bool> draw_flags{{1,visible},{2,visible},{3,true},{4,true},{5,true}};
        auto saved=draw_flags;
        std::map<uint32_t,unsigned> writes;
        unsigned paints=0,draws=0;
        try {
            draw_contextual_buttons(scope && !contextual,
                [](uint32_t slot) { return slot==0x60 ? 1u : 2u; },
                [&](uint32_t p) { return draw_flags.at(p); },
                [&](uint32_t p,bool value) { draw_flags.at(p)=value; ++writes[p]; },[&] {
            draw_interface_controls(scope,[&](uint32_t slot) { return slot==0x30 ? (alias ? 1u : 5u) : 0u; },
                [](uint32_t offset) {
                    switch(offset) {
                    case 0x60: return 1u;
                    case 0x64: return 2u;
                    case 0x20: return 3u;
                    case 0x24: return 4u;
                    case 0x4c: return 5u;
                    default: return 0u;
                    }
                },[&](uint32_t p) { return draw_flags.at(p); },
                [&](uint32_t p,bool value) { draw_flags.at(p)=value; ++writes[p]; },[&] {
                    ++draws;
                    // Native Draw still owns all three gates; this helper does not.
                    if(show && enabled && draw_flags.at(1)) ++paints;
                    if(show && enabled && draw_flags.at(2)) ++paints;
                    for(auto p : {3u,4u,5u})
                        check(draw_flags.at(p)==!scope,"role and mobile controls remain independently masked");
                    if(throw_draw) throw std::runtime_error("entry draw failure");
                });
            });
        } catch(const std::runtime_error &e) {
            check(throw_draw && std::string(e.what())=="entry draw failure","unexpected entry draw failure");
        }
        check(draws==1 && paints==2*unsigned((!scope || contextual) && show && enabled && visible),
              "standard and projected indicators retain every native show/enabled/visibility gate");
        check(draw_flags==saved,"all draw flags restore after normal or throwing renderer");
        check(writes[1]==unsigned(scope && !contextual)*2 && writes[2]==writes[1],
              "contextual masks restore exactly while seated, including scheme aliases");
        for(auto p : {3u,4u,5u}) check(writes[p]==unsigned(scope)*2,"masks restore and aliases deduplicate");
    }
    struct Phase { bool scene,foot,seated,mobile,context; };
    const std::array phases{
        Phase{true,true,false,true,true}, // On foot, approach entry.
        Phase{true,true,true,true,false}, // Native overlap during entry.
        Phase{true,false,true,true,false}, // Inside vehicle/turret.
        Phase{true,false,false,true,false}, // Exit animation, no input receiver.
        Phase{true,true,false,true,true}, // On foot again.
        Phase{false,false,false,false,false}}; // Menu, original rendering.
    for(const auto phase:phases) {
        const auto policy=native_hud_presentation(phase.scene,phase.foot,phase.seated);
        check(policy.hide_mobile==phase.mobile && policy.show_contextual==phase.context,
              "HUD presentation survives input-disabled entry/exit transitions");
        for(bool local:{false,true}) {
            unsigned draws=0; draw_native_interaction(policy,local,[&] { ++draws; });
            check(draws==unsigned(!local || !phase.scene || phase.context),
                  "custom artwork gate affects only local seated/transitioning player");
        }
    }
}
struct ContextButton { uint32_t table,enabled,draws=0,dt=0; };
uint32_t __cdecl context_enabled(ContextButton *button) { return button->enabled; }
void __cdecl context_draw(ContextButton *button,uint32_t dt) { ++button->draws; button->dt=dt; }
void original_context_draw(const nova::ElfImage &image) {
    // Original PlayerComponent -> InteractiveComponent -> enabled/draw virtual
    // dispatch, with only authored button receivers. No scene or initializers.
    const auto entry=nova::abi::require_function(image,"_ZN15PlayerComponent25DrawCustomInteractButtonsEi");
    std::array<uint32_t,34> table{};
    table[0x84/4]=reinterpret_cast<uint32_t>(&context_enabled);
    table[0x6c/4]=reinterpret_cast<uint32_t>(&context_draw);
    ContextButton driver{address(table),1},shooter{address(table),0};
    std::array<uint32_t,2> buttons{address(driver),address(shooter)};
    std::array<uint32_t,0x2c/4> interactive{};
    interactive[0x24/4]=address(buttons); interactive[0x28/4]=address(buttons)+sizeof(buttons);
    std::array<uint32_t,0x130/4> object{}; object[0x12c/4]=address(interactive);
    std::array<uint32_t,0x1a4/4> player{}; player[0x1a0/4]=address(object);
    invoke(entry,{address(player),17});
    check(driver.draws==1 && driver.dt==17 && shooter.draws==0,
          "original selected-object route draws only native-enabled contextual artwork");
    driver.enabled=0; shooter.enabled=1; invoke(entry,{address(player),31});
    check(driver.draws==1 && shooter.draws==1 && shooter.dt==31,"native enabled changes select the next original button");
    for(const auto policy:{native_hud_presentation(true,false,true),native_hud_presentation(true,false,false)})
        draw_native_interaction(policy,true,[&] { invoke(entry,{address(player),41}); });
    check(shooter.draws==1 && shooter.enabled==1,"seated and exit-transition gates suppress only rendering, not native enabled state");
    draw_native_interaction(native_hud_presentation(true,true,false),true,[&] { invoke(entry,{address(player),47}); });
    check(shooter.draws==2 && shooter.dt==47,"returning on foot resumes the original selected-object draw");
    draw_native_interaction(native_hud_presentation(true,false,true),false,[&] { invoke(entry,{address(player),53}); });
    check(shooter.draws==3 && shooter.dt==53,"local seated gate does not suppress another receiver");
    player[0x1a0/4]=0; invoke(entry,{address(player),99});
    check(driver.draws==1 && shooter.draws==3,"no selected object draws no contextual artwork");
}
void real(const char *path) {
    nova::ImportRegistry imports; nova::bionic::register_boot(imports); nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports); Controls controls{ControlsConfig{}};
    const auto entry=nova::abi::require_function(image,"_ZN26CPlayerMachineGunComponent15UpdateFromInputEi");
    const auto update=nova::abi::require_function(image,"_ZN26CPlayerMachineGunComponent6UpdateEi");
    std::array<uint8_t,6> saved{}; std::memcpy(saved.data(),reinterpret_cast<void *>(entry),saved.size());
    std::array<std::array<uint8_t,5>,2> calls{};
    unsigned i=0; for(auto offset : {0xb31u,0xb4du}) std::memcpy(calls[i++].data(),reinterpret_cast<void *>(update+offset),5);
    const auto context=nova::abi::require_function(image,"_ZN15PlayerComponent25DrawCustomInteractButtonsEi");
    std::array<uint8_t,16> context_code{};
    std::memcpy(context_code.data(),reinterpret_cast<void *>(context),context_code.size());
    { NativeControls native(image,controls);
      check(std::memcmp(context_code.data(),reinterpret_cast<void *>(context),context_code.size())!=0,
            "checked contextual presentation hook is installed");
      original_context_draw(image);
    }
    check(!std::memcmp(context_code.data(),reinterpret_cast<void *>(context),context_code.size()),
          "contextual presentation hook restores the exact original body");
    check(!std::memcmp(saved.data(),reinterpret_cast<void *>(entry),saved.size()),"real machinegun entry restored");
    i=0; for(auto offset : {0xb31u,0xb4du}) check(!std::memcmp(calls[i++].data(),reinterpret_cast<void *>(update+offset),5),"real independent release call restored");
    std::puts("real adapter: symbols, exact prologue and both CALL encodings verified/restored; no guest initialization or gameplay");
}
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--real") { if(!std::filesystem::exists(argv[2])) return 77; real(argv[2]); return 0; }
        check(argc==2,"fixture path required"); selection();
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        stop_original=nova::abi::require_function(image,"machinegun_stop"); release_original=nova::abi::require_function(image,"machinegun_release");
        std::vector<std::unique_ptr<nova::abi::GuestCall>> observers;
        std::vector<std::pair<uint32_t,std::array<uint8_t,5>>> saved;
        for(auto name : {"stop","release"}) {
            std::string owner=std::string("machinegun_primary_")+name;
            auto entry=nova::abi::require_function(image,owner),site=nova::abi::require_function(image,owner+"_call");
            std::array<uint8_t,5> bytes{}; std::memcpy(bytes.data(),reinterpret_cast<void *>(site),5); saved.emplace_back(site,bytes);
            observers.push_back(std::make_unique<nova::abi::GuestCall>(image,owner,site-entry,std::string("machinegun_")+name,
                std::string(name)=="stop" ? reinterpret_cast<uint32_t>(&stop_observer) : reinterpret_cast<uint32_t>(&release_observer)));
        }
        action_contract(image); transitions(image); look_contract(image); visibility(image); contextual_visibility();
        observers.clear();
        for(auto &[site,bytes] : saved) check(!std::memcmp(bytes.data(),reinterpret_cast<void *>(site),5),"both synthetic release CALLs restored");
        std::puts("stationary machinegun production contracts passed"); return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"machinegun test failed: %s\n",e.what()); return 1; }
}
