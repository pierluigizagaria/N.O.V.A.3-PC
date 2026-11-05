// V01e authored fixture; never built or executed by the source-only author.
// Original owner binding and root-animation/finish ordering.
// Synthetic storage; explicit observers replace resources, profiling and pose output.
#ifndef NOVA_V01E_RELEASE
#error This opt-in fixture must be built explicitly in Release.
#endif
#include "abi/guest_call.h"
#include "abi/guest_hook.h"
#include <memory>
#include "bionic/imports.h"
#include "loader/elf_image.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
static_assert(sizeof(void*) == 4, "This fixture requires the 32-bit guest ABI");
void check(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> object;
    Sha256() {
        check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA provider");
        try {
            ULONG size = 0, returned = 0;
            check(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                                   sizeof size, &returned, 0) >= 0, "SHA object size");
            object.resize(size);
            check(BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) >= 0, "SHA create");
        } catch (...) { BCryptCloseAlgorithmProvider(algorithm, 0); algorithm = nullptr; throw; }
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};
std::string digest(std::span<const uint8_t> input) {
    check(input.size() <= UINT32_MAX, "SHA input bound");
    Sha256 h;
    check(BCryptHashData(h.hash, const_cast<PUCHAR>(input.data()), static_cast<ULONG>(input.size()), 0) >= 0, "SHA input");
    std::array<uint8_t, 32> out{};
    check(BCryptFinishHash(h.hash, out.data(), static_cast<ULONG>(out.size()), 0) >= 0, "SHA finish");
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (auto value : out) { text += digits[value >> 4]; text += digits[value & 15]; }
    return text;
}
uint32_t address(const void* p) { return reinterpret_cast<uint32_t>(p); }
template<size_t N> void word(std::array<uint8_t, N>& bytes, size_t offset, uint32_t value) {
    check(offset + sizeof value <= N, "synthetic record bound");
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

uint32_t load(uint32_t p, uint32_t offset = 0) {
    uint32_t value; std::memcpy(&value, reinterpret_cast<const void*>(p + offset), 4); return value;
}

uint32_t bits(float f) { uint32_t v; std::memcpy(&v,&f,4); return v; }
uint32_t run(uint32_t fn,std::initializer_list<uint32_t> args) {
    return nova::abi::call_guest(fn,args.begin(),static_cast<uint32_t>(args.size()));
}
constexpr const char* update_name="_ZN19CAnimationComponent6UpdateEi";
constexpr const char* activate_name="_ZN19CAnimationComponent14OnObjActivate2Ev";
constexpr const char* states_event="_ZN19CStatesSetComponent7OnEventEiPKv";
constexpr const char* weapon_event="_ZN16CWeaponComponent7OnEventEiPKv";
constexpr const char* successor_name="_ZN19CStatesSetComponent18SetStateOnSlotTestEiiii.clone.2";
constexpr const char* play_name="_ZN19CAnimationComponent25PlayAnimationOnControllerEiij";
constexpr const char* profiler="_ZN3glf16SingletonWithDepINS_8debugger8ProfilerENS1_8DebuggerEE11GetInstanceEv";
constexpr const char* monitor="_ZN3glf16SingletonWithDepINS_8debugger13MemoryMonitorENS1_8DebuggerEE11GetInstanceEv";
struct Probe {
    uint32_t animation=0,states=0,root=0,slot=0,cookie=0,pose_clip=0;
    unsigned poses=0,events=0,speed_resets=0,cookie_binds=0,root_binds=0;
    bool ok=true,override_state=false;
} probe;
uint32_t __cdecl absent() { return 0; }
void __cdecl no_context(uint32_t) {}
void __cdecl no_push(uint32_t,uint32_t) {}
void __cdecl no_detach(uint32_t) {}
uint32_t __cdecl no_clips(uint32_t) { return 0; }
int __cdecl compare_bytes(const void* a,const void* b,size_t n) { return std::memcmp(a,b,n); }
void __cdecl tracks_observer(uint32_t states,uint32_t slot) {
    probe.ok=probe.ok && states==probe.states && slot==0; ++probe.events;
}
void __cdecl speed_observer(uint32_t states,uint32_t factor) {
    probe.ok=probe.ok && states==probe.states && factor==bits(1); ++probe.speed_resets;
}
uint32_t __cdecl lookup_observer(uint32_t animation,uint32_t name) {
    probe.ok=probe.ok && animation==probe.animation;
    // Synthetic resource ID lookup only. Original selection, time, speed and
    // loop setters still run; no resource/skin/sight result is represented.
    return *reinterpret_cast<const char*>(name)=='A' ? 8u : 7u;
}
void __cdecl cookie_observer(uint32_t cookie,uint32_t root_ref) {
    probe.ok=probe.ok && cookie==probe.cookie && load(root_ref)==probe.root; ++probe.cookie_binds;
}
void __cdecl root_bind_observer(uint32_t,uint32_t root,uint32_t cookie_ref) {
    probe.ok=probe.ok && root==probe.root && load(cookie_ref)==probe.cookie; ++probe.root_binds;
}
void __cdecl pose_observer(uint32_t,uint32_t first,uint32_t second) {
    probe.ok=probe.ok && first==0 && second==0;
    probe.pose_clip=load(probe.slot); ++probe.poses;
}
void __cdecl override_listener(uint32_t,uint32_t event,uint32_t payload) {
    probe.ok=probe.ok && event==17 && load(payload)==0;
    if(probe.override_state) {
        // Deliberate adversarial callback: native OnEvent must honor R+1c.
        const uint32_t runtime=load(probe.states,0xf8);
        *reinterpret_cast<uint8_t*>(runtime+0x1c)=1;
    }
}
struct Native {
    uint32_t update,activate,states,weapon,parent,visible,inventory,jump;
    std::vector<std::unique_ptr<nova::abi::GuestCall>> substitutions;
    explicit Native(const nova::ElfImage& image) {
        check(digest(image.source_bytes())=="a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e","ELF identity");
        auto at=[&](uint32_t va,uint32_t size) { const auto p=image.address(va,size);
            check(image.executable(p)&&image.executable(p+size-1),"mapped original range"); return p; };
        update=at(0x286860,2092); activate=at(0x291df0,586);
        states=at(0x5d9b80,677); weapon=at(0x1bd920,1689);
        parent=at(0xa262e0,105); visible=at(0x9ccf60,23);
        inventory=at(0x263510,60); jump=at(0x8a18c0,18);
        auto sub=[&](const char* owner,uint32_t owner_va,uint32_t site,const char* callee,uint32_t replacement) {
            substitutions.push_back(std::make_unique<nova::abi::GuestCall>(image,owner,site-owner_va,callee,replacement));
        };
        const auto zero=reinterpret_cast<uint32_t>(&absent);
        for(const auto site:{0x28687au,0x286893u,0x286b0du,0x286b16u}) sub(update_name,0x286860,site,profiler,zero);
        for(const auto site:{0x2868a7u,0x286b00u}) sub(update_name,0x286860,site,monitor,zero);
        sub(update_name,0x286860,0x2868b9,"_ZN3glf8debugger13MemoryMonitor11PushContextEPKc",reinterpret_cast<uint32_t>(&no_push));
        sub(update_name,0x286860,0x286b08,"_ZN3glf8debugger13MemoryMonitor10PopContextEv",reinterpret_cast<uint32_t>(&no_context));
        for(const auto site:{0x2798a0u,0x279a22u,0x279a44u}) sub(play_name,0x279870,site,monitor,zero);
        sub(play_name,0x279870,0x2798b2,"_ZN3glf8debugger13MemoryMonitor11PushContextEPKc",reinterpret_cast<uint32_t>(&no_push));
        for(const auto site:{0x279a2au,0x279a4cu}) sub(play_name,0x279870,site,"_ZN3glf8debugger13MemoryMonitor10PopContextEv",reinterpret_cast<uint32_t>(&no_context));
        sub(states_event,0x5d9b80,0x5d9bcd,"_ZN19CStatesSetComponent18UpdateTracksEventsEi",reinterpret_cast<uint32_t>(&tracks_observer));
        sub(weapon_event,0x1bd920,0x1bda5f,"_ZN19CStatesSetComponent18SetAnimSpeedFactorEf",reinterpret_cast<uint32_t>(&speed_observer));
        sub(successor_name,0x5d5e50,0x5d6236,"_ZN19CAnimationComponent23GetAnimnationIdFromNameEPKc",reinterpret_cast<uint32_t>(&lookup_observer));
        sub(activate_name,0x291df0,0x291e92,"_ZN25CustomAnimationTreeCookie4BindERKN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE",reinterpret_cast<uint32_t>(&cookie_observer));
    }
};
struct Fixture {
    alignas(16) std::array<uint8_t,0x180> animation{};
    alignas(16) std::array<uint8_t,0x1b0> states{};
    alignas(16) std::array<uint8_t,0x2c0> weapon{},replacement{};
    alignas(16) std::array<uint8_t,0x110> object{},other_object{};
    alignas(16) std::array<uint8_t,0x120> root{},node{},other_root{},next{};
    alignas(16) std::array<uint8_t,0x200> inventory_manager{},render_manager{};
    std::array<uint8_t,48> inventory{};
    std::array<uint8_t,0x38> slot{};
    std::array<uint8_t,32> runtime{};
    std::array<uint8_t,24> catalogue{};
    std::array<uint8_t,22*68> rows{};
    std::array<uint8_t,4*56> variants{};
    std::array<uint8_t,0x4c> timeline{};
    std::array<uint8_t,0x40> animated_template{};
    std::array<uint8_t,4> cookie{};
    std::array<uint32_t,64> node_table{};
    std::array<uint32_t,24> tree_table{},states_table{},weapon_table{},override_table{};
    std::array<uint32_t,9> timeline_table{};
    std::array<uint32_t,1> tree{},listener{};
    std::array<uint8_t,64> idle_text{},aim_idle_text{},transition_text{};
    const Native& native;
    uint32_t ptr(const auto& a) const { return address(a.data()); }
    static void text(std::array<uint8_t,64>& out,const char* s) {
        const auto n=std::strlen(s); check(n<50,"fixture string bound");
        word(out,0,static_cast<uint32_t>(n)); word(out,4,static_cast<uint32_t>(n));
        std::memcpy(out.data()+12,s,n+1);
    }
    void record(unsigned index,unsigned type,bool loop,unsigned successor,unsigned variant,uint32_t name,float speed) {
        const size_t r=index*68,v=variant*56;
        word(rows,r+4,type); rows[r+8]=loop?1:0;
        word(rows,r+0xc,1); word(rows,r+0x10,UINT32_MAX); word(rows,r+0x18,successor);
        word(rows,r+0x2c,ptr(variants)+static_cast<uint32_t>(v));
        word(rows,r+0x30,ptr(variants)+static_cast<uint32_t>(v)+56);
        word(variants,v,name); word(variants,v+4,bits(speed));
    }
    explicit Fixture(const Native& n):native(n) {
        probe={}; probe.animation=ptr(animation); probe.states=ptr(states); probe.root=ptr(root);
        probe.slot=ptr(slot); probe.cookie=ptr(cookie);
        node_table[4+0x74/4]=reinterpret_cast<uint32_t>(&no_detach);
        for(auto* p:{&root,&node,&other_root,&next}) {
            word(*p,0,address(node_table.data()+4)); word(*p,4,10);
            word(*p,0xe8,ptr(render_manager)); word(*p,0xec,ptr(*p)); word(*p,0xf4,0x18);
        }
        word(object,0x98,ptr(root)); word(object,0x100,ptr(animation));
        word(other_object,0x98,ptr(other_root));
        word(weapon,4,ptr(object)); word(weapon,0x10,ptr(states)); weapon[0xd]=1;
        word(weapon,0x68,0); word(weapon,0x6c,1); word(weapon,0xac,20); word(weapon,0xb0,21);
        word(weapon,0xf8,UINT32_MAX); word(replacement,4,ptr(other_object));
        word(inventory_manager,0x148,ptr(inventory)); word(inventory_manager,0x14c,ptr(inventory)+48);
        word(inventory,12,ptr(weapon)); word(inventory,36,ptr(replacement));
        word(render_manager,0xac,ptr(node)); word(render_manager,0xb8,ptr(next));
        tree_table[0x10/4]=reinterpret_cast<uint32_t>(&pose_observer);
        tree_table[0x58/4]=reinterpret_cast<uint32_t>(&root_bind_observer);
        tree[0]=address(tree_table.data());
        states_table[0x20/4]=n.states; weapon_table[0x20/4]=n.weapon;
        word(states,0,address(states_table.data())); states[0xd]=1;
        word(weapon,0,address(weapon_table.data()));
        word(animation,4,ptr(object)); word(animation,0x14,1); word(animation,0x20,ptr(slot));
        word(animation,0x2c,address(tree.data())); word(animation,0x54,ptr(states)); word(animation,0x9c,1);
        word(animation,0x138,ptr(animated_template)); word(animation,0x148,ptr(cookie));
        word(states,4,ptr(object)); word(states,0xac,ptr(weapon)); word(states,0xf4,1);
        word(states,0xf8,ptr(runtime)); word(states,0xfc,1); word(states,0x11c,ptr(catalogue));
        word(states,0x120,ptr(animation)); word(states,0x144,bits(1));
        word(catalogue,8,ptr(rows)); word(catalogue,12,ptr(rows)+static_cast<uint32_t>(rows.size()));
        text(idle_text,"Idle"); text(aim_idle_text,"AimIdle"); text(transition_text,"Transition");
        record(0,3,true,0,0,ptr(idle_text)+12,1); record(1,3,true,0,1,ptr(aim_idle_text)+12,1);
        record(20,10,false,1,2,ptr(transition_text)+12,-1.5f);
        record(21,11,false,0,3,ptr(transition_text)+12,1);
        timeline_table[0x10/4]=n.jump; timeline_table[0x20/4]=reinterpret_cast<uint32_t>(&no_clips);
        word(timeline,0,address(timeline_table.data())); word(slot,0x28,ptr(timeline));
        override_table[0x20/4]=reinterpret_cast<uint32_t>(&override_listener);
        listener[0]=address(override_table.data());
    }
    void transition(bool aim,uint32_t repeats=1) {
        word(runtime,0,aim?20:21); word(runtime,8,0); word(runtime,12,55); word(runtime,0x14,repeats);
        word(slot,0,55); word(slot,4,100); word(slot,8,1000); word(slot,12,1100);
        word(slot,0x18,bits(aim?-1.5f:1)); word(slot,0x1c,98); word(slot,0x34,0);
    }
    void tick(uint32_t dt) { run(native.update,{ptr(animation),dt}); }
};
void binding_contract(const Native& n) {
    Fixture f(n);
    run(n.activate,{f.ptr(f.animation)});
    check(probe.ok && probe.cookie_binds==1 && probe.root_binds==1,"original activation binds exact object root/cookie");
    check(load(f.ptr(f.animation),0x10)==f.ptr(f.root),"original A+10 retained root");
    run(n.parent,{f.ptr(f.node),f.ptr(f.root)}); run(n.parent,{f.ptr(f.next),f.ptr(f.other_root)});
    check(load(f.ptr(f.node),0xd0)==f.ptr(f.root),"original setParent publishes ancestry");
    check(load(f.ptr(f.node),4)==10,"original parent temporary reference balanced");
    check((run(n.visible,{f.ptr(f.node),0})&255u)==1,"original effective visibility returns AL");
    word(f.node,0xf4,8); check((run(n.visible,{f.ptr(f.node),0})&255u)==0,"one visibility bit alone is insufficient");
    const auto current=run(n.inventory,{f.ptr(f.inventory_manager),load(f.ptr(f.inventory_manager),0x160)});
    const auto draw=load(f.ptr(f.render_manager),0xac);
    check(current==f.ptr(f.weapon) && load(draw,0xec)==draw,"actual selected inventory component and current callback receiver");
    check(load(draw,0xd0)==load(load(current,4),0x98),"current draw node belongs to selected weapon root");
    check(load(load(f.ptr(f.render_manager),0xb8),0xd0)!=load(draw,0xd0),"next node is a different owner");
    word(f.inventory_manager,0x160,1);
    const auto replacement=run(n.inventory,{f.ptr(f.inventory_manager),1});
    check(load(draw,0xd0)!=load(load(replacement,4),0x98),"same draw pointer fails replacement-owner binding");
    check(run(n.inventory,{f.ptr(f.inventory_manager),2})==0,"original inventory bounds reject missing index");
}
void completion_order(const Native& n,bool aim) {
    Fixture f(n); f.transition(aim);
    f.tick(1);
    check(probe.ok && probe.poses==1 && probe.events==0 && load(f.ptr(f.runtime))==(aim?20u:21u),"unfinished native clip retains transition");
    f.tick(1);
    check(probe.ok && probe.poses==2 && probe.events==1,"original end dispatch follows one root-animation callback");
    check(load(f.ptr(f.runtime))==(aim?1u:0u) && load(f.ptr(f.slot))==(aim?8u:7u),"original successor clone and original Play accept settled state/clip");
    check(probe.pose_clip==55,"selected settled clip does not identify the root-animation callback already consumed this update");
    check(load(f.ptr(f.timeline),4)==bits(aim?1001.0f:1099.0f),"native nonloop clamp uses length minus one; no invented endpoint");
    check(probe.speed_resets==(aim?0u:1u),"original weapon finish callback reaches native speed reset only for exit-to-Idle here");
    f.tick(1);
    check(probe.ok && probe.poses==3 && probe.events==1 && probe.pose_clip==(aim?8u:7u),"next root-animation callback sees accepted successor");
}
void override_contract(const Native& n) {
    Fixture f(n); f.transition(false);
    // A synthetic listener mutates the native override latch. This tests the
    // original OnEvent guard, not a proposed placement authority or generation.
    std::array<uint8_t,16> enabled_listener{};
    word(enabled_listener,0,address(f.override_table.data())); enabled_listener[0xd]=1;
    word(f.states,0xac,address(enabled_listener.data())); probe.override_state=true;
    f.tick(2);
    check(probe.ok && probe.events==1 && load(f.ptr(f.runtime))==21 && load(f.ptr(f.slot))==55,
          "native event17 override prevents automatic settled successor");
}
void native_event_guards(const Native& n) {
    { Fixture f(n); f.transition(false,2); f.tick(2);
      check(probe.ok && probe.events==1 && load(f.ptr(f.runtime))==21 && load(f.ptr(f.runtime),0x14)==1,
            "remaining native repetition prevents settled successor"); }
    { Fixture f(n); f.transition(false); word(f.slot,0x34,0x1000); f.tick(2);
      check(probe.ok && probe.events==0 && load(f.ptr(f.runtime))==21,
            "original looping controller does not report a nonloop end"); }
    { Fixture f(n); f.transition(false); word(f.rows,0x30,load(f.ptr(f.rows),0x2c)); f.tick(2);
      check(probe.ok && load(f.ptr(f.runtime))==0 && load(f.ptr(f.slot))==55,
            "native zero-variant successor changes state without accepting a new clip"); }
}

}
int main(int argc,char** argv) {
    try {
        check(argc==2,"usage: weapon-visible-ads-binding-tests <local ELF path>");
        const std::filesystem::path library=argv[1];
        if(!std::filesystem::is_regular_file(library)) { std::puts("SKIP: local library absent"); return 77; }
        nova::ImportRegistry registry; nova::bionic::register_data(registry);
        registry.function("memcmp",reinterpret_cast<const void*>(&compare_bytes));
        nova::ElfImage image(library,registry); const Native native(image);
        binding_contract(native); completion_order(native,false); completion_order(native,true); override_contract(native); native_event_guards(native);
        std::puts("PASS: original owner/finish-order witnesses with substituted resource and pose observers; no actual ADS alignment or placement acceptance");
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
