#include "input/native_bike.h"
#include "input/native_controls.h"
#include "loader/elf_image.h"
#include "bionic/runtime.h"
#include "fixtures/bike_contract.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
#include <thread>
using namespace nova::input;
using nova::abi::invoke;
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
uint32_t address(auto &value) { return reinterpret_cast<uint32_t>(&value); }
void equal(float a,float b,const char *message) { check(std::fabs(a-b)<.0001f,message); }
struct Memory {
    static constexpr uint32_t level=0x1000,component=0x2000,player=0x3000,actor=0x4000,interface_=0x5000,scheme=0x6000;
    std::map<std::pair<uint32_t,uint32_t>,uint32_t> words;
    static inline uint32_t query_entry=0;
    BikeQueries queries{};
    uint64_t generation=0;
    Memory() {
        set(component,0xd,1); set(component,4,actor); set(player,4,actor); set(player,0x170,0x9500);
        set(level,0x17c,0x7000); set(level,0x178,interface_); set(level,0x180,interface_); set(interface_,0xc,scheme);
        unsigned value=0x8000;
        for(auto o : {0x108u,0x128u,0x124u,0xfcu,0x12cu,0x130u,0x134u,0x144u,0x148u,0x14cu,
                      0x10cu,0x150u,0x154u,0x158u,0x1fcu,0x200u}) { set(component,o,value); value+=0x100; }
        set(component,0x100,actor+0x68); set(component,0x104,actor+0x60);
        set(scheme,0x28,0xa000); set(scheme,0x30,0xa100);
        queries={0,actor,component,read(component,0x108),read(component,0x128)};
    }
    void set(uint32_t p,uint32_t o,uint32_t v) { words[{p,o}]=v; }
    uint32_t read(uint32_t p,uint32_t o) { return words[{p,o}]; }
    uint32_t get(uint32_t p,uint32_t t) {
        if(query_entry) return invoke(query_entry,{address(queries),p,t});
        if(p!=actor) return 0;
        return t==0x2e ? component : t==0x2a ? read(component,0x108) : t==0x29 ? read(component,0x128) : 0;
    }
    InputTarget identify() {
        return identify_bike(level,component,player,generation,[&](auto p,auto o) { return read(p,o); },
            [&](auto p,auto o) { return read(p,o); },[&](auto p,auto t) { return get(p,t); },
            [&](auto p,auto slot) { return read(p,slot); });
    }
    bool root(InputTarget owner) {
        return bike_root_matches(owner,level,player,[&](auto p,auto o) { return read(p,o); },[&](auto p,auto o) { return read(p,o); });
    }
};
void identity(const nova::ElfImage &image) {
    Memory::query_entry=nova::abi::require_function(image,"bike_query");
    Memory m; auto owner=m.identify(); check(bool(owner) && owner.kind==TargetKind::bike && m.queries.calls>=3,"valid local Bike identity through aligned native component query");
    TargetCandidate bike{owner.component,true,true,true,{},{},{},owner.bike};
    check(select_input_target(true,{},{},{},{},bike)==owner,"positive exclusive Bike selection");
    check(bike_move_available(true,1) && bike_move_available(true,3) && !bike_move_available(true,2) && !bike_move_available(false,3),"native JoystickBase X-axis gate is preserved independently of Y/enabled");
    const InputTarget old_normal{TargetKind::normal,0x123};
    check(normal_release_can_broadcast(old_normal,true,false,false,false,false,false),"isolated normal context loss retains existing broadcast cleanup");
    check(!normal_release_can_broadcast(old_normal,true,false,false,false,true,false),"normal/Bike conflict cannot broadcast cleanup to a new Bike subscriber");
    for(unsigned owner_index=0;owner_index<4;++owner_index) {
        std::array<TargetCandidate,4> others{}; others[owner_index]={0x111,true,true,true};
        check(!select_input_target(true,others[0],others[1],others[2],others[3],bike),"conflicting owner has no guessed priority");
    }
    for(auto offset : {4u,0x108u,0x128u,0x124u,0xfcu,0x100u,0x104u,0x12cu,0x130u,0x134u,0x144u,0x148u,
                       0x14cu,0x10cu,0x150u,0x154u,0x158u,0x1fcu,0x200u}) {
        Memory changed; auto old=changed.identify(); changed.set(Memory::component,offset,0);
        check(changed.identify()!=old,"same component resource/binding replacement changes selection");
    }
    m.set(Memory::component,0x218,0x1234); check(m.identify()==owner,"advancing native road cursor is not a false transition");
    ++m.generation; check(m.identify()!=owner,"attested SetRoad generation invalidates same-address owner");
    for(unsigned state : {1u,2u,99u}) { m.set(Memory::component,0x120,state); check(!m.identify(),"script/damage/unknown state unsupported"); }
    m.set(Memory::component,0x120,0); m.set(Memory::component,0xd,0); check(!m.identify(),"disabled bike cannot capture");
    Memory remote; remote.set(Memory::player,4,123); check(!remote.identify(),"nonlocal actor rejected");
    Memory handoff; auto old=handoff.identify(); handoff.set(Memory::level,0x178,0x7000); check(!handoff.root(old),"new interface receives no old input release");
    handoff=Memory{}; old=handoff.identify(); handoff.set(Memory::component,4,0xdead); check(!handoff.root(old),"replacement actor receives no stale input calls");
    for(bool manager : {false,true}) {
        Memory dependency; auto t=dependency.identify(); const auto object=manager ? Memory::player : Memory::component;
        const auto offset=manager ? 0x170u : 0xfcu; const auto original=dependency.read(object,offset);
        dependency.set(object,offset,0);
        check(!dependency.identify() && !dependency.root(t),"null current template/manager forbids native input API calls");
        dependency.set(object,offset,original+0x100);
        check(dependency.identify()!=t && dependency.root(t),"valid current dependency replacement keeps controller cancellation authority");
    }
    std::puts("Bike identity: exact local resources, states, conflicts, road generation/cursor and root handoff passed");
}
struct Harness;
Harness *current=nullptr;
uint32_t original_base=0,original_visual=0;
struct Harness {
    const nova::ElfImage &image;
    Memory memory; InputTarget owner;
    BikeContract scene{}; BikeActions actions; FrameInput frame;
    uint32_t update=0,event=0; bool active=true,applied=false,root_valid=true;
    unsigned cleanup_mutation=0,axes=3;
    Harness(const nova::ElfImage &image_) : image(image_),owner(memory.identify()) {
        scene.enabled=scene.move_enabled=scene.primary_enabled=1; scene.template_scale=100; scene.script_target=900;
        update=nova::abi::require_function(image,"bike_update"); event=nova::abi::require_function(image,"bike_event"); current=this;
    }
    bool root() { return root_valid && memory.root(owner); }
    bool live() { return root() && scene.enabled && scene.state==0 && memory.identify()==owner; }
    bool emit(uint32_t type,float x=0) {
        invoke(event,{address(scene),type,std::bit_cast<uint32_t>(x)});
        if(type==14 && cleanup_mutation==1) root_valid=false;
        if(type==14 && cleanup_mutation==2) memory.set(Memory::component,0x130,0);
        return true;
    }
    void clean() { release_bike_actions(actions,[&] { return root(); },[&](auto type,float x) { return emit(type,x); },[&](uint32_t type) { return root_valid && (type==14 ? !scene.move_x : !scene.held && !scene.pressed); }); }
    void apply() {
        if(!active || !live()) { clean(); return; }
        if(applied) return; applied=true;
        apply_bike_actions(frame,actions,[&] { return active && live(); },[&] { return bike_move_available(scene.move_enabled!=0,axes); },
            [&] { return scene.primary_enabled!=0; },[&](auto type,float x) { return emit(type,x); });
    }
    void consumer() {
        validate_bike_primary(actions,[&] { return active && live(); },[&] { return scene.primary_enabled!=0; },
            [&] { return root(); },[&](auto type,float x) { return emit(type,x); },[&] { return root_valid && !scene.held && !scene.pressed; });
    }
    void next(FrameInput next={}) { frame=next; applied=false; }
    void run() { invoke(update,{address(scene),16}); }
    void transition() { clean(); actions={}; ++memory.generation; frame={}; applied=true; owner={}; }
};
void __cdecl base_observer(uint32_t scene,uint32_t dt) { invoke(original_base,{scene,dt}); current->apply(); }
void __cdecl visual_observer(uint32_t scene,uint32_t quaternion) { invoke(original_visual,{scene,quaternion}); current->consumer(); }
void visual_disable(BikeContract *s) { s->primary_enabled=0; }
void visual_resource(BikeContract *) { current->memory.set(Memory::component,0x130,0); }
void visual_handoff(BikeContract *) { current->root_valid=false; }
void base_script(BikeContract *s) { current->transition(); s->state=1; }
void base_focus(BikeContract *) { current->active=false; }
FrameInput input(bool right,bool fire,bool edge=true) {
    FrameInput f; f.held.set(static_cast<size_t>(Action::right),right); f.held.set(static_cast<size_t>(Action::fire),fire);
    if(edge) f.pressed.push_back(Action::fire); return f;
}
void actions(const nova::ElfImage &image) {
    original_base=nova::abi::require_function(image,"bike_base"); original_visual=nova::abi::require_function(image,"bike_visual");
    const auto update=nova::abi::require_function(image,"bike_update");
    const auto base=nova::abi::require_function(image,"bike_base_call"),visual=nova::abi::require_function(image,"bike_visual_call");
    std::array<uint8_t,5> saved_base{},saved_visual{};
    std::memcpy(saved_base.data(),reinterpret_cast<void *>(base),5); std::memcpy(saved_visual.data(),reinterpret_cast<void *>(visual),5);
    {
        nova::abi::GuestCall b(image,"bike_update",base-update,"bike_base",reinterpret_cast<uint32_t>(&base_observer));
        nova::abi::GuestCall v(image,"bike_update",visual-update,"bike_visual",reinterpret_cast<uint32_t>(&visual_observer));
        Harness h(image); h.next(input(true,true)); h.run(); h.run();
        check(h.scene.shots==1 && h.scene.events[2]==1,"held/repeated callback fires once per press");
        check(h.scene.base_calls==2 && h.scene.visual_calls==2 && h.scene.native_args[0]==16 && h.scene.native_args[1]==77,"aligned native calls forward every argument and update");
        check(h.scene.forward==64 && h.scene.camera>6,"automatic forward/camera native phase preserved");
        equal(h.scene.lean,.032f,"native gain clamp and smoothing used");
        h.next(input(true,true,false)); h.run(); check(h.scene.shots==1,"hold never generates another pressed edge");
        h.next(); h.run(); check(!h.scene.move_x && !h.scene.held && !h.scene.pressed,"native move and primary release inputs clear");
        h.next(input(false,false)); h.run(); check(h.scene.shots==2,"short down/up click reaches native consumer once");
        h.clean(); h.clean(); check(h.scene.events[3]==2,"actual input release occurs once despite repeated cleanup");
        for(unsigned axes : {1u,2u,3u}) {
            Harness c(image); c.axes=axes; c.next(input(true,true)); c.run();
            check(c.scene.events[0]==unsigned((axes&1)!=0) && c.scene.forward==32 && c.scene.shots==1,
                "native X-axis lock gates only lateral user input, retaining native forward and primary");
            if(axes&1) { c.axes=2; c.next(input(true,true,false)); c.run(); check(!c.scene.move_x && c.scene.events[1]==1,"lost X-axis permission cancels still-owned queued movement"); }
        }
        for(auto mutation : {visual_disable,visual_resource}) {
            Harness c(image); c.next(input(true,true)); c.scene.after_visual=mutation; c.run();
            check(!c.scene.shots && !c.scene.pressed && c.scene.events[3]==1,"enabled/resource loss after original visual call cancels queued press before consumer");
            c.clean(); check(c.scene.events[1]==1,"camera-only replacement retains current input cleanup authority");
        }
        {
            Harness c(image); c.next(input(true,true)); c.scene.after_visual=visual_handoff; c.run();
            check(!c.scene.events[3],"takeover after callback receives no stale release call");
        }
        {
            Harness c(image); c.next(input(true,true)); c.scene.after_base=base_script; c.run();
            check(!c.scene.shots && !c.scene.events[0] && c.scene.forward==900,"base state transition precedes input eligibility and scripted following");
        }
        for(bool focused : {false,true}) {
            Harness c(image); c.next(input(true,true)); c.scene.after_base=focused ? nullptr : base_focus; c.run();
            if(focused) { c.active=false; c.clean(); }
            check(!c.scene.move_x && !c.scene.held,"pause/focus cleanup uses native input release without gun calls");
        }
        {
            Harness c(image); c.next(input(true,true)); c.run();
            Controls controls{ControlsConfig{}}; controls.gameplay=true; controls.overlay(true);
            c.active=controls.active(); c.clean(); c.clean();
            check(!c.scene.move_x && !c.scene.held && !c.scene.pressed,"overlay invokes actual bike movement/primary release");
        }
        for(unsigned mutation : {0u,1u,2u}) {
            Harness c(image); c.next(input(true,true)); c.run(); c.cleanup_mutation=mutation; c.clean(); c.clean();
            check(c.scene.events[1]==1 && c.scene.events[3]==unsigned(mutation!=1),"every cleanup operation rechecks current root, resource-only change does not lose input release");
        }
        for(auto state : {1u,2u}) {
            Harness c(image); c.next(input(true,true)); c.run(); c.transition(); c.scene.state=state; c.run();
            c.scene.state=0; c.owner=c.memory.identify(); c.next(); c.run();
            check(c.scene.shots==1 && !c.scene.pressed && !c.scene.move_x,"pre-script handoff clears queued native input; return requires a fresh press");
            c.next(input(true,true)); c.run(); check(c.scene.shots==2,"fresh press works after state handoff");
        }
        {
            Harness a(image); a.next(); a.frame.dx=300; a.frame.dy=-200; a.run(); const auto camera=a.scene.camera;
            Harness b0(image); b0.next(); b0.run(); equal(camera,b0.scene.camera,"unsupported XY does not change automatic native camera");
        }
    }
    check(!std::memcmp(saved_base.data(),reinterpret_cast<void *>(base),5) && !std::memcmp(saved_visual.data(),reinterpret_cast<void *>(visual),5),"both exact consumer CALLs restored");
    bool rejected=false; try { nova::abi::GuestCall invalid(image,"bike_update",base-update,"bike_visual",reinterpret_cast<uint32_t>(&base_observer)); }
    catch(const std::runtime_error &) { rejected=true; } check(rejected,"wrong CALL target rejected");
    std::puts("Bike native phase/actions: aligned production helpers, press/hold/click, guards, mutation, cleanup and no mouse bypass passed");
}
uint32_t transition_original=0;
unsigned transition_mode=0;
void __cdecl transition2(uint32_t s,uint32_t value) {
    const bool cancel=transition_mode==0 ? bike_enable_transition(current->scene.enabled!=0,(value&0xff)!=0) :
        transition_mode==1 ? bike_state_transition(current->scene.state,value) : true;
    if(cancel) current->transition(); invoke(transition_original,{s,value});
}
void __cdecl transition1(uint32_t s) { current->transition(); invoke(transition_original,{s}); }
void __cdecl transition4(uint32_t s,uint32_t p,uint32_t d,uint32_t a) { current->transition(); invoke(transition_original,{s,p,d,a}); }
void transitions(const nova::ElfImage &image) {
    struct Entry { const char *name; std::vector<uint8_t> prologue; unsigned args; };
    const std::array<Entry,7> entries{{
        {"bike_enable",{0x55,0x89,0xe5,0x83,0xec,0x58},2}, {"bike_state",{0x55,0x89,0xe5,0x81,0xec,0xe8,0,0,0},2},
        {"bike_road",{0x55,0x89,0xe5,0x83,0xe4,0xf0},2}, {"bike_configure",{0x55,0x89,0xe5,0x56,0x83,0xec,0x14},2},
        {"bike_move_to",{0x55,0x89,0xe5,0x56,0x53},4}, {"bike_reset",{0x55,0x89,0xe5,0x56,0x53},1},
        {"bike_load",{0x55,0x89,0xe5,0x57,0x56,0x53},2}}};
    // GuestHook accepts initializer_list; exercise each exact real-layout entry.
    {
        Harness h(image); h.actions={true,true}; h.emit(13,1); h.emit(29);
        const auto save=nova::abi::require_function(image,"bike_save");
        invoke(save,{address(h.scene),87});
        check(h.scene.move_x==100 && h.scene.held && h.scene.pressed && h.scene.native_args[3]==87,
            "nonmutating SaveSave forwards serialization without cancelling held input");
    }
    auto test=[&](const Entry &e,std::initializer_list<uint8_t> expected) {
        const auto at=nova::abi::require_function(image,e.name); std::vector<uint8_t> saved(expected.size());
        std::memcpy(saved.data(),reinterpret_cast<void *>(at),saved.size());
        { nova::abi::GuestHook hook(image,e.name,expected,e.args==4 ? reinterpret_cast<uint32_t>(&transition4) :
            e.args==1 ? reinterpret_cast<uint32_t>(&transition1) : reinterpret_cast<uint32_t>(&transition2));
          transition_original=hook.original(); transition_mode=std::string(e.name)=="bike_enable" ? 0 : std::string(e.name)=="bike_state" ? 1 : 2;
          Harness h(image); h.actions={true,true}; h.emit(13,1); h.emit(29);
          if(transition_mode<2) {
              invoke(at,{address(h.scene),transition_mode==0 ? 1u : 0u});
              check(h.scene.move_x==100 && h.scene.held && h.scene.pressed && h.actions.moving && h.actions.primary,
                  "already enabled and same-state zero are native no-ops; held input survives");
              h.scene.transitions=0;
          }
          if(e.args==4) invoke(at,{address(h.scene),5,6,7}); else if(e.args==1) invoke(at,{address(h.scene)}); else invoke(at,{address(h.scene),transition_mode==0 ? 0u : 1u});
          check(!h.scene.move_x && !h.scene.pressed && !h.scene.held && h.scene.transitions==1,"pre-transition aligned hook cancels only owned input then forwards native handoff");
          if(e.args==4) check(h.scene.native_args[1]==6 && h.scene.native_args[2]==7,"all four StartMoveTo args forwarded");
        }
        check(!std::memcmp(saved.data(),reinterpret_cast<void *>(at),saved.size()),"transition entry bytes restored");
    };
    test(entries[0],{0x55,0x89,0xe5,0x83,0xec,0x58}); test(entries[1],{0x55,0x89,0xe5,0x81,0xec,0xe8,0,0,0});
    test(entries[2],{0x55,0x89,0xe5,0x83,0xe4,0xf0}); test(entries[3],{0x55,0x89,0xe5,0x56,0x83,0xec,0x14});
    test(entries[4],{0x55,0x89,0xe5,0x56,0x53}); test(entries[5],{0x55,0x89,0xe5,0x56,0x53}); test(entries[6],{0x55,0x89,0xe5,0x57,0x56,0x53});
    bool rejected=false; try { nova::abi::GuestHook bad(image,"bike_enable",{0x55,0x89,0xe5,0x83,0xec,0x54},reinterpret_cast<uint32_t>(&transition2)); }
    catch(const std::runtime_error &) { rejected=true; } check(rejected,"mismatched transition prologue rejected");
}
void pending_cancellation(const nova::ElfImage &image) {
    const auto event=nova::abi::require_function(image,"bike_event");
    for(unsigned failure : {0u,1u,2u}) {
        Memory m; auto owner=m.identify(); BikeContract scene{}; scene.template_scale=100; scene.held=scene.pressed=1; scene.move_x=100;
        BikeActions input{true,true}; BikeCancellation pending;
        retain_bike_cancellation(pending,owner,Memory::level,input);
        if(failure==0) m.set(Memory::component,0xfc,0);
        if(failure==1) m.set(Memory::player,0x170,0);
        bool retained=true; unsigned calls=0;
        auto clean=[&] { clean_bike_cancellation(pending,[&] { return retained; },[&] { return m.root(owner); },
            [&](uint32_t type,float x) {
                ++calls; invoke(event,{address(scene),type,std::bit_cast<uint32_t>(x)});
                if(failure==2 && type==14) m.set(Memory::player,0x170,0);
                return true;
            },[&](uint32_t type) { return retained && (type==14 ? !scene.move_x : !scene.held && !scene.pressed); }); };
        clean();
        check(bool(pending.owner) && pending.actions.primary && pending.actions.moving==(failure!=2),
            "unavailable input API retains each unacknowledged cancellation independently");
        check(calls==unsigned(failure==2),"no native event dereferences unavailable template/manager");
        m.set(Memory::component,0xfc,0x9000); m.set(Memory::player,0x170,0x9100);
        clean(); clean(); check(!pending.owner && calls==2 && !scene.move_x && !scene.held && !scene.pressed,
            "matching current API recovery completes pending cancellation once before new input");
        input={true,true}; retain_bike_cancellation(pending,owner,Memory::level,input); retained=false; clean();
        check(!pending.owner && calls==2,"lost root abandons pending identity without a stale/new-owner call");
    }
}

struct OriginHarness;
OriginHarness *origin_current=nullptr;
uint32_t event_original=0,reset_one_original=0,reset_all_original=0;
thread_local BikeHostEventTag origin_tag;
thread_local uint32_t origin_depth=0;
thread_local bool origin_foreign=false;
struct OriginHarness {
    BikeContract scene{}; BikeWriterEpochs epochs; BikeActions actions; BikeCancellation pending;
    uint32_t event=0; bool api=true,core=true; unsigned native_calls=0,raw_writes=0,last_return=0;
    OriginHarness(const nova::ElfImage &image) {
        origin_current=this; scene.enabled=scene.move_enabled=scene.primary_enabled=1; scene.template_scale=100; scene.carry_scale=1; epochs.receiver=address(scene);
        event=nova::abi::require_function(image,"bike_on_event");
    }
    void sync() { revoke_bike_stale(actions,epochs.move,epochs.primary,epochs.foreign); revoke_bike_stale(pending.actions,epochs.move,epochs.primary,epochs.foreign); }
    void emit(uint32_t type,uint32_t value=0,uint32_t down=0,bool host=false) {
        uint32_t e[]{0,type,value,down}; const auto saved=origin_tag;
        const auto move=epochs.move.load(),primary=epochs.primary.load();
        if(host) origin_tag={address(scene),address(e),origin_depth+1,true};
        if(host && type==13) actions.moving=true;
        if(host && type==29) actions.primary=true;
        last_return=invoke(event,{address(scene),address(e),77}); origin_tag=saved;
        if(host && type==13 && move==epochs.move) {
            actions.move_generation=move; actions.move_bits=std::bit_cast<uint32_t>(scene.move_x); actions.move_raw=true;
        }
        if(host && type==29 && primary==epochs.primary) { actions.primary_generation=primary; actions.primary_raw=true; actions.primary_neutral=false; }
        sync();
    }
    void acquire() { emit(13,std::bit_cast<uint32_t>(1.f),0,true); emit(29,0,0,true); }
    void queue() { InputTarget owner{TargetKind::bike,42}; owner.bike.actor=1; owner.bike.player=2; owner.bike.interface_=3; owner.bike.scheme=4;
        retain_bike_cancellation(pending,owner,1,actions); }
    void fallback() {
        sync(); neutralize_owned_bike_input(pending.actions,[&] { sync(); return core && scene.enabled && scene.state==0 && !api && !epochs.foreign; },
            [&](uint32_t o) { return o==0xc ? std::bit_cast<uint32_t>(scene.move_x) : o==0x20 ? scene.held : scene.pressed; },
            [&](uint32_t o,uint32_t v) { ++raw_writes; if(o==0xc) scene.move_x=std::bit_cast<float>(v); else if(o==0x20) scene.held=v; else scene.pressed=v; });
    }
    void clean() {
        sync(); clean_bike_cancellation(pending,[&] { return core; },[&] { return core && scene.enabled && scene.state==0 && api && !epochs.foreign; },
            [&](uint32_t type,float) {
                return bike_cancel_boundary(pending.actions,type,[&] { return core && scene.enabled && scene.state==0 && api && !epochs.foreign; },[&] { sync(); },[&] {
                    validate_bike_raw(pending.actions,[&](uint32_t o) { return o==0xc ? std::bit_cast<uint32_t>(scene.move_x) : o==0x20 ? scene.held : scene.pressed; });
                },[&] { ++native_calls; emit(type,0,0,true); });
            },
            [&](uint32_t type) { return core && !epochs.foreign && (type==14 ? !scene.move_x : !scene.held && !scene.pressed); });
    }
};
uint32_t __cdecl origin_event_observer(uint32_t control,uint32_t e,uint32_t manager) {
    auto &h=*origin_current; ++origin_depth;
    const bool tracked=control==h.epochs.receiver.load();
    const auto type=tracked ? reinterpret_cast<uint32_t *>(e)[1] : 64;
    const auto key=type==1 ? reinterpret_cast<uint32_t *>(e)[2] : 0;
    const bool host=tracked && claim_bike_host_event(origin_tag,control,e,origin_depth);
    const auto result=observe_bike_writer(h.epochs,tracked,host,tracked && origin_foreign,type,key,
        [&] { return invoke(event_original,{control,e,manager}); },[&] { return control==h.epochs.receiver.load(); },
        [&](uint32_t generation) {
            const auto value=std::bit_cast<uint32_t>(h.scene.move_x);
            bike_carry_readback(h.actions,generation,value); bike_carry_readback(h.pending.actions,generation,value);
        });
    --origin_depth; return result;
}
void __cdecl origin_reset_one(uint32_t control) {
    auto &h=*origin_current; observe_bike_reset(h.epochs,control==h.epochs.receiver.load(),origin_foreign,false,[&] { invoke(reset_one_original,{control}); });
}
void __cdecl origin_reset_all(uint32_t control) {
    auto &h=*origin_current; observe_bike_reset(h.epochs,control==h.epochs.receiver.load(),origin_foreign,true,[&] { invoke(reset_all_original,{control}); });
}
void reentrant_press(BikeContract *s) {
    s->after_event=nullptr; origin_current->emit(33); // Native nested press, even with the outer host tag in scope.
}
void __cdecl origin_base_boundary(uint32_t scene,uint32_t dt) {
    invoke(original_base,{scene,dt}); origin_current->fallback();
}
void __cdecl origin_primary_boundary(uint32_t scene,uint32_t quaternion) {
    invoke(original_visual,{scene,quaternion}); origin_current->fallback();
}
void origin_api_loss(BikeContract *) { origin_current->api=false; }
void origin_new_press_and_loss(BikeContract *) { origin_current->api=false; origin_current->emit(33); }
void provenance(const nova::ElfImage &image) {
    const auto event=nova::abi::require_function(image,"bike_on_event");
    const auto reset_one=nova::abi::require_function(image,"bike_reset_one"),reset_all=nova::abi::require_function(image,"bike_reset_all");
    std::array<uint8_t,6> saved_event{},saved_all{}; std::array<uint8_t,5> saved_one{};
    std::memcpy(saved_event.data(),reinterpret_cast<void *>(event),6); std::memcpy(saved_one.data(),reinterpret_cast<void *>(reset_one),5); std::memcpy(saved_all.data(),reinterpret_cast<void *>(reset_all),6);
    {
        nova::abi::GuestHook e(image,"bike_on_event",{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&origin_event_observer)); event_original=e.original();
        nova::abi::GuestHook one(image,"bike_reset_one",{0x55,0x31,0xd2,0x89,0xe5},reinterpret_cast<uint32_t>(&origin_reset_one)); reset_one_original=one.original();
        nova::abi::GuestHook all(image,"bike_reset_all",{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&origin_reset_all)); reset_all_original=all.original();
        {
            OriginHarness h(image); h.acquire(); const auto generation=h.epochs.move.load();
            h.scene.carry_scale=1.000001f; h.emit(11); h.emit(18); h.emit(19); h.emit(1,3,1);
            check(h.actions.moving && h.actions.primary && h.epochs.move==generation && h.actions.move_bits==std::bit_cast<uint32_t>(h.scene.move_x),
                "ordinary native Draw11/Y-only/unrelated-key carries preserve logical host origin with current raw readback");
            h.queue(); h.clean(); h.clean(); check(!h.scene.move_x && !h.scene.held && h.native_calls==2,"derived carry cannot orphan key-up cleanup or cause drift");
        }
        {
            OriginHarness h(image); h.acquire(); h.emit(11);
            check(h.last_return==0x7b0b && h.scene.native_args[3]==77,"onEvent preserves original EAX word and manager argument through observer postwork");
        }
        {
            const auto update=nova::abi::require_function(image,"bike_update");
            const auto base=nova::abi::require_function(image,"bike_base_call"),visual=nova::abi::require_function(image,"bike_visual_call");
            nova::abi::GuestCall b(image,"bike_update",base-update,"bike_base",reinterpret_cast<uint32_t>(&origin_base_boundary));
            nova::abi::GuestCall v(image,"bike_update",visual-update,"bike_visual",reinterpret_cast<uint32_t>(&origin_primary_boundary));
            for(unsigned phase : {0u,1u,2u}) {
                OriginHarness h(image); h.acquire(); h.queue(); h.scene.released=37;
                if(phase==0) h.scene.after_base=origin_api_loss;
                else h.scene.after_visual=phase==1 ? origin_api_loss : origin_new_press_and_loss;
                invoke(update,{address(h.scene),16});
                check(h.scene.forward==32 && h.scene.base_calls==1 && h.scene.visual_calls==1,"fallback preserves actual base/movement/camera consumer order and automatic following");
                check(h.scene.shots==unsigned(phase==2) && h.scene.released==(phase==2 ? 0u : 37u),
                    "missing API at actual base/late-primary boundary cancels owned press while preserving native new press and release byte");
                if(phase<2) check(!h.scene.move_x && !h.scene.held && !h.scene.pressed && h.pending.actions.primary,
                    "critical input neutralization at native consumers is not an API completion acknowledgement");
                h.api=true; h.clean(); check(!h.pending.owner,"safe recovery completes only remaining owned API side effects");
            }
        }
        for(auto type : {7u,13u,14u,20u,21u,22u,23u}) {
            OriginHarness h(image); h.acquire(); h.queue(); h.emit(type,std::bit_cast<uint32_t>(.5f)); h.sync();
            check(!h.pending.actions.moving && h.pending.actions.primary,"explicit native X intent revokes only movement cancellation");
        }
        for(auto type : {29u,30u,33u,34u}) {
            OriginHarness h(image); h.acquire(); h.queue(); h.emit(type); h.clean();
            check(h.native_calls==1 && (type==29 || type==33 ? h.scene.pressed==1 : !h.scene.pressed),"same-value native re-press/release is a new primary origin, no deferred release into it");
        }
        for(auto key : {10u,13u,15u}) {
            OriginHarness h(image); h.acquire(); h.queue(); h.emit(1,key,1); h.sync();
            check(h.pending.actions.moving==(key==15) && h.pending.actions.primary==(key!=15),"native key table revokes exactly its own intent channel");
        }
        {
            OriginHarness h(image); h.acquire(); h.queue(); h.api=false; h.scene.released=17; h.fallback();
            check(!h.scene.move_x && !h.scene.held && !h.scene.pressed && h.scene.released==17 && h.pending.actions.moving && h.pending.actions.primary,
                "API absence neutralizes only positively owned input and preserves release/history fields without fake ACK");
            h.api=true; h.clean(); h.clean(); check(h.native_calls==2 && !h.pending.owner,"actual safe API returns alone complete cancellation after fallback");
        }
        {
            OriginHarness h(image); h.acquire(); h.scene.pressed=0; h.queue(); h.api=false; h.fallback();
            check(!h.scene.held && h.pending.actions.primary,"expected native pressed consumption is neither new intent nor release ACK");
        }
        {
            OriginHarness h(image); h.acquire(); h.queue(); h.api=false; h.core=false; h.fallback();
            check(h.scene.move_x && h.scene.pressed && !h.raw_writes,"script/disabled/new core never receives fallback writes");
            h.emit(33); h.sync(); h.core=true; h.api=true; h.clean(); check(h.scene.pressed==1,"pending retained observer preserves newer script-state press on return");
        }
        {
            OriginHarness h(image); h.acquire(); h.queue(); h.api=false; h.emit(33); h.fallback();
            check(!h.scene.move_x && h.scene.pressed && h.scene.held,"partial fallback preserves identical unowned native primary while clearing still-owned move");
        }
        {
            OriginHarness h(image); h.acquire(); h.queue(); h.scene.move_x=9; h.api=false; h.fallback();
            check(h.scene.move_x==9 && !h.pending.actions.moving,"different raw unowned overwrite revokes fallback and deferred API authority");
            h.api=true; h.clean(); check(h.scene.move_x==9 && h.native_calls==1,"API recovery cannot cancel a raw overwrite whose origin was lost");
        }
        {
            OriginHarness h(image); h.scene.after_event=reentrant_press; h.emit(29,0,0,true);
            check(!h.actions.primary && h.scene.pressed && h.epochs.primary==1,"nested native press cannot inherit one-shot exact host tag");
        }
        for(bool primary : {false,true}) {
            OriginHarness h(image); h.acquire(); h.queue(); unsigned calls=0;
            const bool called=bike_cancel_boundary(h.pending.actions,primary ? 30u : 14u,[&] {
                h.emit(primary ? 33u : 13u,std::bit_cast<uint32_t>(1.f)); return true;
            },[&] { h.sync(); },[] {},[&] { ++calls; });
            check(!called && !calls,"native same-value intent inside final validation query revokes API cancellation before emission");
            h.api=false; h.raw_writes=0;
            neutralize_owned_bike_input(h.pending.actions,[&] {
                h.emit(primary ? 33u : 13u,std::bit_cast<uint32_t>(1.f)); h.sync(); return true;
            },[&](uint32_t o) { return o==0xc ? std::bit_cast<uint32_t>(h.scene.move_x) : o==0x20 ? h.scene.held : h.scene.pressed; },
            [&](uint32_t,uint32_t) { ++h.raw_writes; });
            check(h.raw_writes==unsigned(primary ? 1 : 2),"query revocation blocks the affected fallback channel independently");
        }
        {
            Memory m; const auto a=m.identify(); auto resource=a; resource.bike.camera+=4;
            check(same_bike_core(a,1,resource,1),"resource-only rebind retains core epoch and sticky poison");
            auto changed=a; changed.bike.actor+=4;
            check(!same_bike_core(a,1,changed,1),"same receiver address with a new actor is a true core epoch");
            changed=a; changed.bike.interface_+=4; check(!same_bike_core(a,1,changed,1),"same pointer UI replacement is a true core epoch");
            check(!same_bike_core(a,1,a,2),"level change is a true core epoch");
        }
        for(bool complete : {false,true}) {
            OriginHarness h(image); h.acquire(); h.queue(); invoke(complete ? reset_all : reset_one,{address(h.scene)}); h.sync();
            check(!h.pending.actions.moving && h.pending.actions.primary==!complete,"exact native resets revoke their channels without API acknowledgement");
        }
        {
            OriginHarness h(image); h.acquire(); h.queue(); const auto m=h.epochs.move.load(),p=h.epochs.primary.load(); h.emit(64);
            check(h.epochs.move==m && h.epochs.primary==p,"event64 early return introduces no carry/writer");
            std::thread foreign([&] {
                origin_foreign=true; uint32_t native_event[]{0,11,0,0};
                invoke(h.event,{address(h.scene),address(native_event),77}); origin_foreign=false;
            }); foreign.join();
            h.api=false; h.fallback(); h.api=true; h.clean();
            check(h.epochs.foreign && !h.raw_writes && !h.native_calls,"foreign observer poisons future host operations without guest queries or cross-call locks");
        }
    }
    check(!std::memcmp(saved_event.data(),reinterpret_cast<void *>(event),6) && !std::memcmp(saved_one.data(),reinterpret_cast<void *>(reset_one),5) && !std::memcmp(saved_all.data(),reinterpret_cast<void *>(reset_all),6),"writer/reset hook bytes restored");
    bool rejected=false; try { nova::abi::GuestHook wrong(image,"bike_reset_one",{0x55,0x31,0xd1,0x89,0xe5},reinterpret_cast<uint32_t>(&origin_reset_one)); }
    catch(const std::runtime_error &) { rejected=true; } check(rejected,"other XOR encodings remain rejected");
    {
        Controls controls{ControlsConfig{}}; controls.gameplay=true;
        SDL_Event click{}; click.type=SDL_EVENT_MOUSE_BUTTON_DOWN; click.button.button=SDL_BUTTON_LEFT;
        SDL_Event motion{}; motion.type=SDL_EVENT_MOUSE_MOTION; motion.motion.xrel=10;
        check(controls.active() && controls.handle(click) && controls.handle(motion),"poisoned native Bike retains gameplay capture; no gameplay touch fallback");
        SDL_Event escape{}; escape.type=SDL_EVENT_KEY_DOWN; escape.key.key=SDLK_ESCAPE;
        check(!controls.handle(escape),"unbound Escape retains original Android BACK route");
        controls.gameplay=false; controls.clear();
        check(!controls.active() && !controls.handle(motion),"native menu/top-state handoff releases capture for existing menu touch behavior");
    }
    std::puts("Bike provenance: native intent versus carry, nested tags, resets, foreign poison, safe owned-input fallback and actual-return pending ACK passed");
}

void draw_position_effect(BikeContract *scene) { scene->cursor+=7; }
void drawing(const nova::ElfImage &image) {
    {
        const auto draw=nova::abi::require_function(image,"bike_visual");
        BikeContract visible{},hidden{}; visible.after_visual=hidden.after_visual=draw_position_effect;
        draw_bike_artwork(false,255,[&](uint32_t alpha) { invoke(draw,{address(visible),alpha}); });
        draw_bike_artwork(true,255,[&](uint32_t alpha) { invoke(draw,{address(hidden),alpha}); });
        check(visible.visual_calls==1 && hidden.visual_calls==1 && visible.cursor==7 && hidden.cursor==7 &&
            visible.native_args[1]==255 && hidden.native_args[1]==0,"zero-alpha scoped artwork retains original Draw/UI position effects and aligned native call");
    }
    for(bool live : {false,true}) for(bool fail : {false,true}) {
        std::map<uint32_t,uint8_t> visible{{1,1},{2,0},{3,1}}; unsigned side_effects=0;
        try { draw_bike_scheme(live,[] { return true; },[] { return true; },[](uint32_t slot) { return slot==0x28 ? 1u : 2u; },
            [&](uint32_t p) { return visible[p]; },[&](uint32_t p,bool v) { visible[p]=v; },[&] {
                ++side_effects; check(visible[1]==unsigned(!live) && !visible[2] && visible[3],"only exact Bike artwork hidden, native neighbor preserved");
                if(fail) throw std::runtime_error("draw");
            }); } catch(const std::runtime_error &) { check(fail,"unexpected Draw error"); }
        check(side_effects==1 && visible[1] && !visible[2] && visible[3],"original scheme input effects forwarded; flags restored on exception");
    }
    {
        bool live=true; unsigned sets=0,draws=0;
        draw_bike_scheme(true,[&] { return live; },[&] { return live; },[](uint32_t slot) { return slot; },[](uint32_t) { return uint8_t(1); },
            [&](uint32_t,bool) { ++sets; },[&] { ++draws; live=false; });
        check(sets==2 && draws==1,"Draw handoff cannot restore stale controls into a replacement owner");
    }
    for(unsigned loss : {0u,1u,2u,3u}) {
        bool eligible=true,owns=true; unsigned sets=0; std::map<uint32_t,uint8_t> flags{{0x28,1},{0x30,0}};
        draw_bike_scheme(true,[&] { return eligible; },[&] { return owns; },[](uint32_t slot) { return slot; },
            [&](uint32_t p) { return flags[p]; },[&](uint32_t p,bool v) { ++sets; flags[p]=v; },[&] {
                eligible=false; owns=loss<2; // camera/config or pause versus new root/control.
            });
        check(sets==(loss<2 ? 4u : 2u) && flags[0x28]==unsigned(loss<2) && !flags[0x30],
            "same artwork restores across resource/pause loss; new root/control forbids stale restore");
    }
    check(bike_touch(1,1,2) && !bike_touch(3,1,2),"pure native artwork has exact Bike control identity");
}
void real(const char *path) {
    nova::ImportRegistry imports; nova::bionic::register_boot(imports); nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports); Controls controls{ControlsConfig{}};
    std::map<uint32_t,std::vector<uint8_t>> bytes;
    for(auto name : {"_ZN14CPlayerControl7onEventEPK6IEventPK12EventManager","_ZN14CPlayerControl12ResetControlEv","_ZN14CPlayerControl13ResetControlsEv",
        "_ZN20CPlayerBikeComponent6EnableEb","_ZN20CPlayerBikeComponent8SetStateEi",
        "_ZN20CPlayerBikeComponent7SetRoadEP15CWayPointObject","_ZN20CPlayerBikeComponent9ConfigureEPK26CComponentPlayerBikeConfig",
        "_ZN20CPlayerBikeComponent11StartMoveToERKN6glitch4core8vector3dIfEES5_PKc","_ZN20CPlayerBikeComponent14ResetComponentEv",
        "_ZN20CPlayerBikeComponent8SaveLoadEP13CMemoryStream"}) {
        auto at=nova::abi::require_function(image,name); bytes[at]=std::vector<uint8_t>(12); std::memcpy(bytes[at].data(),reinterpret_cast<void *>(at),12);
    }
    const auto save=nova::abi::require_function(image,"_ZN20CPlayerBikeComponent8SaveSaveEP13CMemoryStream");
    bytes[save]=std::vector<uint8_t>(32); std::memcpy(bytes[save].data(),reinterpret_cast<void *>(save),32);
    auto update=nova::abi::require_function(image,"_ZN20CPlayerBikeComponent6UpdateEi");
    for(auto offset : {0x34u,0x13c8u}) { bytes[update+offset]=std::vector<uint8_t>(5); std::memcpy(bytes[update+offset].data(),reinterpret_cast<void *>(update+offset),5); }
    { NativeControls native(image,controls); }
    for(auto &[at,saved] : bytes) check(!std::memcmp(saved.data(),reinterpret_cast<void *>(at),saved.size()),"actual Bike CALL/entry restored after complete adapter install");
    std::puts("real Bike checked hook installation/restoration passed without guest initialization/gameplay");
}
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--real") { if(!std::filesystem::exists(argv[2])) return 77; real(argv[2]); return 0; }
        check(argc==2,"fixture path required"); nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        identity(image); actions(image); transitions(image); pending_cancellation(image); provenance(image); drawing(image); std::puts("Bike native action contracts passed"); return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"Bike contracts failed: %s\n",e.what()); return 1; }
}
