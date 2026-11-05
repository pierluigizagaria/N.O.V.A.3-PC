#include "input/native_controls.h"
#include "input/bindings.h"
#include "bionic/runtime.h"
#include "fixtures/jeep_contract.h"
#include "fixtures/driver_contract.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
using namespace nova::input;
using nova::abi::invoke;
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
uint32_t address(auto &value) { return reinterpret_cast<uint32_t>(&value); }
uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
struct Harness;
Harness *current=nullptr;
uint32_t update_original=0,stop_original=0,animation_original=0;
uint32_t __cdecl jeep_pedal_query(uint32_t object,uint32_t channel);
struct Harness {
    static constexpr uint32_t component=0x10000,actor=0x20000,vehicle=0x30000,level=0x40000,normal=0x50000,scheme=0x60000;
    JeepContract native{};
    DriverContract camera{};
    std::map<uint32_t,uint32_t> memory;
    Controls controls{ControlsConfig{}};
    FrameInput frame;
    InputTarget target;
    JeepFireActions fire;
    DriverActions driving;
    DriverPedalRelease pedal;
    unsigned configuration=2;
    uint32_t update,event,reset,stop,animation,move,camera_entry,pedals_entry;
    bool same_level=true,getter=true,eligible=true,primary=true,secondary=true,pending=false;
    unsigned preparations=0,camera_writes=0;
    std::function<void()> after_stop;
    Harness(const nova::ElfImage &image,unsigned capabilities=3) {
        current=this; controls.gameplay=true;
        native.enabled=native.shooter=native.secondary=1; native.driver=address(camera); native.animation.enabled=1;
        for(unsigned i=0;i<2;++i) { native.weapon.allowed[i]=1; native.weapon.ammo[i]=100; }
        memory={{level+0x17c,normal},{normal+0xc,scheme},{normal+0x28,0x70000},{normal+0x2c,0x71000},
            {component+4,actor},{component+0x17c,vehicle},{component+0x108,0x72000},
            {component+0x1ac,capabilities&1},{component+0x1ad,(capabilities>>1)&1},
            {component+0x11c,0x73000},{component+0x114,0x74000},{component+0x174,0x75000},
            {component+0x100,actor+0x68},{component+0x104,actor+0x60},{vehicle+4,0x76000},
            {vehicle+0x25c,component},{vehicle+0x1e0,address(native.weapon)},{vehicle+0x16c,actor},
            {vehicle+0x228,address(native.animation)},{address(native.animation)+0x138,0x77000},
            {address(native.animation)+0x158,1},{vehicle+0x230,9},{vehicle+0x244,10}};
        target=identify();
        check(bool(target),"fixture initial production jeep layout eligible");
        update=nova::abi::require_function(image,"jeep_update"); event=nova::abi::require_function(image,"jeep_event");
        reset=nova::abi::require_function(image,"jeep_reset"); stop=nova::abi::require_function(image,"jeep_stop");
        animation=nova::abi::require_function(image,"jeep_animation"); move=nova::abi::require_function(image,"driver_event");
        camera_entry=nova::abi::require_function(image,"driver_camera");
        pedals_entry=nova::abi::require_function(image,"driver_pedals");
        camera.scale_x=2; camera.scale_y=3; camera.gain=.7f; camera.accelerated=1;
        camera.driver_distance=1; camera.shooter_distance=-1; camera.shooter=(capabilities&2)!=0;
        camera.pitch_minimum=-80; camera.pitch_maximum=80; camera.enabled=camera.ingame=1; camera.mode=2; camera.axes=3; camera.dt=16; camera.query=reinterpret_cast<uint32_t>(&jeep_pedal_query);
    }
    ~Harness() { current=nullptr; }
    uint32_t read(uint32_t p,uint32_t off) { return memory[p+off]; }
    uint32_t byte(uint32_t p,uint32_t off) { return p==component && off==0xd ? native.enabled : read(p,off); }
    InputTarget identify(bool weapons=true) {
        return identify_jeep(level,component,[&](auto p,auto off) { return read(p,off); },
            [&](auto p,auto off) { return byte(p,off); },[&](auto) { return configuration; },weapons);
    }
    bool controller() {
        return same_level && getter && jeep_controller_matches(target,actor,[&](auto p,auto off) { return read(p,off); },
            [&](auto p,auto off) { return byte(p,off); });
    }
    bool weapon() { return controller() && jeep_weapon_matches(target,[&](auto p,auto off) { return read(p,off); }); }
    bool visual() { return controller() && jeep_animation_matches(target,[&](auto p,auto off) { return read(p,off); },
        [&](auto p,auto off) { return byte(p,off); }); }
    bool lifetime(InputTarget cached) {
        return driver_lifetime_matches(cached,level,same_level ? level : level+1,getter ? component : component+1,[&] { return identify(); });
    }
    bool driver_lifetime(InputTarget cached) {
        return driver_lifetime_matches(driver_cleanup_identity(cached),level,same_level ? level : level+1,
            getter ? component : component+1,[&] {
                return driver_cleanup_identity(identify_driver(level,component,[&](auto p,auto off) { return read(p,off); },
                    [&](auto p,auto off) { return byte(p,off); },[&](auto) { return configuration; }));
            });
    }
    bool live() {
        return eligible && lifetime(target) && jeep_owner_matches(target,level,
            {identify(),level,normal,normal,scheme,false,false,false,true});
    }
    void button(Uint32 type,Uint8 button) { SDL_Event e{}; e.type=type; e.button.button=button; controls.handle(e); }
    void key(Uint32 type,SDL_Keycode key) { SDL_Event e{}; e.type=type; e.key.key=key; controls.handle(e); }
    void motion(float x,float y) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.xrel=x; e.motion.yrel=y; controls.handle(e); }
    void prepare() {
        ++preparations;
        apply_jeep_fire(frame,(target.driver.capabilities&2)!=0,primary,secondary && native.secondary,fire,
            [&] { return controls.active() && live(); },[&](unsigned type) { invoke(event,{address(native),type}); });
    }
    void take() {
        frame=controls.active() ? controls.take() : FrameInput{};
        apply_driver_actions(frame,(target.driver.capabilities&1)!=0,3,driving,[&] { return live(); },
            [&](unsigned type,float x,float y) { invoke(move,{address(camera),type,bits(x),bits(y)}); });
        if(configuration==1 && (target.driver.capabilities&1) && frame.down(Action::forward)) {
            if(!pedal.pending) pedal={target,level,true,false};
            pedal_boundary();
        }
        prepare(); pending=live() && (frame.dx!=0 || frame.dy!=0);
    }
    void pedal_boundary() {
        auto cached=pedal.owner;
        driver_pedal_boundary(pedal,[&] { return driver_lifetime(cached); },
            [&] { invoke(pedals_entry,{address(camera)}); },
            [&] { return camera.accelerator_timer==0 && camera.brake_timer==0; });
    }
    void cleanup() {
        release_jeep_fire(fire,[&] { return controller(); },[&] { invoke(reset,{address(native)}); },
            [&] { return weapon(); },[&] { return visual(); },
            [&](unsigned channel) { invoke(stop,{target.jeep.weapon,channel,1}); },
            [&](unsigned channel) { native.animation.channel=channel; invoke(animation,{target.jeep.animation_actor}); });
        release_driver_actions(driving,driver_lifetime(target),[&] { invoke(move,{address(camera),14,0,0}); });
        if(pedal.owned) {
            if(driver_lifetime(pedal.owner)) { pedal.pending=true; pedal_boundary(); } else pedal={};
        }
        controls.clear(); frame={}; pending=false;
    }
    void boundary(uint32_t original) {
        jeep_fire_boundary(true,controls.active() && live(),native.secondary!=0,fire,
            [&] { cleanup(); },[&] { prepare(); },[&] { invoke(original,{address(native),16}); });
    }
    bool camera_lifetime(InputTarget cached) {
        return driver_lifetime_matches(jeep_camera_identity(cached),level,same_level ? level : level+1,
            getter ? component : component+1,[&] { return identify(false); });
    }
    void look(std::function<void()> mutation={}) {
        const auto cached=target;
        const bool allowed=pending && controls.active() && live();
        const bool cleared=camera.one_shot!=0;
        auto sample=allowed ? driver_mouse_input(frame.dx,frame.dy,camera.scale_x,camera.scale_y,
            camera.accelerated!=0,camera.gain,controls.config) : std::optional<Look>{};
        driver_mouse_boundary(pending,allowed,sample,[&] { return !cleared && camera_lifetime(cached) && !camera.one_shot; },
            [&] { return Look{camera.raw_x,camera.raw_y}; },[&](Look raw) { ++camera_writes; camera.raw_x=raw.yaw; camera.raw_y=raw.pitch; },
            [&] { invoke(camera_entry,{address(camera),16}); if(mutation) mutation(); });
    }
};
uint32_t __cdecl jeep_pedal_query(uint32_t object,uint32_t channel) {
    auto &h=*current; check(object==address(h.camera) && channel<2,"exact native jeep pedal query");
    auto owner=h.pedal.pending ? h.pedal.owner : h.target;
    auto button=channel ? owner.driver.brake : owner.driver.accelerator;
    auto result=driver_pedal_input(owner,button,h.controls.active(),h.pedal.pending,true,h.frame,[&] {
        return h.pedal.pending ? h.driver_lifetime(owner) : h.live();
    });
    return result ? *result : channel ? h.camera.brake_pedal : h.camera.accelerator;
}
void __cdecl update_hook(uint32_t object,uint32_t) { check(current && object==address(current->native),"exact selected native Update receiver"); current->boundary(update_original); }
void __cdecl native_stop_mutation() { if(current->after_stop) current->after_stop(); }
void __cdecl stop_hook(uint32_t weapon,uint32_t channel,uint32_t messages) {
    auto &h=*current; const auto cached=h.target;
    observe_jeep_release(h.fire,channel,false,[&] {
        invoke(stop_original,{weapon,channel,messages});
    },[&] { return h.target==cached && cached.jeep.weapon==weapon && h.weapon(); });
}
void animation_hook(uint32_t object,unsigned channel) {
    auto &h=*current; const auto cached=h.target;
    observe_jeep_release(h.fire,channel,true,[&] { invoke(animation_original,{object}); },
        [&] { return h.target==cached && cached.jeep.animation_actor==object && h.visual(); });
}
void __cdecl primary_animation_hook(uint32_t object) { animation_hook(object,0); }
void __cdecl secondary_animation_hook(uint32_t object) { animation_hook(object,1); }
void selection(const nova::ElfImage &image) {
    for(unsigned roles=0;roles<4;++roles) {
        Harness h(image,roles ? roles : 1); h.memory[Harness::component+0x1ac]=roles&1; h.memory[Harness::component+0x1ad]=(roles>>1)&1;
        auto found=h.identify(); check(bool(found)==bool(roles),"neither/driver-only/shooter-only/combined capability selection");
        if(found) {
            check(found.driver.capabilities==roles,"native flags preserved, no exclusive shooter role");
            auto next=select_input_target(true,{},{},{},{Harness::component,true,true,true,found.driver,found.jeep});
            check(next==found,"one jeep owner preserves camera and weapon identity through production router");
            for(unsigned conflict=0;conflict<3;++conflict) {
                TargetCandidate normal{},mech{},gun{};
                (conflict==0 ? normal : conflict==1 ? mech : gun)={22,true,true,true};
                check(!select_input_target(true,normal,mech,gun,{Harness::component,true,true,true,found.driver,found.jeep}),"normal/mech/stationary or mobile-gun conflict rejected");
            }
        }
    }
    Harness h(image,2);
    for(auto cell : {Harness::vehicle+0x25c,Harness::vehicle+0x16c,Harness::component+0x11c,Harness::component+0x114,
        Harness::component+0x174,Harness::component+0x100,Harness::component+0x104,Harness::vehicle+0x1e0}) {
        auto saved=h.memory[cell]; h.memory[cell]=0; check(!h.identify(),"shooter-only backlink/local actor/camera/controller/weapon requirement"); h.memory[cell]=saved;
    }
    for(auto cell : {Harness::component+0x1ac,Harness::component+0x1ad,Harness::component+0x11c,Harness::component+0x114,
        Harness::component+0x174,Harness::vehicle+0x1e0,Harness::vehicle+0x16c,Harness::vehicle+0x228,
        address(h.native.animation)+0x138,address(h.native.animation)+0x158,Harness::vehicle+0x230,Harness::vehicle+0x244}) {
        auto saved=h.memory[cell]; h.memory[cell]^=1;
        check(!h.lifetime(h.target),"every same-pointer capability/camera/weapon/animation transition rejects old identity"); h.memory[cell]=saved;
    }
    h.memory[Harness::vehicle+0x1e0]^=4; check(!h.weapon() && h.visual(),"weapon replacement retains independent valid animation receiver");
    h.memory[Harness::vehicle+0x1e0]^=4; h.memory[Harness::vehicle+0x244]^=1; check(h.weapon() && !h.visual(),"animation replacement retains independent valid weapon receiver");
    std::puts("jeep selection: all capabilities, independent shooter backlink/local actor, camera/resource transitions and owner conflicts passed");
}
// J01a: the host helper is production code. This model independently follows
// the accepted normal Update activation, Pressed order and client request path;
// it deliberately provides no ACK, server rebind or alternate SP transition.
struct RoleHarness : Harness {
    static constexpr uint32_t driver_button=0x81000, shooter_button=0x82000, other_vehicle=0x83000, normal_update=0x100000;
    struct Request { uint32_t actor,index,role; };
    JeepRoleEdge edge;
    std::vector<Request> requests;
    std::map<uint32_t,bool> enabled_buttons, original_edges;
    unsigned forwarded=0;
    RoleHarness(const nova::ElfImage &image,unsigned capabilities=2) : Harness(image,capabilities) {
        memory[normal+0x24]=driver_button; memory[normal+0x20]=shooter_button;
        memory[vehicle+0x168]=(capabilities&1) ? actor : 0;
        memory[vehicle+0x16c]=(capabilities&2) ? actor : 0;
        target=identify();
    }
    void sample() {
        if(!controls.active() || !live()) { edge={}; controls.clear(); return; }
        frame=controls.take();
        offer_jeep_role_edge(edge,frame,target,level,[&](auto p,auto off) { return read(p,off); });
    }
    bool pressed(uint32_t button,uint32_t returned=0) {
        if(!returned) returned=normal_update+(button==driver_button ? 0x74du : 0x64du);
        if(jeep_role_query_matches(normal_update,returned,edge.role) && consume_jeep_role_edge(edge,button,[&](InputTarget owner,uint32_t prior_level,uint64_t epoch) {
            return target==owner && prior_level==level && live() && controls.active() && controls.edge_epoch()==epoch;
        },[&](auto p,auto off) { return read(p,off); },[&](auto p) { return enabled_buttons[p]; })) return true;
        ++forwarded; return original_edges[button];
    }
    bool available(uint32_t car,unsigned role) {
        return role==0 ? read(car,0x16c)==actor && !read(car,0x168) :
                         read(car,0x168)==actor && !read(car,0x16c);
    }
    void update_roles(bool native_permission=true) {
        const std::array<uint32_t,2> vehicles{other_vehicle,vehicle};
        const auto first=read(normal,0x24), second=read(normal,0x20);
        enabled_buttons[first]=enabled_buttons[second]=false;
        for(auto car : vehicles) for(unsigned role=0;role<2;++role)
            if(native_permission && available(car,role)) enabled_buttons[role ? second : first]=true;
        jeep_role_input_boundary(edge,[&] {
            // Original +0x20 is queried before +0x24; numeric order is independent.
            for(unsigned role : {1u,0u}) if(pressed(role ? second : first,normal_update+(role ? 0x64du : 0x74du))) {
                for(uint32_t index=0;index<vehicles.size();++index) if(available(vehicles[index],role)) {
                    requests.push_back({actor,index,role}); break;
                }
            }
        });
    }
};
void role_requests(const nova::ElfImage &image) {
    for(unsigned role=0;role<2;++role) for(unsigned configuration=0;configuration<4;++configuration) {
        RoleHarness h(image,role ? 1u : 2u); h.configuration=configuration; h.target=h.identify();
        const auto owner=h.target; const auto key=role ? SDLK_2 : SDLK_1;
        h.key(SDL_EVENT_KEY_DOWN,key); h.sample(); h.update_roles();
        check(h.requests.size()==1 && h.requests[0].actor==Harness::actor && h.requests[0].index==1 &&
            h.requests[0].role==role,"default 1/2 reaches original native role0/1 request and native list index in every driving configuration");
        check(h.target==owner && h.identify()==owner,"client request does not fake acceptance, rewrite occupancy or capabilities");
        for(unsigned repeat=0;repeat<5;++repeat) { h.update_roles(); h.sample(); }
        check(h.requests.size()==1,"held key and repeated native queries never retry an asynchronous request");
        h.key(SDL_EVENT_KEY_UP,key); h.key(SDL_EVENT_KEY_DOWN,key); h.sample(); h.update_roles();
        check(h.requests.size()==2,"fresh edge may issue a new native request without a fabricated ACK");
    }
    for(unsigned role=0;role<2;++role) {
        RoleHarness h(image,role ? 1u : 2u); const auto key=role ? SDLK_2 : SDLK_1;
        h.key(SDL_EVENT_KEY_DOWN,key); h.sample();
        h.memory[Harness::vehicle+(role ? 0x16c : 0x168)]=0x99999;
        h.update_roles();
        check(h.requests.empty() && h.edge.role==2,"occupied counterpart refuses and discards the edge");
        h.memory[Harness::vehicle+(role ? 0x16c : 0x168)]=0; h.update_roles();
        check(h.requests.empty(),"an unavailable request never waits for the other occupant to leave");
    }
    for(unsigned role=0;role<2;++role) {
        RoleHarness h(image,role ? 1u : 2u); const auto key=role ? SDLK_2 : SDLK_1;
        h.key(SDL_EVENT_KEY_DOWN,key); h.key(SDL_EVENT_KEY_UP,key); h.sample(); h.update_roles();
        check(h.requests.size()==1 && h.requests[0].role==role,"short numeric tap survives until its single native query");
    }
    for(unsigned role=0;role<2;++role) {
        RoleHarness h(image,role ? 1u : 2u); const auto key=role ? SDLK_2 : SDLK_1;
        h.key(SDL_EVENT_KEY_DOWN,key); h.sample();
        const auto button=role ? RoleHarness::shooter_button : RoleHarness::driver_button;
        h.enabled_buttons[button]=true; // Native activation preceded this occupant change.
        h.memory[Harness::vehicle+(role ? 0x16c : 0x168)]=0x99999;
        check(!h.pressed(button) && h.edge.role==2,"fresh occupancy refuses even a stale enabled native button");
    }
    for(auto action : {Action::slot1,Action::slot2}) {
        RoleHarness h(image,3); h.key(SDL_EVENT_KEY_DOWN,action==Action::slot1 ? SDLK_1 : SDLK_2); h.sample(); h.update_roles();
        check(h.requests.empty() && h.target.driver.capabilities==3,"ordinary combined SP has no eligible native transition");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.key(SDL_EVENT_KEY_DOWN,SDLK_2); h.sample(); h.update_roles();
        check(h.requests.empty(),"simultaneous numeric role edges request neither role");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample(); h.update_roles(false); h.update_roles();
        check(h.requests.empty(),"native button disabled at consumption is final refusal, not a deferred edge");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample(); h.enabled_buttons[RoleHarness::driver_button]=true;
        check(!h.pressed(0x99999) && h.edge.role==0,"unrelated button query forwards without consuming the role edge");
        check(!h.pressed(RoleHarness::driver_button,RoleHarness::normal_update+0x64d) &&
              !h.pressed(RoleHarness::driver_button,RoleHarness::normal_update+0x74c) && h.edge.role==0,
              "wrong role or incidental same-button call cannot consume the original producer edge");
        check(h.pressed(RoleHarness::driver_button) && !h.pressed(RoleHarness::driver_button),"exact role button consumes a single edge");
        h.original_edges[0x99999]=true; check(h.pressed(0x99999),"original unrelated native button behavior remains forwarded");
    }
    for(auto offset : {0x20u,0x24u}) {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        const auto old=h.memory[Harness::normal+offset]; h.memory[Harness::normal+offset]^=4;
        h.update_roles(); h.memory[Harness::normal+offset]=old; h.update_roles();
        check(h.requests.empty(),"replaced role button invalidates the sampled binding permanently");
    }
    // Each change keeps the component address. Production lifetime/identity must
    // reject it before retained role pointers are read, even if it later returns.
    for(auto cell : {Harness::component+4,Harness::component+0x17c,Harness::component+0x108,
                    Harness::component+0x1ac,Harness::component+0x1ad,Harness::component+0x11c,
                    Harness::component+0x114,Harness::component+0x174,Harness::component+0x100,
                    Harness::component+0x104,Harness::vehicle+0x25c,Harness::vehicle+4,
                    Harness::vehicle+0x16c,Harness::vehicle+0x1e0,Harness::vehicle+0x228,
                    Harness::level+0x17c,Harness::normal+0xc}) {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        const auto old=h.memory[cell]; h.memory[cell]^=1; h.update_roles(); h.memory[cell]=old; h.update_roles();
        check(h.requests.empty(),"same-address owner/capability/controller/camera/weapon/interface replacement discards role request");
    }
    for(unsigned mutation=0;mutation<5;++mutation) {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        if(mutation==0) h.same_level=false;
        if(mutation==1) h.getter=false;
        if(mutation==2) h.native.enabled=0;
        if(mutation==3) h.eligible=false;
        if(mutation==4) h.configuration=1;
        h.update_roles();
        check(h.requests.empty(),"fresh level/getter/enable/pause/configuration gate rejects role request");
    }
    for(unsigned interruption=0;interruption<5;++interruption) {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        if(interruption==0) { h.controls.context(false,false); h.controls.context(true,false); }
        if(interruption==1) { h.controls.overlay(true); h.controls.overlay(false); }
        if(interruption==2) { h.controls.context(true,true); h.controls.context(true,false); }
        if(interruption==3) { h.controls.gameplay=false; h.controls.clear(); h.controls.gameplay=true; }
        if(interruption==4) h.controls.replace_bindings(h.controls.snapshot_config());
        h.update_roles(); check(h.requests.empty(),"focus/panel/text/pause/remap revocation survives reopening before Pressed");
        h.key(SDL_EVENT_KEY_UP,SDLK_1); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample(); h.update_roles();
        check(h.requests.size()==1,"only fresh post-interruption edge requests a role");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        auto config=h.controls.snapshot_config();
        check(rebind(config,Action::slot1,{BindingKind::key,SDLK_B}).empty(),"role alias uses existing binding conflict contract");
        h.controls.replace_bindings(config); h.update_roles();
        h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample(); h.update_roles();
        check(h.requests.empty(),"old held binding is quarantined and cannot supply a stale role edge");
        h.key(SDL_EVENT_KEY_UP,SDLK_1); h.key(SDL_EVENT_KEY_DOWN,SDLK_B); h.sample(); h.update_roles();
        check(h.requests.size()==1 && h.requests[0].role==0,"remapped slot1 reaches driver; no raw numeric key route");
        check(std::string(action_contexts(Action::slot1)).find("driver")!=std::string::npos &&
              std::string(action_contexts(Action::slot2)).find("shooter")!=std::string::npos,"binding contexts disclose the eligible jeep roles");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1);
        h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); h.sample();
        h.controls.clear_gamepad(); h.update_roles();
        check(h.requests.size()==1 && h.controls.take().down(Action::aim),
            "controller-only revocation preserves sampled keyboard role edge and physical mouse desire");
    }
    {
        RoleHarness h(image); InputTarget selected{TargetKind::normal,0x90000};
        h.key(SDL_EVENT_KEY_DOWN,SDLK_1);
        change_input_target(selected,h.target,[](auto) {},[&] { h.edge={}; h.controls.clear(); });
        h.sample(); h.update_roles(); check(h.requests.empty(),"queued on-foot inventory edge cannot enter jeep");
        h.key(SDL_EVENT_KEY_UP,SDLK_1); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        change_input_target(selected,{TargetKind::normal,0x90000},[](auto) {},[&] { h.edge={}; h.controls.clear(); });
        check(h.edge.role==2 && !h.controls.take().edge(Action::slot1),"queued jeep request cannot become an on-foot inventory edge");
        h.key(SDL_EVENT_KEY_UP,SDLK_1); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); const auto foot=h.controls.take();
        offer_jeep_role_edge(h.edge,foot,selected,Harness::level,[&](auto p,auto off) { return h.read(p,off); });
        check(foot.edge(Action::slot1) && h.edge.role==2,"fresh on-foot slot action remains available to original inventory path only");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        jeep_role_input_boundary(h.edge,[] {}); h.update_roles();
        check(h.requests.empty(),"original Update that does not query the role discards its edge");
        h.key(SDL_EVENT_KEY_UP,SDLK_1); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        try { jeep_role_input_boundary(h.edge,[] { throw std::runtime_error("fixture native failure"); }); } catch(const std::runtime_error &) {}
        check(h.edge.role==2,"exceptional native return also discards role edge");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        unsigned calls=0; bool nested=true;
        const auto result=consume_jeep_role_edge(h.edge,RoleHarness::driver_button,
            [&](InputTarget,uint32_t,uint64_t) {
                if(++calls==1) nested=consume_jeep_role_edge(h.edge,RoleHarness::driver_button,
                    [](InputTarget,uint32_t,uint64_t) { return true; },
                    [&](auto p,auto off) { return h.read(p,off); },[](auto) { return true; });
                return h.live();
            },[&](auto p,auto off) { return h.read(p,off); },[](auto) { return true; });
        check(result && !nested && calls==2,"edge is consumed before reentrant native ownership queries");
    }
    {
        RoleHarness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        unsigned reads=0;
        check(!consume_jeep_role_edge(h.edge,RoleHarness::driver_button,
            [](InputTarget,uint32_t,uint64_t) { return false; },
            [&](auto p,auto off) { ++reads; return h.read(p,off); },[](auto) { return true; }) && reads==0,
            "lost owner is rejected before any retained button/vehicle pointer read");
        h.key(SDL_EVENT_KEY_UP,SDLK_1); h.key(SDL_EVENT_KEY_DOWN,SDLK_1); h.sample();
        check(!consume_jeep_role_edge(h.edge,RoleHarness::driver_button,
            [&](InputTarget,uint32_t,uint64_t) { return h.live(); },
            [&](auto p,auto off) { return h.read(p,off); },[&](auto) { h.eligible=false; return true; }) && h.edge.role==2,
            "ownership is checked again after native enable query without retaining a refused edge");
    }
    for(bool scope : {false,true}) for(bool fail : {false,true}) {
        std::map<uint32_t,uint32_t> buttons{{0x10,10},{0x20,20},{0x24,24},{0x28,28},{0x2c,44}};
        std::map<uint32_t,uint8_t> visible{{10,1},{20,1},{24,0},{28,1},{44,1},{60,1},{99,1}};
        const auto before=visible; unsigned draws=0;
        const std::map<uint32_t,bool> enabled{{20,true},{24,false}};
        try {
            draw_jeep_controls(scope,[&](auto slot) { return buttons[slot]; },[&](auto p) { return visible[p]; },
                [&](auto p,bool show) { visible[p]=show; },[&] {
                    ++draws;
                    check(visible[20]==uint8_t(!scope) && !visible[24] && visible[60] && visible[99],
                        "only scoped native role artwork is hidden; entry indication and informative neighbor remain");
                    check(enabled.at(20) && !enabled.at(24),"drawing never changes native role eligibility");
                    if(fail) throw std::runtime_error("role draw interruption");
                });
        } catch(const std::runtime_error &error) {
            check(fail && std::string(error.what())=="role draw interruption","unexpected role draw failure");
        }
        check(draws==1 && visible==before,"role draw flags and original side effects restore on normal and exceptional return");
    }
    std::puts("jeep roles: remapped native role0/1 requests, eligibility, one-shot lifetime and combined-SP refusal passed");
}
void actions(const nova::ElfImage &image) {
    for(unsigned roles : {1u,2u,3u}) {
        Harness h(image,roles); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT);
        h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.take(); invoke(h.update,{address(h.native),16});
        check(h.native.events[29]==unsigned(roles!=1) && h.native.events[31]==unsigned(roles!=1),"driver-only no weapon action, shooter-only and combined each once");
        check(h.camera.events==unsigned(roles&1),"shooter-only never injects driver movement");
        h.cleanup(); auto stops=h.native.weapon.stops[0]+h.native.weapon.stops[1]; h.cleanup();
        check(h.native.weapon.stops[0]+h.native.weapon.stops[1]==stops,"repeat cleanup cannot duplicate native stop/messages");
    }
    Harness click(image); click.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); click.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT);
    click.take(); invoke(click.update,{address(click.native),16}); check(click.native.weapon.shots[0]==1,"short click preserved by requested edge");
    click.take(); invoke(click.update,{address(click.native),16});
    check(!click.fire.weapon_owned[0] && !click.fire.animation_owned[0] && click.native.weapon.messages[0]==1,"both actual primary calls complete, once");
    click.cleanup(); check(click.native.weapon.messages[0]==1,"no repeat MP stop after native acknowledgement");
    for(unsigned channel=0;channel<2;++channel) {
        Harness h(image); const auto button=channel ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        h.native.weapon.allowed[channel]=0; h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,static_cast<Uint8>(button)); h.take(); invoke(h.update,{address(h.native),16});
        check(!h.native.weapon.shots[channel],"native permission denies FireWeapon");
        h.native.weapon.allowed[channel]=1; h.native.weapon.cooldown[channel]=1; invoke(h.update,{address(h.native),16});
        check(!h.native.weapon.shots[channel],"native cooldown denies held retry");
        h.native.weapon.cooldown[channel]=0; invoke(h.update,{address(h.native),16}); check(h.native.weapon.shots[channel]==1,"held native retry fires when eligibility returns");
        auto ammo=h.native.weapon.ammo[channel]; invoke(h.update,{address(h.native),16}); check(h.native.weapon.ammo[channel]==ammo,"native IsFiring prevents duplicate held FireWeapon");
        h.button(SDL_EVENT_MOUSE_BUTTON_UP,static_cast<Uint8>(button)); h.take();
        h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,static_cast<Uint8>(button)); h.take(); invoke(h.update,{address(h.native),16});
        check(!h.native.weapon.stops[channel] && !h.native.released[channel],"native press clears preceding release; repress never retains an old edge");
        h.cleanup(); check(h.native.weapon.messages[channel]==1,"cleanup after release/repress stops owned channel exactly once");
    }
    for(unsigned halt=0;halt<2;++halt) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); h.take(); invoke(h.update,{address(h.native),16});
        h.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT); h.take();
        h.native.halt=halt+1; invoke(h.update,{address(h.native),16});
        check(!h.fire.weapon_owned[halt] && h.fire.animation_owned[halt],"interrupted native release acknowledges only completed weapon operation");
        h.cleanup(); check(h.native.weapon.messages[0]==1 && h.native.weapon.messages[1]==1 && h.native.animation.stops[0]==1 && h.native.animation.stops[1]==1,"partial/both-channel cleanup independently completes remaining operations without repetition");
    }
    for(auto cell : {Harness::vehicle+0x1e0,Harness::vehicle+0x244,Harness::component+0x11c}) {
        Harness h(image); h.key(SDL_EVENT_KEY_DOWN,SDLK_W);
        h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT);
        h.motion(8,9); h.take(); h.memory[cell]^=4; invoke(h.update,{address(h.native),16});
        check(h.native.updates==1 && h.native.weapon.shots[0]==0 && h.native.weapon.shots[1]==0 &&
            !h.native.held[0] && !h.native.held[1] && !h.pending && h.camera.releases==1,
            "actual checked Update entry revalidates same-address resources before native consumption, releases drive and clears prepared fire/mouse");
    }
    Harness button_gate(image); button_gate.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT);
    button_gate.take(); button_gate.primary=false; invoke(button_gate.update,{address(button_gate.native),16});
    check(!button_gate.native.weapon.shots[0] && button_gate.native.weapon.messages[0]==1,
        "button permission loss between selection and native Update consumes a release, never the old press");
    Harness ammunition(image); ammunition.native.weapon.ammo[1]=0; ammunition.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT);
    ammunition.take(); invoke(ammunition.update,{address(ammunition.native),16}); check(!ammunition.native.weapon.shots[1],"native empty ammo blocks a pressed secondary");
    ammunition.native.weapon.ammo[1]=1; invoke(ammunition.update,{address(ammunition.native),16});
    check(ammunition.native.weapon.shots[1]==1 && ammunition.native.weapon.ammo[1]==0,"native held retry owns ammo consumption");
    Harness reset(image); reset.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); reset.take(); invoke(reset.reset,{address(reset.native)});
    check(reset.fire.weapon_owned[0] && reset.fire.animation_owned[0],"cleared/reset bytes are not release acknowledgements"); reset.cleanup();
    check(reset.native.weapon.messages[0]==1,"cleanup still stops after external native reset");
    for(unsigned change=0;change<9;++change) {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); h.take();
        if(change==0) h.memory[Harness::vehicle+0x1e0]^=4;
        if(change==1) h.memory[Harness::vehicle+0x244]^=1;
        if(change==2) h.same_level=false;
        if(change==3) h.getter=false;
        if(change==4) h.native.enabled=0;
        if(change==5) h.memory[Harness::component+0x100]^=4;
        if(change==6) h.memory[Harness::vehicle+0x228]^=4;
        if(change==7) h.memory[Harness::vehicle+0x25c]^=4;
        if(change==8) h.memory[Harness::component+0x1ad]=0;
        h.cleanup();
        const bool control=change==0 || change==1 || change==6 || change==8;
        check(h.native.resets==unsigned(control),"only current enabled controller reset across ownership/resource changes");
        check(h.native.weapon.messages[0]==unsigned(control && change!=0) && h.native.weapon.messages[1]==unsigned(control && change!=0),"each still-current weapon cleaned independently, no stale pointer call");
        check(h.native.animation.stops[0]==unsigned(control && change!=1 && change!=6),"each still-current animation cleaned independently, no replacement receiver call");
    }
    Harness pause(image); pause.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); pause.key(SDL_EVENT_KEY_DOWN,SDLK_W); pause.motion(5,7); pause.take();
    pause.controls.context(false,false); invoke(pause.update,{address(pause.native),16});
    check(pause.native.weapon.messages[1]==1 && !pause.pending && !pause.driving.moving,"actual update boundary focus loss cleans held drive/fire and pending mouse");
    pause.controls.context(true,false); pause.take(); invoke(pause.update,{address(pause.native),16}); check(!pause.native.held[1],"focus resume requires fresh input");
    {
        Harness h(image); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.take();
        h.controls.overlay(true); invoke(h.update,{address(h.native),16});
        check(h.native.weapon.messages[1]==1 && !h.driving.moving && !h.pending,"overlay actual jeep update releases held drive/fire and pending look");
        h.controls.overlay(false); h.take(); invoke(h.update,{address(h.native),16});
        check(!h.native.held[1],"overlay close cannot replay jeep fire");
    }
    Harness permission(image); permission.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); permission.take();
    permission.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT); permission.take(); permission.native.secondary=0;
    // Direct original models the guarded native consumer, proving return/bytes
    // cannot acknowledge the unavailable secondary branch.
    invoke(update_original,{address(permission.native),16}); check(permission.fire.weapon_owned[1] && permission.native.released[1],"HasSecondaryFire loss skips release and preserves obligation");
    invoke(permission.update,{address(permission.native),16}); check(permission.native.weapon.messages[1]==1 && !permission.fire.weapon_owned[1],"consumption guard cleans valid owned secondary even when native branch is unavailable");
    for(unsigned resource=0;resource<4;++resource) for(bool paused : {false,true}) {
        Harness h(image); h.configuration=1; h.camera.mode=1; h.target=h.identify();
        h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT);
        h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.take();
        check(h.camera.accelerator_timer==16 && h.pedal.owned && h.driving.moving,"combined driver pedal and fire ownership established");
        if(paused) {
            invoke(h.update,{address(h.native),16});
            h.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT); h.button(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT); h.take();
            h.native.halt=1; invoke(h.update,{address(h.native),16});
            check(!h.fire.weapon_owned[0] && h.fire.animation_owned[0] && h.fire.weapon_owned[1] && h.pedal.owned,
                "mixed real pedal and partial primary/secondary gun release obligations established before pause/resource loss");
        }
        if(resource==0) h.memory[Harness::vehicle+0x1e0]^=4;
        if(resource==1) h.memory[Harness::vehicle+0x244]^=1;
        if(resource==2) h.memory[Harness::component+0x11c]^=4;
        if(resource==3) h.memory[Harness::vehicle+0x228]^=4;
        const auto old_timer=h.camera.accelerator_timer; h.camera.ingame=!paused; h.cleanup();
        check(h.camera.releases==1 && !h.driving.moving,"weapon/animation/camera replacement retains accepted driver move release");
        if(paused) {
            check(h.pedal.pending && h.camera.accelerator_timer==old_timer,"separate driver lifetime retains paused pedal obligation after shooter resource loss");
            h.camera.ingame=1; h.pedal_boundary();
        }
        check(!h.pedal.owned && h.camera.accelerator_timer==0 && h.camera.brake_timer==0,"actual native pedal consumer completes independently of shooter resource identity");
        auto calls=h.camera.pedal_calls; h.cleanup(); check(h.camera.pedal_calls==calls,"driver completion does not repeat on second cleanup");
    }
    for(unsigned remaining : {1u,2u,0u}) {
        Harness h(image); h.configuration=1; h.camera.mode=1; h.target=h.identify();
        h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT);
        h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); h.take();
        h.memory[Harness::component+0x1ac]=remaining&1; h.memory[Harness::component+0x1ad]=(remaining>>1)&1;
        if(!remaining) h.native.enabled=0; // The established native all-disable path owns teardown.
        const auto pedal_calls=h.camera.pedal_calls; h.cleanup();
        if(remaining==1) check(h.camera.releases==1 && !h.pedal.owned && !h.camera.accelerator_timer,
            "combined-to-driver-only retains valid movement/pedal release despite shooter capability loss");
        else check(h.camera.pedal_calls==pedal_calls && !h.pedal.owned && !h.camera.releases,
            "driver capability loss/disable defers drive teardown to native owner, no forced pedal consumer");
        check(h.native.weapon.messages[0]==unsigned(remaining!=0) && h.native.weapon.messages[1]==unsigned(remaining!=0),
            "still-current gun cleanup is independent of remaining capability, all-disable performs no late callbacks");
        auto messages=h.native.weapon.messages[0]+h.native.weapon.messages[1]; h.cleanup();
        check(h.native.weapon.messages[0]+h.native.weapon.messages[1]==messages,"capability transition cleanup is idempotent");
        if(remaining) {
            h.target=h.identify(); h.take(); check(!h.frame.down(Action::forward) && !h.frame.requested(Action::fire) && !h.frame.requested(Action::aim),
                "same-pointer new capability receives no stale held input");
        }
        // Native Enable(true) explicitly invokes ResetControls at 0x22aa5b.
        // Partial flag flips are defensive cases; their earlier host cleanup
        // already reset its owned fire/controller input.
        h.memory[Harness::component+0x1ac]=h.memory[Harness::component+0x1ad]=1;
        h.native.enabled=h.native.shooter=1; h.target=h.identify();
        if(!remaining) invoke(h.reset,{address(h.native)});
        const auto presses=h.native.events[29]+h.native.events[31];
        h.take(); h.pedal_boundary(); invoke(h.update,{address(h.native),16});
        check(h.native.events[29]+h.native.events[31]==presses && !h.native.held[0] && !h.native.held[1] &&
            !h.camera.accelerator_timer && !h.camera.brake_timer && !h.camera.native_x && !h.camera.native_y && !h.pending,
            "return to combined/re-enable has no replay of old drive/pedal/fire/mouse through actual native consumers");
        h.key(SDL_EVENT_KEY_DOWN,SDLK_W); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT);
        h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); h.motion(5,6); h.take();
        check(h.native.events[29]+h.native.events[31]==presses+2 && h.driving.moving && h.camera.accelerator_timer==16 && h.pending,
            "returned combined capability accepts exactly fresh drive/pedal/fire/mouse presses");
        invoke(h.update,{address(h.native),16}); h.look();
        check(h.native.weapon.shots[0]==1 && h.camera.calls==1 && !h.pending,
            "returned capability consumes the fresh fire and one coherent mouse pair through native consumers");
        h.cleanup();
    }
    Harness midway(image); midway.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); midway.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT); midway.take();
    midway.native.weapon.on_stop=reinterpret_cast<uint32_t>(&native_stop_mutation);
    midway.after_stop=[&] { midway.memory[Harness::vehicle+0x25c]^=4; };
    midway.cleanup(); check(midway.native.weapon.messages[0]==1 && midway.native.weapon.messages[1]==0 && midway.native.animation.stops[0]==0,"live controller rechecked after every guest call; no callbacks after native handoff");
    std::puts("jeep actions: native press/hold/cooldown/ammo/release, four-call partial ACK, mixed resources, reset/non-ACK, pause/permission and mid-call handoff passed");
}
void look_and_draw(const nova::ElfImage &image) {
    for(unsigned roles : {1u,2u,3u}) for(auto delta : {Look{8,0},Look{0,9},Look{8,9},Look{0,0}}) {
        Harness h(image,roles); h.button(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT); h.motion(delta.yaw,delta.pitch); h.take();
        const auto writes=h.camera_writes; h.look(); const auto yaw=h.camera.yaw,pitch=h.camera.pitch;
        h.look(); check(std::abs(h.camera.yaw-yaw)<=.0161f && h.camera.pitch==pitch,"repeated camera callback retains native recentering without a second host sample");
        check(h.camera_writes-writes==unsigned(delta.yaw!=0 || delta.pitch!=0)*2,"one coherent temporary pair with conditional restoration");
        check(h.camera.raw_x==0 && h.camera.raw_y==0,"original raw input restored for all supported capabilities");
    }
    for(unsigned mutation=0;mutation<5;++mutation) {
        Harness h(image,2); h.camera.raw_x=2; h.camera.raw_y=3; h.motion(8,9); h.take();
        h.look([&] {
            if(mutation==0) h.memory[Harness::component+0x11c]^=4;
            if(mutation==1) h.memory[Harness::component+0x1ac]=1;
            if(mutation==2) h.memory[Harness::vehicle+0x1e0]^=4;
            if(mutation==3) h.same_level=false;
            if(mutation==4) h.controls.context(false,false);
        });
        check(h.camera_writes==((mutation==2 || mutation==4) ? 2u : 1u),"camera restoration uses captured structural lifetime, independent focus eligibility");
    }
    for(unsigned resource=0;resource<5;++resource) for(unsigned native_write=0;native_write<3;++native_write) {
        Harness h(image,2); h.camera.raw_x=2; h.camera.raw_y=3;
        h.camera.one_shot=native_write==1; h.camera.mutate=native_write==2;
        h.motion(8,9); h.take();
        h.look([&] {
            if(resource==0) h.memory[Harness::vehicle+0x1e0]=0;
            if(resource==1) h.memory[Harness::vehicle+0x228]=0;
            if(resource==2) h.memory[Harness::vehicle+0x244]^=1;
            if(resource==3) h.memory[address(h.native.animation)+0x138]^=1;
            if(resource==4) h.memory[address(h.native.animation)+0x158]^=1;
        });
        const float expected_x=native_write==0 ? 2.f : native_write==1 ? 0.f : 99.f;
        const float expected_y=native_write==0 ? 3.f : native_write==1 ? 0.f : -27.f;
        check(h.camera.raw_x==expected_x && h.camera.raw_y==expected_y && h.camera_writes==(native_write==1 ? 1u : 2u),
            "weapon/animation-only replacement retains camera input owner, nonzero prior raw restoration and D4/new native writes");
    }
    Harness clear(image,2); clear.camera.raw_x=2; clear.camera.raw_y=3; clear.camera.one_shot=1; clear.motion(8,0); clear.take(); clear.look();
    check(clear.camera.raw_x==0 && clear.camera.raw_y==0,"native D4 whole pair clear retains zero supplied axis in shooter-only context");
    for(bool shooter : {false,true}) {
        std::map<uint32_t,uint8_t> visible{{11,1},{12,0},{13,1},{14,1},{99,1}};
        std::map<uint32_t,uint32_t> controls{{0x28,11},{0x2c,12},{0x30,13},{0x34,14}};
        unsigned effects=0;
        draw_jeep_scheme(true,shooter,[&](auto slot) { return controls[slot]; },[&](auto button) { return visible[button]; },
            [&](auto button,bool draw) { visible[button]=draw; },[&] {
                ++effects; check(!visible[11] && !visible[12] && !visible[13]==shooter && !visible[14]==shooter && visible[99],"only supported move/rotate/fire touch artwork hidden, informative neighbor retained");
            });
        check(effects==1 && visible[11] && !visible[12] && visible[13] && visible[14],"native Draw side effects forwarded, all independent flags restored");
        auto target=Harness(image,shooter ? 3 : 1).target;
        check(jeep_touch_control(target,13,11,12,13,14)==shooter,"direct joystick-draw identity extends only supported shooter controls");
    }
    std::puts("jeep camera/draw: one raw pair across capabilities, native clears/lifetime changes, focus restore and narrow artwork with side effects passed");
}
void real(const char *path) {
    nova::ImportRegistry imports; nova::bionic::register_boot(imports); nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports); Controls controls{ControlsConfig{}};
    std::map<uint32_t,std::vector<uint8_t>> before;
    const auto update=nova::abi::require_function(image,"_ZN20CPlayerJeepComponent6UpdateEi");
    before[update]=std::vector<uint8_t>(reinterpret_cast<uint8_t *>(update),reinterpret_cast<uint8_t *>(update)+7);
    for(uint32_t off : {0x1d13u,0xfe5u,0x1d21u,0xff3u}) before[update+off]=std::vector<uint8_t>(reinterpret_cast<uint8_t *>(update+off),reinterpret_cast<uint8_t *>(update+off)+5);
    { NativeControls adapter(image,controls); }
    for(auto &[entry,bytes] : before) check(!std::memcmp(bytes.data(),reinterpret_cast<void *>(entry),bytes.size()),"real update/four exact release call bytes restored");
    std::puts("real jeep hook install/remove passed without initializers or gameplay");
}
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--real") { if(!std::filesystem::exists(argv[2])) return 77; real(argv[2]); return 0; }
        check(argc==2,"synthetic fixture path required"); nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        selection(image);
        role_requests(image);
        const auto update=nova::abi::require_function(image,"jeep_update"); std::array<uint8_t,7> before{}; std::memcpy(before.data(),reinterpret_cast<void *>(update),7);
        std::vector<std::pair<uint32_t,std::array<uint8_t,5>>> sites;
        {
            nova::abi::GuestHook hook(image,"jeep_update",{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&update_hook)); update_original=hook.original();
            std::vector<std::unique_ptr<nova::abi::GuestCall>> calls;
            for(auto [name,replacement] : {std::pair{"jeep_primary_stop",reinterpret_cast<uint32_t>(&stop_hook)},
                std::pair{"jeep_secondary_stop",reinterpret_cast<uint32_t>(&stop_hook)},
                std::pair{"jeep_primary_animation",reinterpret_cast<uint32_t>(&primary_animation_hook)},
                std::pair{"jeep_secondary_animation",reinterpret_cast<uint32_t>(&secondary_animation_hook)}}) {
                auto entry=nova::abi::require_function(image,name),call=nova::abi::require_function(image,std::string(name)+"_call");
                std::array<uint8_t,5> saved{}; std::memcpy(saved.data(),reinterpret_cast<void *>(call),5); sites.emplace_back(call,saved);
                const bool weapon=std::string(name).find("stop")!=std::string::npos;
                auto checked=std::make_unique<nova::abi::GuestCall>(image,name,call-entry,weapon ? "jeep_stop" : "jeep_animation",replacement);
                if(weapon) stop_original=checked->original(); else animation_original=checked->original();
                calls.push_back(std::move(checked));
            }
            actions(image); look_and_draw(image);
        }
        check(!std::memcmp(before.data(),reinterpret_cast<void *>(update),7),"checked realigning Update trampoline restores exact bytes");
        for(auto &[site,bytes] : sites) check(!std::memcmp(bytes.data(),reinterpret_cast<void *>(site),5),"all four exact CALL sites restored");
        bool rejected=false;
        try { nova::abi::GuestHook bad(image,"jeep_update",{0x8d,0x4c,0x24,0x08,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&update_hook)); } catch(const std::runtime_error &) { rejected=true; }
        check(rejected && !std::memcmp(before.data(),reinterpret_cast<void *>(update),7),"wrong Update entry rejected before mutation");
        for(auto name : {"jeep_primary_stop","jeep_secondary_stop","jeep_primary_animation","jeep_secondary_animation"}) {
            auto entry=nova::abi::require_function(image,name),call=nova::abi::require_function(image,std::string(name)+"_call"); rejected=false;
            try { nova::abi::GuestCall bad(image,name,call-entry,"jeep_reset",reinterpret_cast<uint32_t>(&stop_hook)); } catch(const std::runtime_error &) { rejected=true; }
            check(rejected,"wrong native release target rejected before mutation at every new site");
            for(auto &[site,bytes] : sites) check(!std::memcmp(bytes.data(),reinterpret_cast<void *>(site),5),"rejected release target leaves every exact CALL unchanged");
        }
        std::puts("jeep capability contracts passed"); return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"jeep contract failed: %s\n",error.what()); return 1; }
}
