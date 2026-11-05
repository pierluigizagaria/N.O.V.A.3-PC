#include "input/native_controls.h"
#include "bionic/runtime.h"
#include "fixtures/driver_contract.h"
#include "fixtures/mech_contract.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
using namespace nova::input;
using nova::abi::invoke;
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
void close_to(float a,float b,const char *message,float tolerance=.0003f) {
    check(std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=tolerance,message);
}
uint32_t address(auto &object) { return reinterpret_cast<uint32_t>(&object); }
uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
InputTarget owner(unsigned roles=1,unsigned mode=2) {
    return {TargetKind::driver,44,{101,102,103,roles,mode,104,105,106,107}};
}
DriverOwner context(InputTarget target) { return {target,1,104,104,105,false,false,false,true}; }
void selection() {
    // Exercise production offset reads, not a second guessed role enum/layout.
    std::map<uint32_t,uint32_t> memory{{1000+0x17c,104},{44+0xd,1},{44+0x17c,101},{44+4,102},
        {44+0x108,103},{44+0x1ac,1},{44+0x1ad,0},{101+0x25c,44},{101+4,108},
        {104+0xc,105},{104+0x28,106},{104+0x2c,107}};
    auto identify=[&](unsigned mode) {
        return identify_driver(1000,44,[&](uint32_t p,uint32_t off) { return memory[p+off]; },
            [&](uint32_t p,uint32_t off) { return memory[p+off]; },[&](uint32_t) { return mode; });
    };
    for(unsigned roles=0;roles<4;++roles) for(unsigned mode=0;mode<5;++mode) {
        memory[44+0x1ac]=roles&1; memory[44+0x1ad]=(roles>>1)&1;
        auto target=identify(mode);
        check(bool(target)==bool((roles&1) && mode<4),"exact native driver capability; shooter-only/unknown config rejected");
        if(target) check(target==owner(roles,mode),"production driver identity includes both flags and native configuration");
    }
    memory[44+0x1ac]=1; memory[44+0x1ad]=0;
    for(uint32_t cell : {44u+0xd,44u+0x17c,44u+4,44u+0x108,1000u+0x17c,104u+0xc,101u+0x25c,101u+4}) {
        auto saved=memory[cell]; memory[cell]=0; check(!identify(2),"missing native binding/backlink rejected"); memory[cell]=saved;
    }
    for(unsigned mask=0;mask<256;++mask) {
        TargetCandidate normal{11,(mask&1)!=0,true,true},mech{22,(mask&2)!=0,true,true};
        TargetCandidate gun{33,(mask&4)!=0,(mask&64)==0,true};
        TargetCandidate jeep{44,(mask&8)!=0,(mask&16)!=0,(mask&32)!=0,owner().driver};
        const bool valid_context=(mask&128)!=0;
        auto target=select_input_target(valid_context,normal,mech,gun,jeep);
        const unsigned count=unsigned(normal.enabled)+unsigned(mech.enabled)+unsigned(gun.enabled)+unsigned(jeep.enabled);
        const bool expected=valid_context && count==1 && (!gun.enabled || gun.alive) &&
            (!jeep.enabled || (jeep.alive && jeep.interactive));
        check(bool(target)==expected,"256 exclusive on-foot/mech/stationary/driver/unsupported-mounted-gun combinations");
    }
    auto selected=owner(); auto now=context(selected);
    check(driver_owner_matches(selected,1,now),"eligible driver owner");
    for(unsigned field=0;field<18;++field) {
        auto bad=now;
        switch(field) {
        case 0: bad.level=2; break; case 1: bad.normal=9; break; case 2: bad.active_interface=9; break;
        case 3: bad.scheme=9; break; case 4: bad.normal_enabled=true; break; case 5: bad.mech_enabled=true; break;
        case 6: bad.machinegun_enabled=true; break; case 7: bad.ingame=false; break;
        case 8: bad.target.component=9; break; case 9: bad.target.driver.vehicle=9; break;
        case 10: bad.target.driver.actor=9; break; case 11: bad.target.driver.input_template=9; break;
        case 12: bad.target.driver.capabilities=2; break; case 13: bad.target.driver.configuration=1; break;
        case 14: bad.target.driver.normal=9; break; case 15: bad.target.driver.scheme=9; break;
        case 16: bad.target.driver.accelerator=9; break; default: bad.target.driver.brake=9; break;
        }
        check(!driver_owner_matches(selected,1,bad),"every live identity/interface/enable/application field rejected independently");
    }
    check(!live_input_target(selected,1,2,11,22,33,44) && !live_input_target(selected,1,1,11,22,33,45),"fresh level/local getter before dereference");
    unsigned dereferences=0;
    check(!driver_lifetime_matches(selected,1,2,44,[&] { ++dereferences; return selected; }) && dereferences==0,
        "entry-level mismatch rejects reused component/context addresses before native memory read");
    check(!driver_lifetime_matches(selected,1,1,45,[&] { ++dereferences; return selected; }) && dereferences==0,
        "fresh getter mismatch rejects before old component memory read");
    std::puts("driver selection: native offset/role/configuration rejects, 256 exclusive contexts and 18 live fields passed");
}
struct Harness;
Harness *current=nullptr;
uint32_t __cdecl pedal_query(uint32_t object,uint32_t channel);
uint32_t camera_original=0,pedals_original=0;
std::array<uint32_t,3> draw_originals{};
uint32_t hidden_move=0,hidden_rotate=0;
bool live_draw=false;
void __cdecl draw_hook(uint32_t object,uint32_t alpha) {
    auto &button=*reinterpret_cast<DriverDrawContract *>(object);
    if(live_draw && driver_touch_control(object,hidden_move,hidden_rotate)) return;
    invoke(draw_originals.at(button.type),{object,alpha});
}
struct Harness {
    DriverContract native{};
    InputTarget target=owner(),actual=target;
    Controls controls{ControlsConfig{}};
    DriverActions actions;
    DriverPedalRelease pedal;
    FrameInput frame;
    uint32_t camera=0,pedals=0,event=0;
    bool pending=false,eligible=true,same_level=true,getter_matches=true,rotate=true;
    unsigned writes=0,query_count=0;
    bool change_on_first_query=false;
    Harness(const nova::ElfImage &image) {
        current=this; controls.gameplay=true;
        native.scale_x=2; native.scale_y=3; native.gain=.7f; native.driver_distance=native.shooter_distance=1;
        native.pitch_minimum=-25; native.pitch_maximum=35; native.enabled=native.ingame=1; native.mode=2; native.dt=16; native.query=reinterpret_cast<uint32_t>(&pedal_query);
        camera=nova::abi::require_function(image,"driver_camera"); pedals=nova::abi::require_function(image,"driver_pedals");
        event=nova::abi::require_function(image,"driver_event");
    }
    ~Harness() { current=nullptr; }
    bool lifetime(InputTarget cached) const {
        return driver_lifetime_matches(cached,1,same_level ? 1 : 2,getter_matches ? 44 : 9,[&] { return actual; });
    }
    bool live() const { return eligible && native.enabled && lifetime(target) && driver_owner_matches(target,1,context(actual)); }
    void key(Uint32 type,SDL_Keycode key) { SDL_Event e{}; e.type=type; e.key.key=key; controls.handle(e); }
    void motion(float x,float y) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.xrel=x; e.motion.yrel=y; controls.handle(e); }
    void take(unsigned axes=3,bool move=true) {
        frame=controls.active() ? controls.take() : FrameInput{};
        apply_driver_actions(frame,move,axes,actions,[&] { return live(); },[&](uint32_t type,float x,float y) {
            invoke(event,{address(native),type,bits(x),bits(y)});
        });
        pending=live() && (frame.dx!=0 || frame.dy!=0);
    }
    void boundary(uint32_t original,unsigned dt) {
        const auto cached=target;
        const bool allowed=pending && controls.active() && live() && rotate;
        const bool cleared=native.one_shot!=0;
        auto sample=allowed ? driver_mouse_input(frame.dx,frame.dy,native.scale_x,native.scale_y,
            native.accelerated!=0,native.gain,controls.config) : std::optional<Look>{};
        driver_mouse_boundary(pending,allowed,sample,[&] { return !cleared && lifetime(cached) && !native.one_shot; },
            [&] { return Look{native.raw_x,native.raw_y}; },[&](Look raw) { native.raw_x=raw.yaw; native.raw_y=raw.pitch; ++writes; },
            [&] { invoke(original,{address(native),dt}); if(native.mutate_owner) actual.driver.vehicle=999; });
    }
    void pedal_boundary(uint32_t original) {
        const auto cached=pedal.owner;
        driver_pedal_boundary(pedal,[&] { return lifetime(cached); },[&] {
            invoke(original,{address(native)});
        },[&] { return native.accelerator_timer==0 && native.brake_timer==0; });
    }
    void cleanup() {
        release_driver_actions(actions,lifetime(target) && native.enabled,[&] { invoke(event,{address(native),14,0,0}); });
        if(pedal.owned) {
            if(lifetime(pedal.owner) && native.enabled) { pedal.pending=true; invoke(pedals,{address(native)}); }
            else pedal={};
        }
        controls.clear(); pending=false; frame={};
    }
};
uint32_t __cdecl pedal_query(uint32_t object,uint32_t channel) {
    check(current && object==address(current->native) && channel<2,"exact native pedal query receiver/channel");
    auto &h=*current;
    ++h.query_count;
    if(channel==1 && h.change_on_first_query) h.actual.driver.vehicle=999;
    const auto button=channel ? h.target.driver.brake : h.target.driver.accelerator;
    if(h.pedal.pending) {
        auto value=driver_pedal_input(h.pedal.owner,button,false,true,false,h.frame,[&] { return h.lifetime(h.pedal.owner); });
        if(value) return *value;
    }
    auto value=driver_pedal_input(h.target,button,h.controls.active(),false,true,h.frame,[&] { return h.live(); });
    return value ? *value : channel ? h.native.brake_pedal : h.native.accelerator;
}
void __cdecl camera_hook(uint32_t object,uint32_t dt) { check(current && object==address(current->native),"exact camera object"); current->boundary(camera_original,dt); }
void __cdecl pedals_hook(uint32_t object) { check(current && object==address(current->native),"exact native pedal API receiver"); current->pedal_boundary(pedals_original); }
void actions(const nova::ElfImage &image) {
    FrameInput forward; forward.held.set(static_cast<size_t>(Action::forward));
    FrameInput back; back.held.set(static_cast<size_t>(Action::back));
    for(unsigned mode=0;mode<4;++mode) for(unsigned roles : {1u,3u}) {
        auto selected=owner(roles,mode);
        unsigned queries=0;
        auto query=[&](uint32_t button,const FrameInput &frame,bool active,bool enabled,bool release,bool live) {
            return driver_pedal_input(selected,button,active,release,enabled,frame,[&] { ++queries; return live; });
        };
        auto w=query(106,forward,true,true,false,true),s=query(107,back,true,true,false,true);
        check((mode==1 && w && *w==1 && s && *s==1) || (mode!=1 && !w && !s),"W/S map only to native type-1 pedals, combined SP allowed");
        check(!query(999,forward,true,true,false,true),"neighboring shooter/button never overridden");
        if(mode==1) {
            check(query(106,forward,true,false,false,true)==0,"native disabled pedal blocks held key");
            check(!query(106,forward,false,true,false,true) && !query(106,forward,true,true,false,false),"inactive or lost live owner never receives pedal input");
            check(query(106,forward,false,false,true,true)==0,"pending release forces only its owned pedal false through pause");
            check(!query(107,back,true,true,false,false),"identity rechecked separately at every native pedal query");
        } else check(queries==0,"other native configurations do not query driver-pedal eligibility");
    }

    for(unsigned axes=0;axes<4;++axes) {
        Harness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.key(SDL_EVENT_KEY_DOWN,SDLK_D); h.take(axes);
        close_to(h.native.native_x,(axes&1) ? 1.f : 0.f,"native X axis lock");
        close_to(h.native.native_y,(axes&2) ? 1.f : 0.f,"native Y axis lock");
        h.cleanup(); h.cleanup(); check(h.native.releases==unsigned(axes!=0),"actual directed move release exactly once");
    }
    for(unsigned loss=0;loss<6;++loss) {
        Harness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.take();
        if(loss==0) h.same_level=false; if(loss==1) h.getter_matches=false; if(loss==2) h.actual.driver.vehicle=9;
        if(loss==3) h.actual.driver.capabilities=2; if(loss==4) h.actual.driver.configuration=1; if(loss==5) h.native.enabled=0;
        h.cleanup(); check(h.native.releases==0,"native teardown owns stale/disabled/same-pointer role/vehicle/configuration changes");
    }
    Harness blocked(image); blocked.eligible=false; blocked.key(SDL_EVENT_KEY_DOWN,SDLK_W); blocked.take();
    check(blocked.native.events==0,"inactive driver receives no host input");
    for(auto next : {InputTarget{TargetKind::normal,11},InputTarget{TargetKind::mech,22},InputTarget{TargetKind::machinegun,33},InputTarget{},owner(2),owner(3),owner(1,1)}) {
        Harness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.motion(7,8); h.take();
        change_input_target(h.target,next,[&](InputTarget old) { check(old==h.target,"old owner released before publication"); h.cleanup(); },
            [&] { h.controls.clear(); h.pending=false; h.frame={}; });
        check(h.native.releases==1 && !h.pending && !h.controls.take().down(Action::forward),"transition clears held keys/counts and releases exactly one old owner");
    }
    bool exit=true; check(!mech_exit_edge(exit,false) && exit,"disabled exit preserves native eligibility");
    check(mech_exit_edge(exit,true) && !mech_exit_edge(exit,true),"one enabled contextual E exit edge");
    for(unsigned gate=0;gate<4;++gate) {
        Harness h(image); h.native.mode=1; h.target=h.actual=owner(1,1);
        h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.key(SDL_EVENT_KEY_DOWN,SDLK_S); h.take(1);
        h.pedal={h.target,1,true,false}; invoke(h.pedals,{address(h.native)});
        check(h.native.accelerator_timer==16 && h.native.brake_timer==16,"native held pedal timer consumer");
        if(gate==0) h.native.ingame=0;
        if(gate==1) h.same_level=false;
        if(gate==2) h.actual.driver.accelerator=9;
        if(gate==3) h.actual.driver.brake=9;
        h.cleanup();
        if(gate==0) {
            check(h.pedal.pending && h.native.accelerator_timer==16,"paused native input consumer retains release obligation");
            h.native.ingame=1; invoke(h.pedals,{address(h.native)});
            check(!h.pedal.owned && !h.native.accelerator_timer && !h.native.brake_timer,"actual resumed pedal completion acknowledges neutral input");
            auto calls=h.native.pedal_calls; h.cleanup(); check(h.native.pedal_calls==calls,"no duplicate native cleanup after acknowledgement");
        } else check(!h.pedal.owned && h.native.pedal_calls==1,"changed level/pedal identity never invokes stale API");
    }
    Harness natural(image); natural.native.mode=1; natural.target=natural.actual=owner(1,1);
    natural.key(SDL_EVENT_KEY_DOWN,SDLK_W); natural.take(1); natural.pedal={natural.target,1,true,false};
    invoke(natural.pedals,{address(natural.native)});
    natural.key(SDL_EVENT_KEY_UP,SDLK_W); natural.take(1); invoke(natural.pedals,{address(natural.native)});
    check(!natural.pedal.owned,"natural actual key-up consumer completes timer ownership without extra cleanup");
    Harness mid_query(image); mid_query.native.mode=1; mid_query.target=mid_query.actual=owner(1,1);
    mid_query.native.accelerator_timer=mid_query.native.brake_timer=16;
    mid_query.pedal={mid_query.target,1,true,true}; mid_query.change_on_first_query=true;
    invoke(mid_query.pedals,{address(mid_query.native)});
    check(mid_query.query_count==2 && mid_query.pedal.owned && mid_query.pedal.pending,
        "role/vehicle loss between actual native pedal queries prevents false acknowledgement");
    DriverPedalRelease replaced{owner(1,1),1,true,true};
    driver_pedal_boundary(replaced,[] { return true; },[&] { replaced={owner(3,1),1,true,true}; },[] { return true; });
    check(replaced.owned && replaced.owner.driver.capabilities==3,"old pedal completion cannot acknowledge a new callback owner");
    {
        Harness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.take();
        h.controls.overlay(true); check(!h.controls.active(),"overlay independently gates driver movement");
        h.cleanup(); h.cleanup(); h.controls.overlay(false); h.take();
        check(h.native.releases==1 && !h.actions.moving && !h.frame.down(Action::forward),"overlay executes actual driver move release and clears old input");
    }
    std::puts("driver actions: axis locks, directed release/lifetime, component transitions, guarded exit and native pedal completion passed");
}
void mouse(const nova::ElfImage &image) {
    for(bool accelerated : {false,true}) for(float coefficient : {-1.f,0.f,.1f,.7f,1.f,4.f,10.f,14.f})
    for(float count : {-10000.f,-90.f,-1.f,0.f,1.f,90.f,10000.f}) {
        Harness h(image); h.native.accelerated=accelerated; h.native.gain=coefficient;
        h.native.pitch_minimum=-100000; h.native.pitch_maximum=100000;
        h.motion(count,17); h.take(); invoke(h.camera,{address(h.native),16});
        close_to(h.native.yaw,-count*.1f,"inverse calibration through native gain (off/negative/low/quadratic/capped)",.001f);
        close_to(h.native.pitch,-1.7f,"native event9 Y convention",.0003f);
        close_to(h.native.raw_x,0,"raw X restored"); close_to(h.native.raw_y,0,"raw Y restored");
    }
    for(float gain : {.1f,.7f,1.f,4.f}) for(float threshold : {std::max(1/gain-1,0.f),10*std::max(10/gain-1,0.f)})
    for(float value : {std::nextafter(threshold,0.f),threshold,std::nextafter(threshold,std::numeric_limits<float>::infinity())}) {
        Harness h(image); h.native.accelerated=1; h.native.gain=gain; h.motion(value*20,0); h.take();
        invoke(h.camera,{address(h.native),16}); close_to(h.native.yaw,-value*2,"native float gain clamp boundary +/- one ULP",.002f);
    }
    for(unsigned sign=0;sign<16;++sign) {
        Harness h(image); h.native.shooter=sign&1; h.native.driver_distance=(sign&2) ? -1.f : 1.f;
        h.native.shooter_distance=(sign&4) ? -1.f : 1.f; h.native.invert_native=(sign&8)!=0;
        for(bool inverted : {false,true}) {
            h.controls.config.invert_y=inverted; h.native.pitch=0; h.motion(0,20); h.take(); invoke(h.camera,{address(h.native),1});
            const float distance=h.native.shooter ? h.native.shooter_distance : h.native.driver_distance;
            const float expected=2*(inverted ? -1.f : 1.f)*(distance>=0 ? 1.f : -1.f)*(h.native.invert_native ? 1.f : -1.f);
            close_to(h.native.pitch,expected,"host invert once and both native capability-distance/MP-sign branches");
        }
    }
    for(float gain : {.1f,1.f,4.f,12.f}) {
        Harness one(image); one.native.accelerated=1; one.native.gain=gain;
        one.motion(100,40); one.take(); invoke(one.camera,{address(one.native),90}); const float yaw=one.native.yaw,pitch=one.native.pitch;
        Harness parts(image); parts.native.accelerated=1; parts.native.gain=gain;
        for(unsigned n=0;n<10;++n) { parts.motion(10,4); parts.take(); invoke(parts.camera,{address(parts.native),n+1}); }
        close_to(parts.native.yaw,yaw,"count partition invariance through original nonlinear gain");
        close_to(parts.native.pitch,pitch,"coherent Y partition independent of direct-path dt");
    }
    for(auto pair : {std::pair{20.f,0.f},std::pair{0.f,20.f},std::pair{20.f,-13.f}}) {
        Harness h(image); h.native.raw_x=2; h.native.raw_y=-3; h.motion(pair.first,pair.second); h.take();
        invoke(h.camera,{address(h.native),16}); auto yaw=h.native.yaw; const auto writes=h.writes;
        invoke(h.camera,{address(h.native),16}); check(h.writes==writes && h.native.calls==2,"same sample supplied once; repeated original consumer still called");
        close_to(h.native.yaw,yaw-4,"second callback keeps nonzero original native input, no duplicate host contribution");
        close_to(h.native.raw_x,2,"native baseline X preserved"); close_to(h.native.raw_y,-3,"native baseline Y preserved");
    }
    for(auto pair : {std::pair{0.f,20.f},std::pair{20.f,0.f}}) {
        Harness h(image); h.native.raw_x=2; h.native.raw_y=-3; h.native.one_shot=1;
        h.motion(pair.first,pair.second); h.take(); invoke(h.camera,{address(h.native),16});
        close_to(h.native.raw_x,0,"native one-shot clear preserved even zero injected X");
        close_to(h.native.raw_y,0,"native one-shot clear preserved even zero injected Y");
        check(h.native.yaw!=0 || h.native.pitch!=0,"one-shot consumes cached locals before clearing");
    }
    for(unsigned locked=0;locked<4;++locked) {
        Harness h(image); if(locked&1) h.native.scale_x=0; if(locked&2) h.native.scale_y=0;
        h.motion(20,15); h.take(); invoke(h.camera,{address(h.native),16});
        close_to(h.native.yaw,(locked&1) ? 0.f : -2.f,"finite zero X preserves native axis lock while Y remains usable");
        close_to(h.native.pitch,(locked&2) ? 0.f : -1.5f,"finite zero Y preserves native axis lock while X remains usable");
        check(h.native.calls==1,"axis locks never skip native auto-centering/physics consumer");
    }
    for(unsigned invalid=0;invalid<6;++invalid) {
        Harness h(image); h.native.raw_x=2; h.native.raw_y=-3; h.motion(20,15); h.take();
        switch(invalid) {
        case 0: h.native.scale_x=std::numeric_limits<float>::infinity(); break;
        case 1: h.native.scale_y=std::numeric_limits<float>::quiet_NaN(); break;
        case 2: h.native.accelerated=1; h.native.gain=std::numeric_limits<float>::quiet_NaN(); break;
        case 3: h.frame.dx=std::numeric_limits<float>::infinity(); break;
        case 4: h.frame.dx=std::numeric_limits<float>::max(); h.native.scale_x=std::numeric_limits<float>::denorm_min(); break;
        default: h.frame.dy=std::numeric_limits<float>::quiet_NaN(); break;
        }
        invoke(h.camera,{address(h.native),16}); check(h.writes==0 && h.native.calls==1,"invalid conversion rejects entire pair and still forwards original");
        check(h.native.raw_x==2 && h.native.raw_y== -3,"invalid pair makes no partial native input write");
    }
    for(unsigned loss=0;loss<8;++loss) {
        Harness h(image); h.native.raw_x=2; h.native.raw_y=-3; h.motion(20,15); h.take();
        auto sample=driver_mouse_input(20,15,2,3,false,0,h.controls.config); const auto cached=h.target;
        driver_mouse_boundary(h.pending,true,sample,[&] { return h.lifetime(cached); },
            [&] { return Look{h.native.raw_x,h.native.raw_y}; },[&](Look v) { h.native.raw_x=v.yaw; h.native.raw_y=v.pitch; ++h.writes; },[&] {
                invoke(camera_original,{address(h.native),16});
                switch(loss) {
                case 0: h.same_level=false; break; case 1: h.getter_matches=false; break;
                case 2: h.actual.driver.vehicle=9; break; case 3: h.actual.driver.capabilities=3; break;
                case 4: h.actual.driver.configuration=1; break; case 5: h.actual.driver.input_template=9; break;
                case 6: h.actual.driver.normal=9; break; default: h.actual.driver.scheme=9; break;
                }
            });
        check(h.writes==1,"native mid-callback ownership loss prevents late restore into new context");
    }
    for(unsigned loss=0;loss<3;++loss) {
        Harness h(image); h.native.raw_x=2; h.native.raw_y=-3;
        bool pending=true; const auto cached=h.target;
        auto sample=driver_mouse_input(20,15,2,3,false,0,h.controls.config);
        driver_mouse_boundary(pending,true,sample,[&] { return h.lifetime(cached); },
            [&] { return Look{h.native.raw_x,h.native.raw_y}; },[&](Look value) { h.native.raw_x=value.yaw; h.native.raw_y=value.pitch; },[&] {
                invoke(camera_original,{address(h.native),16});
                if(loss==0) h.eligible=false;
                if(loss==1) h.controls.context(false,false);
                if(loss==2) h.controls.gameplay=false;
            });
        check(h.native.raw_x==2 && h.native.raw_y== -3,"same structural owner restores host input after callback eligibility/focus/pause loss");
    }
    Harness mutation(image); mutation.native.raw_x=2; mutation.native.raw_y=-3; mutation.native.mutate=1;
    mutation.motion(20,15); mutation.take(); invoke(mutation.camera,{address(mutation.native),16});
    check(mutation.native.raw_x==99 && mutation.native.raw_y== -27,"native same-identity raw writes preserved");
    Harness no_motion(image); no_motion.native.raw_x=2; no_motion.native.raw_y=-3; no_motion.take();
    invoke(no_motion.camera,{address(no_motion.native),16}); check(no_motion.writes==0,"no-motion frame leaves all raw fields native");
    Harness limits(image); limits.motion(2,10000); limits.take(); invoke(limits.camera,{address(limits.native),1});
    close_to(limits.native.pitch,-25,"native asymmetric camera limit remains authoritative");
    for(unsigned gate=0;gate<4;++gate) {
        Harness h(image); h.motion(20,15); h.take();
        if(gate==0) h.rotate=false; if(gate==1) h.eligible=false; if(gate==2) h.native.enabled=0; if(gate==3) h.controls.context(false,false);
        invoke(h.camera,{address(h.native),16}); check(h.writes==0 && h.native.calls==1,"rotation/context/disabled/focus guard retains native original");
        h.pending=false; invoke(h.camera,{address(h.native),16}); check(h.writes==0,"next frame discards unused deltas");
    }
    std::puts("driver camera: inverse native gain, count partitions, Y signs, d4, native mutations, lifetime/guards and once-only pair passed");
}
void drawing(const nova::ElfImage &image) {
    const auto setter=nova::abi::require_function(image,"mech_set_draw");
    for(bool scope : {false,true}) for(bool fail : {false,true}) {
        MechDrawControl exit{1,1,1,0},accelerator{1,0,1,0},brake{0,1,0,0},shooter{1,1,1,0},hud{1,1,0,0};
        unsigned draws=0;
        try {
            draw_driver_controls(scope,[&](uint32_t slot) { return slot==0x10 ? address(exit) : slot==0x28 ? address(accelerator) : address(brake); },
                [&](uint32_t p) { return static_cast<uint8_t>(reinterpret_cast<MechDrawControl *>(p)->visible); },
                [&](uint32_t p,bool visible) { invoke(setter,{p,visible}); },[&] {
                    ++draws; check(exit.visible==unsigned(!scope) && accelerator.visible==0 && brake.visible==unsigned(!scope),"driver exit/pedals only hidden");
                    check(shooter.visible && hud.visible && exit.enabled && exit.pressed && !brake.enabled,"SP shooter, HUD and native action state preserved");
                    if(fail) throw std::runtime_error("synthetic draw interruption");
                });
        } catch(const std::runtime_error &error) { check(fail && std::string(error.what())=="synthetic draw interruption","unexpected draw failure"); }
        check(draws==1 && exit.visible==1 && accelerator.visible==0 && brake.visible==1,"native drawing flags restored after normal/exception dispatch");
    }
    MechDrawControl shared{1,1,1,0},neighbor{1,1,1,0};
    unsigned fullscreen_calls=0;
    const auto fullscreen=nova::abi::require_function(image,"driver_fullscreen");
    draw_driver_scheme(true,[&](uint32_t) { return address(shared); },
        [&](uint32_t p) { return static_cast<uint8_t>(reinterpret_cast<MechDrawControl *>(p)->visible); },
        [&](uint32_t p,bool visible) { invoke(setter,{p,visible}); },[&] {
            check(!shared.visible && neighbor.visible,"exact move/rotate scheme scope retains shooter graphics");
            invoke(fullscreen,{address(fullscreen_calls)});
        });
    check(shared.visible && shared.sets==2,"aliased move/rotate drawing flags saved/restored once");
    check(fullscreen_calls==1,"native full-screen Draw input side effect retained while only paint controls are hidden");
    for(unsigned type=0;type<3;++type) {
        DriverDrawContract move{type,0,0},rotate{type,0,0},shooter{type,0,0};
        hidden_move=address(move); hidden_rotate=address(rotate);
        const char *names[]={"driver_analog_draw","driver_mobile_draw","driver_angular_draw"};
        auto entry=nova::abi::require_function(image,names[type]);
        std::array<uint8_t,6> saved{}; std::memcpy(saved.data(),reinterpret_cast<void *>(entry),6);
        {
            nova::abi::GuestHook hook(image,names[type],{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&draw_hook));
            draw_originals[type]=hook.original();
            live_draw=true; invoke(entry,{address(move),81}); invoke(entry,{address(rotate),82}); invoke(entry,{address(shooter),83});
            check(!move.draws && !rotate.draws && shooter.draws==1 && shooter.alpha==83,"actual checked draw hook hides driver controls and forwards neighboring shooter alpha unchanged");
            live_draw=false; invoke(entry,{address(move),84}); check(move.draws==1 && move.alpha==84,"outside-scope exact draw trampoline executes aligned MOVAPS");
        }
        check(!std::memcmp(saved.data(),reinterpret_cast<void *>(entry),6),"each of three checked draw hooks restored");
        bool rejected=false;
        try { nova::abi::GuestHook mismatch(image,names[type],{0x55,0x89,0xe5,0x57,0x56,0x90},reinterpret_cast<uint32_t>(&draw_hook)); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected && !std::memcmp(saved.data(),reinterpret_cast<void *>(entry),6),"each draw prologue mismatch rejected before mutation");
    }
    check(driver_touch_control(11,11,22) && driver_touch_control(22,11,22) && !driver_touch_control(33,11,22),"exact joystick/camera control suppression retains neighboring shooter");
    check(native_hud_presentation(true,false,true).hide_mobile,"seated driver retains mobile mask");
    check(native_hud_presentation(true,false,false).hide_mobile,"input-disabled exit animation retains mobile mask");
    check(!native_hud_presentation(false,false,false).hide_mobile,"menu remains outside the gameplay presentation mask");
    std::puts("driver drawing: exact controls, SP shooter/HUD retention, live scope and exception restoration passed");
}
void real(const char *path) {
    nova::ImportRegistry imports; nova::bionic::register_boot(imports); nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports); Controls controls{ControlsConfig{}};
    std::map<uint32_t,std::array<uint8_t,7>> before;
    for(auto name : {"_ZN20CPlayerJeepComponent17UpdateFromPhysicsEi","_ZN21CLevelInterfaceNormal16UpdateJeepPedalsEv",
        "_ZN14AnalogJoystick4DrawEi","_ZN20MobileAnalogJoystick4DrawEi","_ZN20AngularRotateControl4DrawEi"}) {
        auto entry=nova::abi::require_function(image,name); std::memcpy(before[entry].data(),reinterpret_cast<void *>(entry),7);
    }
    { NativeControls adapter(image,controls); }
    for(auto &[entry,bytes] : before) check(!std::memcmp(bytes.data(),reinterpret_cast<void *>(entry),bytes.size()),"all five real driver hook entries restored");
    std::puts("real NativeControls installs/restores exact camera, pedal and three driver draw hooks; no initializers or gameplay");
}
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--real") { if(!std::filesystem::exists(argv[2])) return 77; real(argv[2]); return 0; }
        check(argc==2,"synthetic fixture path required"); selection();
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        const auto camera=nova::abi::require_function(image,"driver_camera"),pedals=nova::abi::require_function(image,"driver_pedals");
        std::array<uint8_t,7> before_camera{},before_pedals{};
        std::memcpy(before_camera.data(),reinterpret_cast<void *>(camera),7); std::memcpy(before_pedals.data(),reinterpret_cast<void *>(pedals),7);
        {
            nova::abi::GuestHook camera_detour(image,"driver_camera",{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&camera_hook));
            nova::abi::GuestHook pedal_detour(image,"driver_pedals",{0x55,0x89,0xe5,0x83,0xec,0x28},reinterpret_cast<uint32_t>(&pedals_hook));
            camera_original=camera_detour.original(); pedals_original=pedal_detour.original();
            actions(image); mouse(image); drawing(image);
        }
        check(!std::memcmp(before_camera.data(),reinterpret_cast<void *>(camera),7) && !std::memcmp(before_pedals.data(),reinterpret_cast<void *>(pedals),7),"camera/pedal entry bytes restored");
        bool rejected=false;
        try { nova::abi::GuestHook bad(image,"driver_camera",{0x8d,0x4c,0x24,0x08,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&camera_hook)); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected && !std::memcmp(before_camera.data(),reinterpret_cast<void *>(camera),7),"mismatched camera prologue rejected without mutation");
        rejected=false;
        try { nova::abi::GuestHook mismatch(image,"driver_pedals",{0x55,0x89,0xe5,0x83,0xec,0x18},reinterpret_cast<uint32_t>(&pedals_hook)); }
        catch(const std::runtime_error &) { rejected=true; }
        check(rejected && !std::memcmp(before_pedals.data(),reinterpret_cast<void *>(pedals),7),"native pedal API supported but mismatched prologue rejected before mutation");
        std::puts("driver adapter contracts passed"); return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"driver adapter test failed: %s\n",error.what()); return 1; }
}
