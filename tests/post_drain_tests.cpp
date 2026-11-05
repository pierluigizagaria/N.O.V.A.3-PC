#include "input/native_gamepad_post_drain.h"
#include "input/normal_keyboard_movement.h"
#include "input/native_adapter.h"
#include "input/native_target.h"
#include "fixtures/post_drain_contract.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <windows.h>
#include "post_drain_original_gate.h"
using namespace nova::input;
using nova::abi::invoke;
namespace {
void check(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
bool nearly_equal(float a,float b) { return std::abs(a-b)<.00001f; }
uint32_t raw(const void *p) { return reinterpret_cast<uint32_t>(p); }
template<class T=uint32_t> T &at(uint32_t p,uint32_t off=0) { return *reinterpret_cast<T *>(p+off); }
SDL_Event key(Uint32 type,SDL_Keycode code) { SDL_Event e{}; e.type=type; e.key.key=code; return e; }
NativeGamepadPostDrain *adapter=nullptr;
Controls *controls=nullptr;
PostDrainWorld *world=nullptr;
uint32_t lifecycle=0,construct=0,reset_fn=0,enable_fn=0,queue_fn=0,drain_fn=0;
PostDrainPlayer *replacement=nullptr;
unsigned callback_mode=0;
bool keyboard_moving=false,keyboard_observed=false;
bool protect_poison=true;
uint32_t poisoned_page=0;
struct PlayerPage {
    PostDrainPlayer *data=static_cast<PostDrainPlayer *>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    PlayerPage() { if(!data) throw std::runtime_error("owned poison page allocation"); }
    ~PlayerPage() { VirtualFree(data,0,MEM_RELEASE); }
};
void unpoison() {
    if(!poisoned_page) return;
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void *>(poisoned_page),4096,PAGE_READWRITE,&old)) throw std::runtime_error("poison page restore");
    poisoned_page=0;
}
void __cdecl reset_observer(unsigned p) noexcept try { adapter->native_reset(p); } catch (...) { post_drain_fixture::callback_fail_fast("fixture.reset_observer"); }
void __cdecl event_observer(unsigned p,unsigned event) noexcept try { adapter->native_event(p,event); } catch (...) { post_drain_fixture::callback_fail_fast("fixture.event_observer"); }
void __cdecl event_callback(unsigned p) noexcept try {
    world->event_callback=0;
    if(callback_mode==1) { // terminal replacement: original hook first, then poison
        invoke(lifecycle,{p}); reinterpret_cast<PostDrainPlayer *>(p)->poison=1;
        world->player=raw(replacement);
        if(protect_poison) {
            DWORD old=0;
            if(!VirtualProtect(reinterpret_cast<void *>(p),4096,PAGE_NOACCESS,&old)) throw std::runtime_error("poison protection");
            poisoned_page=p;
        }
    } else if(callback_mode==2) { // exact-address reconstruction, not pointer identity
        invoke(construct,{p,at(p,4)});
    } else if(callback_mode==3) controls->clear_gamepad();
    else if(callback_mode==4) invoke(reset_fn,{p});
    else if(callback_mode==5) invoke(enable_fn,{p,0});
    else if(callback_mode==6) { // nested ordinary drain: original once, no nested host emission
        invoke(drain_fn,{world->manager,7,0});
    }
} catch (...) { post_drain_fixture::callback_fail_fast("fixture.event_callback"); }
void __cdecl getter_callback(unsigned p) noexcept try {
    world->getter_callback=0; event_callback(p);
} catch (...) { post_drain_fixture::callback_fail_fast("fixture.getter_callback"); }
void __cdecl after_drain(unsigned p) noexcept try {
    // The real production later writer is queued, even when it releases a
    // keyboard key after a prior frame. It cannot directly erase current pad13.
    auto frame=controls->take();
    queue_normal_keyboard_movement(frame,keyboard_moving,keyboard_observed,[] { return true; },
        [&](uint32_t type,float x,float y) {
            PostDrainEvent e{0,type,x,y}; invoke(queue_fn,{world->manager,raw(&e)});
        });
    if(world->native_later_writer) invoke(world->native_later_writer,{p});
    if(callback_mode==10) invoke(reset_fn,{p});
    if(callback_mode==11) invoke(enable_fn,{p,0});
    if(callback_mode==12) {
        const auto actor=at(p,4);
        queue_mouse_look(at<GuestLookInput>(p,0x48),at<float>(actor,0x68),at<float>(actor,0x60),{-.2f,.3f});
    }
} catch (...) { post_drain_fixture::callback_fail_fast("fixture.after_drain"); }
}
#include "play_gamepad_fixture.h"
int main(int argc,char **argv) {
    try {
        check(argc>=2,"fixture ELF required");
        bool bad_queue=false,bad_caller=false,bad_role=false;
        bool play_routing=false,omit_play_poll=false,omit_play_gate=false;
        const char *original_file=nullptr;
        for(int i=2;i<argc;++i) {
            const std::string_view option=argv[i];
            if(option=="--play-routing") play_routing=true;
            else if(option=="--omit-play-poll") { play_routing=true; omit_play_poll=true; }
            else if(option=="--omit-play-gate") { play_routing=true; omit_play_gate=true; }
            else if(option=="--queue-host-packets") bad_queue=true;
            else if(option=="--retain-old-caller") bad_caller=true;
            else if(option=="--omit-role-revocation") bad_role=true;
            else if(option=="--original" && i+1<argc) original_file=argv[++i];
            else throw std::runtime_error("unknown fixture option");
        }
        if(original_file && !std::filesystem::exists(original_file)) return 77;
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        auto fn=[&](const char *name) { return nova::abi::require_function(image,name); };
        world=reinterpret_cast<PostDrainWorld *>(image.symbol("post_drain_world")); check(world,"world symbol");
        PlayerPage old_page; auto &old=*old_page.data;
        alignas(16) PostDrainPlayer fresh{},foreign{};
        protect_poison=!bad_caller;
        alignas(16) std::array<uint8_t,0x700> level{};
        alignas(16) std::array<uint8_t,0x140> actor{},actor2{};
        alignas(16) std::array<uint8_t,0x100> normal{},scheme{},input_template{},weapon{},move{},look{};
        std::array<uint32_t,24> scheme_table{},look_table{};
        std::array<uint8_t,0x18820> app{};
        uint32_t state=image.symbol("_ZTV9GS_InGame")+8;
        auto init=[&](PostDrainPlayer &p,std::array<uint8_t,0x140> &a) {
            p={}; const auto value=raw(&p);
            at(value)=image.symbol("_ZTV15PlayerComponent")+8; at(value,4)=raw(a.data());
            for(auto head : {0xbcu,0xc4u,0xccu}) { at(value,head)=value+head; at(value,head+4)=value+head; }
            at<float>(value,0x2c)=1.f;
            at<uint8_t>(value,0xd)=1; at(value,0xfc)=raw(input_template.data());
            at(value,0x170)=raw(weapon.data()); at(value,0x100)=raw(a.data())+0x68;
            at(value,0x104)=raw(a.data())+0x60; at(value,0x1b0)=0x1234; at(value,0x2f0)=1000;
        };
        init(old,actor); init(fresh,actor2); init(foreign,actor2);
        at(raw(input_template.data()),4)=60; at(raw(input_template.data()),8)=2;
        at<float>(raw(input_template.data()),0xc)=3.f; at<float>(raw(input_template.data()),0x10)=5.f;
        at<float>(raw(level.data()),0x23c)=1.f; at<float>(raw(level.data()),0x240)=.5f; at<uint8_t>(raw(input_template.data()),0x5d)=1;
        at(raw(normal.data()),0xc)=raw(scheme.data());
        at(raw(scheme.data()))=raw(scheme_table.data());
        scheme_table[0x28/4]=fn("_ZN14ControlScheme414GetMoveControlEv"); scheme_table[0x2c/4]=fn("_ZN14ControlScheme416GetRotateControlEv");
        look_table[0x2c/4]=fn("fixture_inactive"); at(raw(look.data()))=raw(look_table.data());
        at(raw(scheme.data()),0xc)=raw(move.data()); at(raw(scheme.data()),0x18)=raw(look.data());
        at<uint8_t>(raw(move.data()),0x1d)=at<uint8_t>(raw(look.data()),0x1d)=1;
        at(raw(level.data()),0x17c)=at(raw(level.data()),0x178)=raw(normal.data());
        world->level=raw(level.data()); world->player=raw(&old); world->app=raw(app.data());
        world->state=raw(&state); world->manager=raw(world);
        at(image.symbol("_ZN18GlobalEventManager9SingletonE"))=raw(world);
        std::unique_ptr<post_drain_original::Gate> original_gate;
        if(original_file) original_gate=std::make_unique<post_drain_original::Gate>(original_file,*world,image);
        Controls input(ControlsConfig{}); input.gameplay=true; controls=&input;
        NativeGamepadPostDrain native(image,input); adapter=&native;
        lifecycle=fn("_ZN15PlayerComponent14ResetComponentEv"); construct=fn("_ZN15PlayerComponentC1EP11CGameObject");
        enable_fn=fn("_ZN15PlayerComponent6EnableEb"); reset_fn=fn("fixture_reset");
        queue_fn=fn("_ZN12EventManager9raiseSyncERK6IEvent"); drain_fn=fn("_ZN11Application7_UpdateEii");
        world->reset_observer=raw(reinterpret_cast<void *>(&reset_observer));
        world->event_observer=raw(reinterpret_cast<void *>(&event_observer));
        world->after_drain_callback=raw(reinterpret_cast<void *>(&after_drain));
        auto step=[&](uint32_t dt=20) { native.begin_frame(); invoke(drain_fn,{raw(world),dt,0}); native.end_frame(); };
        if(play_routing) {
            play_gamepad_fixture::run(input,native,step,raw(&old),raw(actor.data()),raw(move.data()),raw(look.data()),
                omit_play_poll,omit_play_gate);
            return 0;
        }
        GamepadSample pad; pad.device=7; pad.generation=1;
        auto neutral=[&] { pad.axes={}; pad.held.reset(); pad.neutral=true; input.submit_gamepad(pad); };
        auto sample=[&](GamepadAxes axes) { pad.axes=axes; pad.neutral=false; input.submit_gamepad(pad); };
        // No-controller path: only the caller's own acquisition, no added query.
        auto queries=world->acquisitions; step(0);
        check(world->acquisitions==queries+1 && world->immediate==0 && old.consumers==1 && old.last_dt==0,
            "keyboard-only exact original caller/consumer once; no analog queries");
        input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W)); step(17);
        check(world->queue_count==1 && nearly_equal(at<float>(raw(&old),0x24),0),"keyboard retains original queued latency");
        step(17); check(nearly_equal(at<float>(raw(&old),0x24),2.5f) && world->immediate==0,"keyboard-only next native drain preserved");
        input.handle(key(SDL_EVENT_KEY_UP,SDLK_W)); step(); step();
        // First observed owner and each new device require a subsequent neutral.
        sample({.5f,-.25f,.25f,.5f}); step();
        check(world->immediate==0,"first held report cannot authorize a newly bound actor");
        // T13b3f: denial cleanup belongs only to a prior host movement contribution.
        neutral(); sample({.5f,-.25f,0,0});
        at<uint8_t>(raw(move.data()),0x1d)=0;
        at<float>(raw(&old),0x1c)=7.f; at<float>(raw(&old),0x24)=-9.f;
        const auto fresh_denied_sends=world->immediate;
        step(); step();
        check(world->immediate==fresh_denied_sends && at<float>(raw(&old),0x1c)==7.f && at<float>(raw(&old),0x24)==-9.f,
            "fresh denied held pad emits no movement event and preserves native sentinel");
        at<uint8_t>(raw(move.data()),0x1d)=1; step();
        check(world->immediate==fresh_denied_sends+1 && nearly_equal(at<float>(raw(&old),0x1c),.75f) && nearly_equal(at<float>(raw(&old),0x24),.625f),
            "permitted pad establishes its native movement contribution");
        at<uint8_t>(raw(move.data()),0x1d)=0;
        world->callback_type=14; world->event_callback=raw(reinterpret_cast<void *>(&event_callback));
        const auto owned_denied_sends=world->immediate; step();
        check(world->immediate==owned_denied_sends+1 && world->event_callback==0 &&
            at<float>(raw(&old),0x1c)==0 && at<float>(raw(&old),0x24)==0,
            "permission loss releases prior host movement exactly once");
        at<float>(raw(&old),0x1c)=11.f; at<float>(raw(&old),0x24)=-13.f;
        step(); step();
        check(world->immediate==owned_denied_sends+1 && at<float>(raw(&old),0x1c)==11.f && at<float>(raw(&old),0x24)==-13.f,
            "continued denial is silent after owned release and preserves later native sentinel");
        at<uint8_t>(raw(move.data()),0x1d)=1; invoke(reset_fn,{raw(&old)});
        // End T13b3f movement denial cases; retain all original cases below.
        neutral(); sample({.5f,-.25f,.25f,.5f});
        world->queue_host=bad_queue ? 1u : 0u;
        const auto before=old.consumers,queued=world->queued,timer=at(raw(&old),0x2f0);
        const auto yaw=at<float>(raw(actor.data()),0x68),pitch=at<float>(raw(actor.data()),0x60);
        step(20);
        if(bad_queue) {
            // Actual copied packet is retained, source is revoked and native reset
            // occurs, THEN its queued9 reauthorizes input at the following drain.
            check(world->queue_count==2,"negative really retained both copied host packets");
            input.clear_gamepad(); invoke(reset_fn,{raw(&old)});
            world->queue_host=0;
            invoke(fn("_ZN12EventManager12UpdateEventsEi"),{raw(world),20});
            invoke(fn("fixture_player_caller"),{raw(&old),20});
            check(nearly_equal(at<float>(raw(actor.data()),0x68),yaw),"STALE_COPIED_PACKET_REAUTHORIZED_AFTER_REVOCATION");
            return 2;
        }
        check(world->queued==queued && world->queue_count==0,"host analog has no queue append or copied packet to survive revocation");
        check(old.consumers==before+1 && old.look_calls==old.consumers && old.last_dt==20,
            "stable post-drain path reaches immediate native caller originals exactly once");
        check(at(raw(&old),0x2f0)==timer-20 && world->forwarded_result==0xd0000000u,
            "exact caller post-input write and original drain result are preserved");
        check(nearly_equal(at<float>(raw(actor.data()),0x68),yaw-.2f) && nearly_equal(at<float>(raw(actor.data()),0x60),pitch-1.f),
            "producer -> later keyboard writer -> native consumer preserves yaw dt*.02 and per-call relative pitch");
        check(nearly_equal(at<float>(raw(&old),0x1c),.75f) && nearly_equal(at<float>(raw(&old),0x24),.625f),
            "continuous movement survives later normal keyboard path with native signs/scales");
        if(original_gate) check(world->velocity_samples>0 && world->velocity_dt==20 &&
            nearly_equal(world->consumed_move_x,.75f) && nearly_equal(world->consumed_move_y,.625f),
            "original UpdateFromInput feeds surviving movement to its real ComputeControlVelocity CALL");
        const auto zero_yaw=at<float>(raw(actor.data()),0x68),zero_pitch=at<float>(raw(actor.data()),0x60);
        step(0);
        check(nearly_equal(at<float>(raw(actor.data()),0x68),zero_yaw) && nearly_equal(at<float>(raw(actor.data()),0x60),zero_pitch-1.f),"dt0 leaves yaw but retains native pitch cadence");
        input.replace_sensitivity({8.f,1.9f}); step(0x80000001u);
        check(old.last_dt==0x80000001u && nearly_equal(at<float>(raw(&old),0x48),.5f),"exact signed dt bits; mouse settings do not scale native units");
        at<float>(raw(actor.data()),0x68)=0;
        // Later authoritative writers win: never restore a cached pad rate.
        callback_mode=10; const auto reset_yaw=at<float>(raw(actor.data()),0x68); step();
        check(nearly_equal(at<float>(raw(actor.data()),0x68),reset_yaw) && at<float>(raw(&old),0x64)==0,"later native reset defeats delivered analog");
        callback_mode=11; step(); check(at<uint8_t>(raw(&old),0xd)==0 && at<float>(raw(&old),0x48)==0,"later native disable owns its reset");
        pad.held.set(static_cast<size_t>(Action::aim)); input.submit_gamepad(pad); input.clear();
        const auto temporary_revision=input.gamepad_revision();
        native.target_observed(world->level,world->player,raw(normal.data()),raw(scheme.data()),false);
        check(input.gamepad_revision()==temporary_revision,"temporary normal disable does not revoke physical held source");
        check(input.continuous_gamepad().sample.held.test(static_cast<size_t>(Action::aim)),"temporary native clear preserves physical held desire");
        invoke(enable_fn,{raw(&old),1}); callback_mode=0; step();
        check(at<float>(raw(&old),0x48)==.5f,"native permission resumes same source without terminal rearm");
        callback_mode=12; const auto mouse_yaw=at<float>(raw(actor.data()),0x68),mouse_pitch=at<float>(raw(actor.data()),0x60); step();
        check(nearly_equal(at<float>(raw(actor.data()),0x68),mouse_yaw-.2f) && nearly_equal(at<float>(raw(actor.data()),0x60),mouse_pitch+.3f),"later O07 mouse queue owns both axes without cached analog restore");
        callback_mode=0;
        // Permission and source changes before admission cannot leave a host copy.
        at<uint8_t>(raw(look.data()),0x1d)=0; step(); check(at<float>(raw(&old),0x48)==0,"fresh rotate permission refuses look");
        at<uint8_t>(raw(look.data()),0x1d)=1;
        input.clear_gamepad(); step(); check(at<float>(raw(&old),0x1c)==0 && world->queue_count==0,"device revocation releases owned movement without a stale packet");
        neutral(); sample({.5f,0,.25f,.5f});
        // Reset within immediate dispatch must prevent the second event from
        // restoring earlier movement, even with permissions still true.
        world->callback_type=9; world->event_callback=raw(reinterpret_cast<void *>(&event_callback)); callback_mode=4;
        const auto reset_calls=world->immediate; step();
        check(world->immediate==reset_calls+1 && at<float>(raw(&old),0x1c)==0 && at<float>(raw(&old),0x48)==0,"callback reset cancels remaining delivery without restoring cached axes");
        callback_mode=3; world->event_callback=raw(reinterpret_cast<void *>(&event_callback)); step();
        check(world->queue_count==0 && !input.continuous_gamepad().source_present,"callback device revocation leaves no copied host event");
        step(); check(at<float>(raw(&old),0x48)==0,"next ordinary admission releases already-delivered look after source revocation");
        neutral(); sample({.5f,0,.25f,.5f});
        // A revocation in a fresh native getter is checked before positive delivery.
        callback_mode=3; world->getter_callback=raw(reinterpret_cast<void *>(&getter_callback));
        const auto getter_sends=world->immediate; step();
        check(world->immediate==getter_sends && !input.continuous_gamepad().admitted,"fresh acquisition source revocation cannot authorize an event");
        neutral(); sample({.5f,0,.25f,.5f});
        // Reconstructed receiver cannot inherit a second event or an old sample.
        callback_mode=2; world->event_callback=raw(reinterpret_cast<void *>(&event_callback));
        const auto reconstruct_calls=world->immediate; step();
        check(world->immediate==reconstruct_calls+1 && !input.continuous_gamepad().admitted && old.obsolete==0,"same-address C1 retires source before next event");
        callback_mode=0; step(); neutral(); sample({.5f,0,.25f,.5f});
        // Actual outer caller sequence reselects the receiver after immediate
        // dispatch; its UpdateLookAt/+1b0/+2f0 use only that fresh receiver.
        replacement=&fresh; callback_mode=1; world->stale_caller=bad_caller ? 1u : 0u;
        world->event_callback=raw(reinterpret_cast<void *>(&event_callback));
        const auto old_consumers=old.consumers; step(); unpoison();
        check(old.obsolete==0,"OBSOLETE_NATIVE_CALLER_CONTINUATION_AFTER_DELIVERY");
        check(old.consumers==old_consumers && fresh.consumers==1 && fresh.look_calls==1,"replacement uses freshly selected caller receiver exactly once");
        callback_mode=0; step(); neutral(); sample({.25f,0,.5f,0}); step();
        check(fresh.events>=2 && at<float>(raw(&fresh),0x48)==1.f,"new actor requires observed owner then new neutral and fresh sample");
        // Nested ordinary calls forward their original once while injection is
        // quarantined across the entire outer dispatch and fresh-query sequence.
        callback_mode=6; world->callback_type=9; world->event_callback=raw(reinterpret_cast<void *>(&event_callback));
        const auto nested_drains=world->drains,nested_sends=world->immediate;
        step();
        check(world->drains==nested_drains+2 && world->immediate==nested_sends+2,"nested ordinary drain forwards once without duplicate host delivery");
        callback_mode=0;
        // Connected later role handover: exact production selector/transition,
        // adapter refresh observer, Controls::take and role action consumer.
        input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W));
        pad.held.set(static_cast<size_t>(Action::aim)); pad.held.set(static_cast<size_t>(Action::fire));
        input.submit_gamepad(pad);
        check(input.continuous_gamepad().admitted,"normal source admitted before later role handover");
        InputTarget selected{TargetKind::normal,world->player};
        at<uint8_t>(world->player,0xd)=0;
        auto next=select_input_target(true,{world->player,false,false,false},{raw(&foreign),true,true,true});
        check(next.kind==TargetKind::mech,"actual selector chose unsupported role");
        if(!bad_role) native.target_observed(world->level,world->player,raw(normal.data()),raw(scheme.data()),true);
        check(input.continuous_gamepad().keyboard_held.test(static_cast<size_t>(Action::forward)),
            "pad authority revocation does not erase keyboard state");
        change_input_target(selected,next,[&](InputTarget old_target) { invoke(reset_fn,{old_target.component}); },[&] { input.clear(); });
        // Existing handover clears keyboard edges; a fresh keyboard report must
        // remain available, while a held/new pad edge cannot enter the role.
        input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W));
        pad.held.set(static_cast<size_t>(Action::reload)); input.submit_gamepad(pad);
        const auto role_frame=input.take();
        check(!role_frame.down(Action::aim) && !role_frame.down(Action::fire) && !role_frame.edge(Action::reload),
            "NORMAL_PAD_AUTHORITY_LEAKED_TO_LATER_ROLE_TAKE");
        check(role_frame.down(Action::forward),"keyboard remains usable by role after native handover");
        MechActions role_actions; unsigned role_events=0;
        apply_mech_actions(role_frame,{true,true,true},role_actions,[&](uint32_t type,float x,float y) {
            check(type==13 && x==0 && y==-1,"unsupported role receives keyboard movement only"); ++role_events;
        });
        check(role_events==1 && !role_actions.primary && !role_actions.secondary,"no controller-only role action");
        input.handle(key(SDL_EVENT_KEY_UP,SDLK_W)); at<uint8_t>(world->player,0xd)=1;
        const auto return_sends=world->immediate; step();
        input.submit_gamepad(pad); step();
        check(world->immediate==return_sends && !input.continuous_gamepad().admitted,
            "return to normal observes owner without reviving prior held source");
        neutral(); sample({.25f,0,.5f,0}); step();
        check(input.continuous_gamepad().admitted && at<float>(world->player,0x48)==1.f,
            "normal return requires fresh neutral then fresh sample");
        const auto foreign_revision=input.gamepad_revision();
        const auto foreign_lifetimes=foreign.lifetimes;
        check(invoke(lifecycle,{raw(&foreign)})==0xa5000001u && input.gamepad_revision()==foreign_revision && foreign.lifetimes==foreign_lifetimes+1,"foreign lifecycle forwards once without local source rearm");
        const auto sends=world->immediate,drains=world->drains;
        invoke(fn("_ZN12EventManager12UpdateEventsEi"),{raw(world),0x81234567u});
        check(world->drains==drains+1 && world->drain_dt==0x81234567u && world->immediate==sends,"foreign native drain unchanged and no analog injection");
        std::puts("post-drain production adapter / native-layout caller-consumer contract: passed");
        return 0;
    } catch(const std::exception &e) { unpoison(); std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
