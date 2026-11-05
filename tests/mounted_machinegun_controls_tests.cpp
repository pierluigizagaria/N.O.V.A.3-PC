#include "input/native_controls.h"
#include "bionic/runtime.h"
#include "fixtures/mounted_machinegun_contract.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
using namespace nova::input;
using nova::abi::invoke;
namespace {
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
void close_to(float value,float expected,const char *message) { check(std::isfinite(value) && std::abs(value-expected)<.0002f,message); }
uint32_t address(auto &value) { return reinterpret_cast<uint32_t>(&value); }
struct Memory {
    std::map<std::pair<uint32_t,uint32_t>,uint32_t> words;
    MountedQueries queries{};
    uint32_t query;
    static constexpr uint32_t level=0x1000,component=0x2000,player=0x3000,actor=0x4000,gun=0x5000;
    static constexpr uint32_t parent=0x6000,car=0x7000,ai=0x8000,weapon=0x9000,manager=0xa000,normal=0xb000;
    explicit Memory(const nova::ElfImage &image) : query(nova::abi::require_function(image,"mounted_query")) {
        set(component,0xd,1); set(component,0x22c,0); set(component,0x238,player); set(component,4,actor);
        set(player,4,actor); set(actor,0x38,gun); set(component,0x118,gun); set(gun,0x38,parent);
        set(component,0x11c,ai); set(component,0x120,weapon); set(player,0x170,manager);
        set(ai,4,gun); set(ai,0x78,actor); set(ai,0x24,0xc000); set(component,0xfc,0xd000);
        set(component,0x100,ai+0x54); set(component,0x104,ai+0x58); set(ai,0x44,0xe000);
        set(component,0x108,0xf000); set(component,0x110,0x10000); set(component,0x124,0x11000);
        set(level,0x17c,normal); set(level,0x178,normal); set(normal,0xc,0x12000);
        queries.count=3; queries.entries[0]={parent,0x5e,car}; queries.entries[1]={gun,0x43,ai}; queries.entries[2]={gun,0x47,weapon};
    }
    void set(uint32_t object,uint32_t offset,uint32_t value) { words[{object,offset}]=value; }
    uint32_t read(uint32_t object,uint32_t offset) const {
        auto i=words.find({object,offset}); check(i!=words.end(),"no unknown/stale object dereference"); return i->second;
    }
    auto reader() { return [&](uint32_t p,uint32_t o) { return read(p,o); }; }
    auto getter() { return [&](uint32_t p,uint32_t type) { return invoke(query,{address(queries),p,type}); }; }
    InputTarget identify(uint32_t local_player=player) { return identify_machinegun(level,component,local_player,reader(),reader(),getter()); }
    bool structure(InputTarget target) { return machinegun_structure_matches(target,level,player,reader(),reader(),getter()); }
    bool controller(InputTarget target) { return structure(target) && machinegun_controller_matches(target,reader(),getter()) && structure(target); }
    bool weapon_valid(InputTarget target) { return structure(target) && machinegun_weapon_matches(target,reader(),getter()) && structure(target); }
    bool manager_valid(InputTarget target) { return structure(target) && read(player,0x170)==target.machinegun.manager; }
};
void identity(const nova::ElfImage &image) {
    Memory m(image); auto original=m.identify(); check(bool(original) && mounted_machinegun(original),"positive local CarPhysics mounted identity");
    check(m.queries.calls>=3,"production identification reaches aligned native component queries");
    for(unsigned mask=0;mask<16;++mask) {
        const bool normal=(mask&1)!=0,mech=(mask&2)!=0,mg=(mask&4)!=0,jeep=(mask&8)!=0;
        auto t=select_input_target(true,{11,normal,true,true},{22,mech,true,true},
            {Memory::component,mg,true,true,{},{},original.machinegun},{44,jeep,true,true});
        check(bool(t)==(unsigned(normal)+unsigned(mech)+unsigned(mg)+unsigned(jeep)==1),"one local owner, no invented Jeep priority");
        if(t.kind==TargetKind::machinegun) check(t==original,"routing retains complete MG identity");
    }
    check(!m.identify(0xdead),"nonlocal normal getter rejected before dereference");
    // A parent may also contain unrelated vehicle/remote components.
    m.queries.entries[m.queries.count++]={Memory::parent,0x2f,0x7777};
    check(m.identify()==original,"parent Jeep/remote component is not competing local owner");
    for(auto field : std::array<std::pair<uint32_t,uint32_t>,18>{{
        {Memory::component,4},{Memory::component,0x238},{Memory::component,0x118},{Memory::gun,0x38},
        {Memory::component,0x11c},{Memory::ai,0x24},{Memory::component,0xfc},{Memory::component,0x100},
        {Memory::component,0x104},{Memory::ai,0x44},{Memory::component,0x108},{Memory::component,0x110},
        {Memory::component,0x124},{Memory::level,0x17c},{Memory::normal,0xc},{Memory::component,0x120},
        {Memory::player,0x170},{Memory::ai,0x78}}}) {
        Memory changed(image); auto t=changed.identify(); auto v=changed.read(field.first,field.second);
        // Replace with null where a synthetic pointer would otherwise require new memory.
        changed.set(field.first,field.second,0);
        check(changed.identify()!=t,"same-pointer field change cannot preserve full owner");
        changed.set(field.first,field.second,v); check(changed.identify()==t,"exact identity can be selected again");
    }
    for(unsigned index=0;index<3;++index) {
        Memory changed(image); auto t=changed.identify(); changed.queries.entries[index].component+=4;
        check(changed.identify()!=t,"actual component table replacement detected despite unchanged cached pointer");
    }
    Memory static_parent(image); static_parent.queries.entries[0].component=0;
    auto stationary=static_parent.identify(); check(bool(stationary) && !mounted_machinegun(stationary),"static parent still supported");
    static_parent.set(Memory::gun,0x38,0); check(bool(static_parent.identify()),"null parent still supported");
    check(static_parent.identify()!=stationary,"static/null transitions invalidate same component owner");
    std::puts("identity: native query ABI, exact resources, local authority, Car/static/null and same-pointer changes passed");
}
void cleanup(const nova::ElfImage &image) {
    for(unsigned mask=0;mask<8;++mask) {
        Memory m(image); const auto owner=m.identify(); MachineGunActions state{true,true,true,true,true};
        if(mask&1) m.set(Memory::component,0x100,0);
        if(mask&2) { m.set(Memory::component,0x120,0); m.queries.entries[2].component=0; }
        if(mask&4) m.set(Memory::player,0x170,0);
        unsigned resets=0,stops=0,releases=0;
        auto run=[&] { release_owned_machinegun(state,[&] { return m.structure(owner); },[&] { return m.controller(owner); },
            [&] { ++resets; },[&] { return m.weapon_valid(owner); },[&] { ++stops; },
            [&] { return m.manager_valid(owner); },[&] { ++releases; }); };
        run(); run();
        check(resets==unsigned(!(mask&1)) && stops==unsigned(!(mask&2)) && releases==unsigned(!(mask&4)),"independent input/weapon/manager cleanup exactly once");
    }
    for(auto field : {std::pair{Memory::ai,0x24u},std::pair{Memory::component,0xfcu},
                      std::pair{Memory::component,0x108u},std::pair{Memory::component,0x110u},std::pair{Memory::component,0x124u}}) {
        Memory m(image); auto owner=m.identify(); m.set(field.first,field.second,0);
        check(m.identify()!=owner && m.controller(owner),"template/camera-only replacement changes selection but retains valid controller reset");
    }
    {
        Memory m(image); auto owner=m.identify(); const uint32_t new_ai=0x18000;
        m.set(Memory::component,0x11c,new_ai); m.queries.entries[1].component=new_ai;
        m.set(new_ai,4,Memory::gun); m.set(new_ai,0x78,Memory::actor);
        m.set(Memory::component,0x100,new_ai+0x54); m.set(Memory::component,0x104,new_ai+0x58);
        check(m.controller(owner) && m.weapon_valid(owner) && m.manager_valid(owner),"same current local controller survives reserved AI rebind for cleanup");
        m.set(new_ai,0x78,0); check(!m.structure(owner),"unreserved replacement AI is native handoff, no cleanup calls");
    }
    for(unsigned mutation=0;mutation<6;++mutation) {
        Memory m(image); auto owner=m.identify(); MachineGunActions state{true,false,true,true,true};
        unsigned stops=0,releases=0;
        release_owned_machinegun(state,[&] { return m.structure(owner); },[&] { return m.controller(owner); },[&] {
            if(mutation==0) m.set(Memory::component,0xd,0);
            if(mutation==1) m.set(Memory::component,0x22c,1);
            if(mutation==2) m.set(Memory::gun,0x38,0);
            if(mutation==3) { m.set(Memory::component,0x120,0); m.queries.entries[2].component=0; }
            if(mutation==4) m.set(Memory::player,0x170,0);
            if(mutation==5) m.set(Memory::component,0x11c,0);
        },[&] { return m.weapon_valid(owner); },[&] { ++stops; },
        [&] { return m.manager_valid(owner); },[&] { ++releases; });
        check(stops==unsigned(mutation==2 || mutation==4) && releases==unsigned(mutation==2 || mutation==3),"reset callback mutation rechecked before each still-valid release");
    }
    for(bool replace_manager : {false,true}) {
        Memory m(image); auto owner=m.identify(); MachineGunActions state{true,false,true,true,true}; unsigned releases=0;
        release_owned_machinegun(state,[&] { return m.structure(owner); },[&] { return m.controller(owner); },[] {},
            [&] { return m.weapon_valid(owner); },[&] {
                if(replace_manager) m.set(Memory::player,0x170,0); else m.set(Memory::component,0x22c,1);
            },[&] { return m.manager_valid(owner); },[&] { ++releases; });
        check(!releases,"Stop mutation rechecked before manager release");
    }
    for(unsigned completion=0;completion<4;++completion) {
        Memory m(image); auto owner=m.identify(); MachineGunActions state{false,false,true,true,true};
        if(completion&1) observe_owned_machinegun_release(state.weapon_owned,true,[] {},[&] { return m.weapon_valid(owner); });
        if(completion&2) observe_owned_machinegun_release(state.manager_owned,true,[] {},[&] { return m.manager_valid(owner); });
        unsigned stops=0,releases=0;
        release_owned_machinegun(state,[&] { return m.structure(owner); },[&] { return m.controller(owner); },[] {},
            [&] { return m.weapon_valid(owner); },[&] { ++stops; },[&] { return m.manager_valid(owner); },[&] { ++releases; });
        check(stops==unsigned(!(completion&1)) && releases==unsigned(!(completion&2)),"each actual-return ACK and partial completion independently retained");
    }
    {
        Memory m(image); auto owner=m.identify(); m.set(Memory::component,4,0x19000);
        check(!m.structure(owner),"new root actor receives no old controller cleanup");
    }
    bool owned=true;
    observe_owned_machinegun_release(owned,false,[] {},[] { return true; }); check(owned,"wrong receiver/channel cannot ACK");
    observe_owned_machinegun_release(owned,true,[] {},[] { return false; }); check(owned,"changed receiver after return cannot ACK");
    observe_owned_machinegun_release(owned,true,[] {},[] { return true; }); check(!owned,"matching actual return ACK");
    for(auto mode : {0u,1u,2u}) {
        Memory m(image); auto owner=m.identify(); MachineGunActions state{true,true,true,true,true}; unsigned calls=0;
        m.set(Memory::component,mode==0 ? 0xd : mode==1 ? 0x22c : 0x118,mode==1 ? 1u : 0u);
        release_owned_machinegun(state,[&] { return m.structure(owner); },[&] { return m.controller(owner); },[&] { ++calls; },
            [&] { return m.weapon_valid(owner); },[&] { ++calls; },[&] { return m.manager_valid(owner); },[&] { ++calls; });
        check(!calls,"disabled/exit/native handoff receives no stale reset or duplicate stops");
    }
    std::puts("cleanup: independent valid resources, reset/stop mutations, partial return ACKs, native exit/handoff passed");
}
struct Boundary;
Boundary *active=nullptr;
uint32_t update_original=0,input_original=0;
struct Boundary {
    MountedMachineGun model{};
    FrameInput frame;
    ControlsConfig config{};
    bool selected=true,eligible=true,pending=false,rotate=true;
    unsigned prepares=0,cleanups=0;
    uint32_t reset=0;
    Boundary(const nova::ElfImage &image) {
        active=this; model.gun.yaw_limit=45; model.gun.pitch_minimum=-12; model.gun.pitch_maximum=27;
        model.gun.actor_yaw=101; model.gun.actor_pitch=-66;
        model.gun.weapon.allowed[0]=model.gun.weapon.allowed[1]=1;
        reset=nova::abi::require_function(image,"machinegun_reset");
    }
    void counts(float x,float y) { frame={}; frame.dx=x; frame.dy=y; pending=x!=0 || y!=0; }
    void input(uint32_t object,uint32_t dt) {
        mouse_look_boundary(pending,[&] { return selected && eligible && rotate && object==address(model.gun) && !model.gun.exiting &&
            machinegun_look_available(model.duration,model.delay,static_cast<int>(dt)); },[&] {
            auto &g=model.gun; auto delta=machinegun_mouse_delta(frame.dx,frame.dy,g.pitch,g.pitch_minimum,g.pitch_maximum,config);
            queue_mouse_look(*reinterpret_cast<GuestLookInput *>(&g.look),g.yaw,g.pitch,delta); ++prepares;
        },[&] { invoke(input_original,{object,dt}); },[] {});
    }
};
void __cdecl input_hook(uint32_t object,uint32_t dt) { active->input(object,dt); }
void __cdecl update_hook(uint32_t object,uint32_t dt) {
    auto &h=*active;
    machinegun_action_boundary(h.selected,h.eligible,[&] {
        ++h.cleanups; h.pending=false;
        if(!h.model.gun.exiting) invoke(h.reset,{object});
    },[&] { invoke(update_original,{object,dt}); });
}
void phases(const nova::ElfImage &image) {
    Boundary h(image); const auto update=nova::abi::require_function(image,"mounted_update");
    const auto ai=nova::abi::require_function(image,"mounted_ai");
    std::array<uint8_t,7> saved{}; std::memcpy(saved.data(),reinterpret_cast<void *>(update),saved.size());
    {
        nova::abi::GuestHook update_detour(image,"mounted_update",{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&update_hook));
        update_original=update_detour.original();
        nova::abi::GuestHook input_detour(image,"machinegun_consume",{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&input_hook));
        input_original=input_detour.original();
        for(auto counts : {std::pair{40.f,0.f},std::pair{0.f,-60.f},std::pair{25.f,15.f},std::pair{1000.f,1000.f},std::pair{-1000.f,-1000.f}}) {
            auto &s=h.model; s.gun.yaw=s.gun.pitch=0;
            s.translation[0]+=3; s.translation[1]-=2; s.translation[2]+=4;
            s.parent_yaw+=17; s.bank+=9; s.tilt-=3;
            invoke(ai,{address(s)}); // Ordinary group4 AI phase before special local player.
            h.counts(counts.first/2,counts.second/2); h.frame.dx+=counts.first/2; h.frame.dy+=counts.second/2;
            const auto before=h.prepares; auto delta=machinegun_mouse_delta(counts.first,counts.second,0,-12,27,h.config);
            invoke(update,{address(s),16});
            close_to(s.gun.yaw,std::clamp(delta.yaw,-45.f,45.f),"local yaw uses native bound despite moving parent");
            close_to(s.gun.pitch,delta.pitch,"local asymmetric pitch despite bank/tilt");
            close_to(s.node_yaw,0,"input cannot flush separate AI/node phase"); close_to(s.node_pitch,0,"pitch world phase remains native delayed");
            check(h.prepares==before+1 && !h.pending && s.dt==16,"one pair at nested native input hook and actual dt forwarding");
            invoke(update,{address(s),16}); check(h.prepares==before+1,"repeated callback cannot consume mouse twice");
            close_to(s.gun.actor_yaw,101,"actor yaw remains native"); close_to(s.gun.actor_pitch,-66,"actor pitch remains native");
            auto world=s.world_direction[0]; auto saved_input=s.gun.look;
            invoke(ai,{address(s)}); s.parent_yaw+=11; s.bank-=7; s.tilt+=2; s.translation[0]+=5;
            h.counts(0,0); invoke(update,{address(s),23});
            close_to(s.node_yaw,s.gun.yaw,"next native AI phase propagates local angle without host compensation");
            check(std::abs(s.world_direction[0]-world)>.0001f,"no-motion frame still follows native parent motion");
            check(!std::memcmp(&saved_input,&s.gun.look,sizeof(saved_input)),"no-motion preserves native pending fields");
            check(s.bank_phases==s.updates && s.player_phases==s.updates,"native parent/camera/player work always forwarded");
            close_to(s.world_position[0],s.translation[0]+s.world_direction[0],"native moving translation applied after input");
        }
        h.model.gun.pitch=0; h.config.invert_y=true; h.counts(0,40); invoke(update,{address(h.model),7}); close_to(h.model.gun.pitch,4,"invert-Y once");
        for(int delay : {0,16,17}) {
            h.model.duration=1; h.model.delay=delay; h.model.look_yaw=7; h.model.look_pitch=3;
            h.counts(20,10); auto n=h.prepares; invoke(update,{address(h.model),16});
            check(h.prepares==n+unsigned(delay>16),"LookAt guard includes last native interpolation step");
            if(delay<=16) { close_to(h.model.gun.yaw,7,"native target owns yaw"); close_to(h.model.gun.pitch,3,"native target owns pitch"); }
            h.pending=false;
        }
        h.model.duration=0; h.model.gun.exiting=1; h.model.gun.pitch=5;
        h.model.gun.look.pitch_target=0; h.model.gun.look.pitch_delta=-1;
        h.eligible=false; h.counts(20,20); auto resets=h.model.gun.resets;
        invoke(update,{address(h.model),16}); close_to(h.model.gun.look.pitch_target,0,"exit target preserved");
        close_to(h.model.gun.look.pitch_delta,-1,"exit native delta preserved"); close_to(h.model.gun.pitch,4,"native exit phase forwarded");
        check(h.model.gun.resets==resets && !h.pending,"no reset during native exit");
        h.model.gun.exiting=0; h.model.gun.held[0]=1; h.counts(20,20); invoke(update,{address(h.model),16});
        check(!h.model.gun.held[0] && !h.pending,"interrupted same-controller actions reset before original update");
        const auto cleanup=h.cleanups; h.selected=false; invoke(update,{address(h.model),16});
        check(h.cleanups==cleanup,"unselected owner forwards without stale host cleanup");
    }
    check(!std::memcmp(saved.data(),reinterpret_cast<void *>(update),saved.size()),"MG Update seven-byte hook restored");
    bool rejected=false;
    try { nova::abi::GuestHook bad(image,"mounted_update",{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf8},reinterpret_cast<uint32_t>(&update_hook)); }
    catch(const std::runtime_error &) { rejected=true; }
    check(rejected && !std::memcmp(saved.data(),reinterpret_cast<void *>(update),saved.size()),"MG Update mismatch rejects before mutation");
    std::puts("phases: real nested aligned Update/input hooks, moving parent, separate AI phase, once-only pair, native LookAt/exit passed");
}
struct FireFixture;
FireFixture *fire_fixture=nullptr;
uint32_t fire_stop_original=0,fire_release_original=0;
struct FireFixture {
    Memory memory;
    InputTarget owner;
    MountedMachineGun scene{};
    MachineGunActions actions;
    uint32_t event,update,reset,stop,release,stop_site,release_site;
    bool invalidate_after_stop=false,invalidate_after_release=false;
    explicit FireFixture(const nova::ElfImage &image) : memory(image),owner(memory.identify()) {
        fire_fixture=this; scene.gun.yaw_limit=45; scene.gun.pitch_minimum=-12; scene.gun.pitch_maximum=27;
        scene.gun.weapon.allowed[0]=scene.gun.weapon.allowed[1]=1;
        scene.parent_yaw=23; scene.bank=11; scene.tilt=-5; scene.translation[0]=9;
        event=nova::abi::require_function(image,"machinegun_event"); update=nova::abi::require_function(image,"mounted_update");
        reset=nova::abi::require_function(image,"machinegun_reset"); stop=nova::abi::require_function(image,"machinegun_stop");
        release=nova::abi::require_function(image,"machinegun_release");
        stop_site=nova::abi::require_function(image,"machinegun_primary_stop"); release_site=nova::abi::require_function(image,"machinegun_primary_release");
    }
    void input(bool primary,bool secondary,bool secondary_allowed=true) {
        FrameInput frame; frame.held.set(static_cast<size_t>(Action::fire),primary); frame.held.set(static_cast<size_t>(Action::aim),secondary);
        apply_machinegun_actions(frame,true,secondary_allowed,actions,[&] { return memory.identify()==owner; },
            [&](uint32_t type) { invoke(event,{address(scene.gun),type}); });
    }
    void clean() {
        release_owned_machinegun(actions,[&] { return memory.structure(owner); },[&] { return memory.controller(owner); },
            [&] { invoke(reset,{address(scene.gun)}); },[&] { return memory.weapon_valid(owner); },
            [&] { invoke(stop,{address(scene.gun.weapon),0,1}); },[&] { return memory.manager_valid(owner); },
            [&] { invoke(release,{address(scene.gun.manager),0}); });
    }
};
void __cdecl fire_stop_observer(uint32_t receiver,uint32_t channel,uint32_t messages) {
    auto &h=*fire_fixture;
    const bool expected=receiver==address(h.scene.gun.weapon) && channel==0 && h.memory.weapon_valid(h.owner);
    observe_owned_machinegun_release(h.actions.weapon_owned,expected,[&] {
        check(h.actions.weapon_owned || !expected,"ownership remains through actual native call");
        invoke(fire_stop_original,{receiver,channel,messages});
        if(h.invalidate_after_stop) { h.memory.set(Memory::component,0x120,0); h.memory.queries.entries[2].component=0; }
    },[&] { return h.memory.weapon_valid(h.owner); });
}
void __cdecl fire_release_observer(uint32_t receiver,uint32_t channel) {
    auto &h=*fire_fixture;
    const bool expected=receiver==address(h.scene.gun.manager) && channel==0 && h.memory.manager_valid(h.owner);
    observe_owned_machinegun_release(h.actions.manager_owned,expected,[&] {
        invoke(fire_release_original,{receiver,channel});
        if(h.invalidate_after_release) h.memory.set(Memory::player,0x170,0);
    },[&] { return h.memory.manager_valid(h.owner); });
}
void native_actions(const nova::ElfImage &image) {
    fire_stop_original=nova::abi::require_function(image,"machinegun_stop");
    fire_release_original=nova::abi::require_function(image,"machinegun_release");
    const auto stop_owner=nova::abi::require_function(image,"machinegun_primary_stop");
    const auto stop_call=nova::abi::require_function(image,"machinegun_primary_stop_call");
    const auto release_owner=nova::abi::require_function(image,"machinegun_primary_release");
    const auto release_call=nova::abi::require_function(image,"machinegun_primary_release_call");
    std::array<uint8_t,5> saved_stop{},saved_release{};
    std::memcpy(saved_stop.data(),reinterpret_cast<void *>(stop_call),5); std::memcpy(saved_release.data(),reinterpret_cast<void *>(release_call),5);
    {
        nova::abi::GuestCall stop(image,"machinegun_primary_stop",stop_call-stop_owner,"machinegun_stop",reinterpret_cast<uint32_t>(&fire_stop_observer));
        nova::abi::GuestCall release(image,"machinegun_primary_release",release_call-release_owner,"machinegun_release",reinterpret_cast<uint32_t>(&fire_release_observer));
        {
            FireFixture h(image); h.input(true,false); invoke(h.update,{address(h.scene),16});
            Controls controls{ControlsConfig{}}; controls.gameplay=true; controls.overlay(true);
            if(!controls.active()) { h.clean(); h.clean(); }
            check(h.scene.gun.weapon.stops==1 && h.scene.gun.manager.releases==1 && !h.scene.gun.held[0],"overlay executes actual mounted MG native cleanup once");
        }
        for(unsigned completion=0;completion<4;++completion) {
            FireFixture h(image); h.input(true,false); invoke(h.update,{address(h.scene),16});
            check(h.scene.gun.weapon.shots[0]==1 && h.scene.gun.manager.presses==1,"mounted primary uses both native fire receivers after parent work");
            h.input(false,false);
            if(completion==1) invoke(h.stop_site,{address(h.scene.gun.weapon),0,1});
            if(completion==2) invoke(h.update,{address(h.scene),16});
            if(completion==3) invoke(h.reset,{address(h.scene.gun)});
            check(h.actions.weapon_owned==(completion!=1 && completion!=2) && h.actions.manager_owned==(completion!=2),"mounted actual dual ACK independently tracks reset/partial/native completion");
            h.clean(); h.clean(); invoke(h.update,{address(h.scene),16});
            check(h.scene.gun.weapon.stops==1 && h.scene.gun.weapon.messages==1 && h.scene.gun.manager.releases==1,"mounted cleanup invokes each native API once, no duplicate MP-style message");
            check(!h.scene.gun.held[0] && !h.scene.gun.released[0] && !h.scene.gun.weapon.firing && !h.scene.gun.manager.firing,"no stuck native input on resume");
        }
        for(bool allowed : {false,true}) {
            FireFixture h(image); h.input(false,true,allowed); invoke(h.update,{address(h.scene),16});
            h.input(false,true,allowed); invoke(h.update,{address(h.scene),16}); h.input(false,false,allowed); h.clean();
            check(h.scene.gun.weapon.shots[1]==unsigned(allowed) && !h.scene.gun.weapon.stops && !h.scene.gun.manager.releases,"mounted secondary is eligible press edge, no invented held or stop");
        }
        for(bool weapon : {false,true}) {
            FireFixture h(image); h.input(true,false); invoke(h.update,{address(h.scene),16});
            if(weapon) { h.memory.set(Memory::component,0x120,0); h.memory.queries.entries[2].component=0; }
            else h.memory.set(Memory::player,0x170,0);
            h.clean(); h.clean();
            check(h.scene.gun.resets==1 && h.scene.gun.weapon.stops==unsigned(!weapon) && h.scene.gun.manager.releases==unsigned(weapon),"receiver-only replacement retains native input reset and other valid receiver cleanup");
        }
        for(unsigned resource=0;resource<4;++resource) {
            FireFixture h(image); h.input(true,false);
            auto &m=h.memory;
            h.scene.gun.yaw=13; h.scene.gun.pitch=-6; h.scene.duration=37; h.scene.delay=5;
            h.scene.gun.look.yaw_pending=1; h.scene.gun.look.yaw_target=39;
            h.scene.gun.look.pitch_target=20; h.scene.gun.look.pitch_delta=26;
            if(resource==0) {
                const uint32_t ai=0x18000; m.set(Memory::component,0x11c,ai); m.queries.entries[1].component=ai;
                m.set(ai,4,Memory::gun); m.set(ai,0x78,Memory::actor); m.set(ai,0x24,0xc000); m.set(ai,0x44,0xe000);
                m.set(Memory::component,0x100,ai+0x54); m.set(Memory::component,0x104,ai+0x58);
            }
            if(resource==1) m.set(Memory::gun,0x38,0);
            if(resource==2) m.queries.entries[0].component=0x17000;
            if(resource==3) m.set(Memory::component,0xfc,0x18000);
            auto next=m.identify(); check(bool(next) && next!=h.owner,"same-root resource/reparent changes full selection");
            Controls controls{ControlsConfig{}}; controls.gameplay=true;
            SDL_Event e{}; e.type=SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button=SDL_BUTTON_LEFT; controls.handle(e);
            change_input_target(h.owner,next,[&](InputTarget) { h.clean(); },[&] { controls.clear(); });
            check(h.scene.gun.resets==1 && !h.scene.gun.held[0] && !h.scene.gun.look.yaw_pending && !h.scene.gun.look.pitch_delta,
                "current reserved AI rebind/reparent/template transition cancels old native held/pending input");
            close_to(h.scene.gun.yaw,13,"native reset never assigns current AI yaw"); close_to(h.scene.gun.pitch,-6,"native reset never assigns current AI pitch");
            check(h.scene.duration==37 && h.scene.delay==5 && !h.scene.ai_phases && !h.scene.bank_phases,"reset preserves LookAt duration/delay and world phases");
            auto frame=controls.take(); check(!frame.requested(Action::fire) && !frame.dx && !frame.dy,"new context requires fresh PC presses/counts");
            h.scene.duration=0; invoke(h.update,{address(h.scene),16});
            check(!h.scene.gun.weapon.shots[0],"native original consumer cannot replay cancelled old host fire");
            h.input(true,false); invoke(h.update,{address(h.scene),16});
            check(h.scene.gun.weapon.shots[0]==1,"fresh host press works on rebound current owner"); h.clean();
        }
        FireFixture h(image); h.input(true,false); h.input(false,false); h.input(true,false);
        check(!h.scene.gun.released[0],"native repress cancels previous release");
        invoke(h.update,{address(h.scene),16}); h.clean();
        check(h.scene.gun.weapon.stops==1 && h.scene.gun.manager.releases==1,"short/repress native actions keep exactly one cleanup");
        FireFixture changed(image); changed.input(true,false); changed.invalidate_after_stop=true;
        invoke(changed.stop_site,{address(changed.scene.gun.weapon),0,1});
        check(changed.actions.weapon_owned,"native return with replaced weapon cannot ACK old obligation");
        changed.clean(); check(changed.scene.gun.weapon.stops==1 && changed.scene.gun.manager.releases==1,"lost weapon cannot be stopped again, independent manager still releases");
    }
    check(!std::memcmp(saved_stop.data(),reinterpret_cast<void *>(stop_call),5) && !std::memcmp(saved_release.data(),reinterpret_cast<void *>(release_call),5),"mounted release CALL fixtures restored");
    std::puts("native actions: actual aligned release CALLs, primary/secondary, partial ACK, resource replacement and once-only cleanup passed");
}
void drawing() {
    for(bool live : {false,true}) for(bool throws : {false,true}) {
        std::map<uint32_t,uint8_t> visible{{1,1},{2,0},{3,1},{4,1},{5,1},{6,1}};
        unsigned draws=0,input_events=0;
        try {
            draw_mounted_machinegun_scheme(live,[&](uint32_t slot) { return slot==0x28 ? 1u : slot==0x2c ? 2u : 3u; },
                [&](uint32_t p) { return visible[p]; },[&](uint32_t p,bool v) { visible[p]=v; },[&] {
                    ++draws; ++input_events; // FullScreen Draw event11 is forwarded exactly once.
                    check(visible[1]==unsigned(!live) && !visible[2] && visible[3]==unsigned(!live),"exact mounted artwork flags");
                    check(visible[4] && visible[5],"HUD/tutorial/neighbor flags retained");
                    if(throws) throw std::runtime_error("draw exception");
                });
        } catch(const std::runtime_error &e) { check(throws && std::string(e.what())=="draw exception","unexpected draw error"); }
        check(draws==1 && input_events==1 && visible[1] && !visible[2] && visible[3],"native Draw side effects and exact flags restored");
        draw_mounted_machinegun_interface(live,[](uint32_t slot) { check(slot==0x10,"only MG exit artwork selected"); return 6u; },
            [&](uint32_t p) { return visible[p]; },[&](uint32_t p,bool v) { visible[p]=v; },[&] {
                check(visible[6]==unsigned(!live) && visible[4] && visible[5],"exit hidden while HUD/tutorial remain");
            });
        check(visible[6],"exit draw flag restored");
    }
    check(mounted_machinegun_touch(3,1,2,3,4) && !mounted_machinegun_touch(5,1,2,3,4),"direct pure artwork exact identity");
    std::puts("drawing: forwarded scheme/input effects, scoped exact artwork, native HUD/tutorial and flag restoration passed");
}
void real(const char *path) {
    nova::ImportRegistry imports; nova::bionic::register_boot(imports); nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(path,imports); Controls controls{ControlsConfig{}};
    const auto update=nova::abi::require_function(image,"_ZN26CPlayerMachineGunComponent6UpdateEi");
    std::array<uint8_t,7> entry{}; std::memcpy(entry.data(),reinterpret_cast<void *>(update),entry.size());
    check(entry==std::array<uint8_t,7>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},"actual MG Update entry exact bytes");
    { NativeControls native(image,controls); }
    check(!std::memcmp(entry.data(),reinterpret_cast<void *>(update),entry.size()),"actual MG Update restored after complete NativeControls install");
    std::puts("real mounted MG install/remove verified without guest initialization or gameplay");
}
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--real") { if(!std::filesystem::exists(argv[2])) return 77; real(argv[2]); return 0; }
        check(argc==2,"fixture path required"); nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        identity(image); cleanup(image); phases(image); native_actions(image); drawing();
        std::puts("mounted machine-gun production contracts passed"); return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"mounted machine-gun test failed: %s\n",e.what()); return 1; }
}
