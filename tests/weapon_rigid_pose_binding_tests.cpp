#include <cstdlib>
// V01h authored fixture; never built or executed by the source-only author.
// Original owner binding and root-animation/finish ordering.
// Synthetic storage; original Main sampling, typed publication and matrix generation.
// Root traversal and resource-name lookup are explicit fixture seams, not production hooks.
#ifndef NOVA_V01H_RELEASE
#error This opt-in fixture must be built explicitly in Release.
#endif
#include "abi/guest_call.h"
#include "abi/guest_hook.h"
#include "weapon_placement/rigid_draw_topology.h"
#include "weapon_placement/weapon_pose_binding.h"
#include <memory>
#include <fstream>
#include <utility>
#include <exception>
#include <cmath>
#include <initializer_list>
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
static_assert(!nova::weapon_pose::kConsumedWeaponPoseBindingAvailable, "This prerequisite fixture cannot activate placement or reduction");
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
uint32_t __cdecl absent() noexcept { try { return 0; 
    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: absent\n"); std::abort(); }
}
void __cdecl no_context(uint32_t) noexcept { try {
    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: no_context\n"); std::abort(); }
}
void __cdecl no_push(uint32_t,uint32_t) noexcept { try {
    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: no_push\n"); std::abort(); }
}
void __cdecl no_detach(uint32_t) noexcept { try {
    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: no_detach\n"); std::abort(); }
}
uint32_t __cdecl no_clips(uint32_t) noexcept { try { return 0; 
    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: no_clips\n"); std::abort(); }
}
void __cdecl tracks_observer(uint32_t states,uint32_t slot) noexcept { try {
    probe.ok=probe.ok && states==probe.states && slot==0; ++probe.events;

    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: tracks_observer\n"); std::abort(); }
}
void __cdecl speed_observer(uint32_t states,uint32_t factor) noexcept { try {
    probe.ok=probe.ok && states==probe.states && factor==bits(1); ++probe.speed_resets;

    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: speed_observer\n"); std::abort(); }
}
uint32_t __cdecl lookup_observer(uint32_t animation,uint32_t name) noexcept { try {
    probe.ok=probe.ok && animation==probe.animation;
    // Synthetic resource ID lookup only. Original selection, time, speed and
    // loop setters still run; no resource/skin/sight result is represented.
    return *reinterpret_cast<const char*>(name)=='A' ? 68u : 20u;

    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: lookup_observer\n"); std::abort(); }
}
void (*pose_action)() = nullptr;
std::exception_ptr pose_error;
void __cdecl pose_observer(uint32_t,uint32_t first,uint32_t second) noexcept { try {
    probe.ok=probe.ok && first==0 && second==0;
    probe.pose_clip=load(probe.slot); ++probe.poses;
    if (pose_action) pose_action();

    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: pose_observer\n"); std::abort(); }
}
void __cdecl override_listener(uint32_t,uint32_t event,uint32_t payload) noexcept { try {
    probe.ok=probe.ok && event==17 && load(payload)==0;
    if(probe.override_state) {
        // Deliberate adversarial callback: native OnEvent must honor R+1c.
        const uint32_t runtime=load(probe.states,0xf8);
        *reinterpret_cast<uint8_t*>(runtime+0x1c)=1;
    }

    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: override_listener\n"); std::abort(); }
}
struct Native {
    uint32_t update,states,weapon,inventory,jump;
    std::vector<std::unique_ptr<nova::abi::GuestCall>> substitutions;
    std::vector<std::pair<uint32_t,std::array<uint8_t,5>>> original_calls;
    void restore_calls() {
        substitutions.clear();
        for(const auto& item:original_calls) check(std::memcmp(reinterpret_cast<const void*>(item.first),item.second.data(),5)==0,"every fixture CALL restored exactly");
    }
    explicit Native(const nova::ElfImage& image) {
        check(digest(image.source_bytes())=="a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e","ELF identity");
        auto at=[&](uint32_t va,uint32_t size) { const auto p=image.address(va,size);
            check(image.executable(p)&&image.executable(p+size-1),"mapped original range"); return p; };
        update=at(0x286860,2092);
        states=at(0x5d9b80,677); weapon=at(0x1bd920,1689);
        inventory=at(0x263510,60); jump=at(0x8a18c0,18);
        auto sub=[&](const char* owner,uint32_t owner_va,uint32_t site,const char* callee,uint32_t replacement) {
            std::array<uint8_t,5> saved{};const auto call=at(site,5);
            std::memcpy(saved.data(),reinterpret_cast<const void*>(call),5);original_calls.emplace_back(call,saved);
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
        probe={};pose_error=nullptr; probe.animation=ptr(animation); probe.states=ptr(states); probe.root=ptr(root);
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
        word(runtime,0,aim?20:21); word(runtime,8,0); word(runtime,12,114); word(runtime,0x14,repeats);
        word(slot,0,114); word(slot,4,267); word(slot,8,13333); word(slot,12,13600);
        word(slot,0x18,bits(aim?-1.5f:1)); word(slot,0x1c,265); word(slot,0x34,0);
    }
    void tick(uint32_t dt) { run(native.update,{ptr(animation),dt});if(pose_error)std::rethrow_exception(pose_error); }
};

// All following storage is test-owned. The only compiled channel is index0;
// serialized resource record98 is never used as a compiled channel index.
using Q = std::array<uint32_t,4>;
using M = std::array<uint32_t,16>;
Q quaternion(uint32_t p) {Q q{};std::memcpy(q.data(),reinterpret_cast<const void*>(p),16);return q;}
M matrix(uint32_t p) {M m{};std::memcpy(m.data(),reinterpret_cast<const void*>(p),64);return m;}
std::vector<uint8_t> input(const std::filesystem::path& p,const char* hash) {
    std::ifstream s(p,std::ios::binary|std::ios::ate);check(static_cast<bool>(s),"bounded local resource present");
    auto size=s.tellg();check(size>0 && size<=4096,"bounded resource length");s.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(size));s.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));
    check(static_cast<bool>(s) && digest(b)==hash,"resource SHA256");return b;
}
struct PoseNative {
    const nova::ElfImage& image;
    uint32_t set_ctor,factory,bind,compute,publish,sample,absolute,set_clip;
    uint32_t set_table,node_table,root_table,track_table,main_table,k_table,track_singleton;
    uint32_t code(uint32_t a) const {const auto p=image.address(a,1);check(image.executable(p),"original code mapping");return p;}
    explicit PoseNative(const nova::ElfImage& i):image(i) {
        set_ctor=code(0x801270);factory=code(0x8a46d0);bind=code(0xd2690);
        compute=code(0x848700);publish=code(0x81b060);sample=code(0x811110);
        absolute=code(0x9cd4d0);set_clip=code(0x8a1970);
        set_table=i.address(0x11103e8,0x40);node_table=i.address(0x1120b90,0xbc);
        root_table=i.address(0x10fd0f0,0xbc);track_table=i.address(0x1127168,0x50);
        main_table=i.address(0x1100d0c,0xa4);k_table=i.address(0x10f92ec,0x40);
        track_singleton=i.address(0x115e68c,4);
        check(load(main_table,0x44)==code(0x9ceca0) && load(main_table,0x64)==compute,"actual Main getter/compute slots");
        check(load(k_table)==code(0x8aa300) && load(k_table,0x14)==set_clip && load(k_table,0x3c)==code(0x8a1b00),"actual K update/setClip/source identity");
        check(load(track_table,0x20)==code(0x92b910) && load(track_table,0x24)==code(0x9093e0) && load(track_table,0x44)==sample,"actual typed sampler and writers");
        check(load(node_table,0x98)==code(0x9cd0f0) && load(node_table,0xb8)==code(0xa02440),"actual local writer and absolute generator");
        check(load(root_table,0x48)==code(0x9cd4c0),"actual parent absolute getter");
    }
};
struct Segment {
    std::vector<uint8_t> time,values;
    std::array<uint8_t,4+276*8> blocks{};
    std::array<uint32_t,2> interval{};
    Segment(std::vector<uint8_t> t,std::vector<uint8_t> v,uint32_t first,uint32_t last):time(std::move(t)),values(std::move(v)),interval{first,last} {
        check(time.size()%2==0 && values.size()==time.size()*8,"ushort time / float4 value lengths");
        word(blocks,0,276);word(blocks,4+196*8,static_cast<uint32_t>(time.size()));word(blocks,8+196*8,address(time.data()));
        word(blocks,4+197*8,static_cast<uint32_t>(values.size()));word(blocks,8+197*8,address(values.data()));
    }
};
struct Payload {
    std::vector<uint8_t> record,descriptor,channel,uid;
    Segment idle,main,transition;
    uint32_t track=0;
    Payload(const PoseNative& n,const std::filesystem::path& own):
      record(input(own/"right-hand-record.bin","c616226b52cb484619245c91cf1b195c8ae2e4b7467115b866eaf4f384cb5a72")),
      descriptor(input(own/"right-hand-descriptor.bin","e88a9936200e8a869d920cc71b65aa250157003811be9926f0c56340c937a868")),
      channel(input(own/"right-hand-channel.bin","a7338f7c9162a10ebc332b5eec1faa988b79ab9864e57b033d0882ad91d47303")),
      uid(input(own/"right-hand-uid.bin","3f737493889a76460455048a1923a85de71af3ea6ba15429c856eb89fb235210")),
      idle(input(own/"s0-time.bin","854717842232def7928fbad0924fbd509ee0d32d6a832a518e96530d5ce5f4eb"),
           input(own/"s0-value.bin","369777d1ff587ebc0c2fc16398bddd69c8efe0395ec84ad7385ac9c7f27f4dc7"),0,2200),
      main(input(own/"s3-time.bin","1f375222a70270ef0e0209dbddc070c3eb76b1c3d1db2dd245b2f4dc841b0814"),
           input(own/"s3-value.bin","f8218d7fc58ebdf8b9d31af732d1845958497d575b49f6dcf38dec0fedd544f0"),6400,8233),
      transition(input(own/"s6-time.bin","506a72c7df90d261de2baec78a0866201761b89a3fe77632030b3bd20d14b0d9"),
                 input(own/"s6-value.bin","def282583361922054cd87458f78cc14d47ab6484ba12f836812935242800022"),12200,18200) {
        uid.push_back(0);const auto rp=address(record.data());
        auto store=[](uint32_t p,uint32_t off,uint32_t v){std::memcpy(reinterpret_cast<void*>(p+off),&v,4);};
        store(address(channel.data()),0,address(uid.data()));store(rp,8,address(descriptor.data()));store(rp,0x10,address(channel.data()));
        track=run(n.factory,{rp});store(rp,0x14,track);
        check(track==n.track_singleton && load(track)==n.track_table,"original type5 factory installs actual quaternion singleton");
        check(load(address(channel.data()),4)==5,"actual quaternion channel type");
    }
    Segment& for_clip(uint32_t clip) {
        if(clip==20) return idle;if(clip==68 || clip==56) return main;
        check(clip==114,"only exact retained source clips allowed");return transition;
    }
    Q reference(const PoseNative& n,uint32_t clip,uint32_t time_bits) {
        auto& s=for_clip(clip);std::array<uint8_t,16> cache{};
        std::array<uint32_t,3> accessor{address(record.data()),address(s.blocks.data()),address(cache.data())};
        Q out{};uint32_t key=0;
        run(n.sample,{track,address(accessor.data()),time_bits,address(out.data()),address(&key),1});
        check(key< s.time.size()/2,"original reference selector remains in supplied block");return out;
    }
};
struct OwnedNode {
    alignas(16) std::array<uint8_t,0x200> bytes{};
    std::array<uint8_t,32> metadata{};
    OwnedNode(uint32_t table,const char* uid,bool root) {
        word(bytes,0,table);word(bytes,load(table-16)+4,10);
        word(bytes,0xd4,ptr()+0xd4);word(bytes,0xd8,ptr()+0xd4);
        if(root) {word(metadata,0xc,address(uid));word(bytes,0x120,address(metadata.data()));}
        else {word(metadata,0,address(uid));word(bytes,0x134,address(metadata.data()));}
        // Explicit synthetic identity parent and unit scale. These flags select
        // the authenticated copy path; they are not inferred runtime ADS flags.
        word(bytes,0xf4,root?0x80018u:0x40018u);
        for(auto off:{0x10u,0x24u,0x38u,0x4cu,0x50u,0x64u,0x78u,0x8cu,0xacu,0xb0u,0xb4u}) word(bytes,off,bits(1));
        word(bytes,0xa8,bits(1));
    }
    uint32_t ptr() const {return address(bytes.data());}
    void attach(OwnedNode& child) {
        word(bytes,0xd4,child.ptr()+4);word(bytes,0xd8,child.ptr()+4);
        word(child.bytes,4,ptr()+0xd4);word(child.bytes,8,ptr()+0xd4);word(child.bytes,0xd0,ptr());
    }
    Q rotation() const {return quaternion(ptr()+0x9c);}
    M absolute() const {return matrix(ptr()+0x10);}
};
struct Trace {uint32_t selected,controller,time,target,root;Q local;M bone_absolute;};
struct Pipeline {
    const PoseNative& n;Payload& p;Fixture& f;
    OwnedNode root,child,other_root,other_child;
    std::array<uint8_t,0x84> set{};std::array<uint8_t,0x54> cookie{};
    std::array<uint8_t,0x54> main{};std::array<uint8_t,0x30> blender{};
    std::array<uint8_t,20> library{},resource{},selected_list{};
    std::array<uint8_t,0x24> resource_data{},block{},block_owner{};
    std::array<uint8_t,0x40> database{};std::array<uint8_t,115*12> clips{};
    std::array<uint8_t,16> filter{},buffer{};std::array<uint8_t,0x3c> layout{};
    std::array<uint32_t,1> tracks{},channels{},targets{},aux{},cookie_ref{},offset_ref{};
    std::array<uint32_t,3> size_ref{}; // Native vector begin/end/capacity; Bind reads end.
    std::array<uint32_t,2> mapping{},unit{};
    std::array<uint16_t,1> offsets{0},selected{0};std::array<uint8_t,1> sizes{16};
    Q value{};uint32_t key=0,mask=1;std::vector<Trace> trace;
    void (*after_publish)(Pipeline&)=nullptr;
    static Pipeline* active;
    uint32_t ptr(const auto& a) const {return address(a.data());}
    Pipeline(const PoseNative& native,Payload& payload,Fixture& fixture):n(native),p(payload),f(fixture),
      root(n.root_table,"fixture-root-a",true),child(n.node_table,reinterpret_cast<const char*>(p.uid.data()),false),
      other_root(n.root_table,"fixture-root-b",true),other_child(n.node_table,reinterpret_cast<const char*>(p.uid.data()),false) {
        root.attach(child);other_root.attach(other_child);
        run(n.set_ctor,{ptr(set)});check(load(ptr(set))==n.set_table,"actual set C1");word(set,4,10);
        tracks[0]=p.track;channels[0]=address(p.channel.data());
        word(set,0xc,ptr(channels));word(set,0x10,ptr(channels)+4);word(set,0x14,ptr(channels)+4);
        word(set,0x18,ptr(tracks));word(set,0x1c,ptr(tracks)+4);word(set,0x20,ptr(tracks)+4);
        mapping={2,address(p.record.data())};word(set,0x50,ptr(mapping));word(set,0x44,ptr(library));
        word(library,0,ptr(resource));word(resource,0x10,ptr(resource_data));word(resource_data,0x20,ptr(database));
        word(database,0x38,115);word(database,0x3c,ptr(clips));
        auto clip=[&](unsigned id,uint32_t start,uint32_t end){word(clips,id*12+4,start);word(clips,id*12+8,end);};
        clip(20,0,3333);clip(56,6366,7166);clip(68,7766,9899);clip(114,13333,13600);
        word(f.timeline,0,n.k_table);word(f.timeline,0x2c,ptr(database)+0x38);word(f.timeline,0x28,bits(1));
        word(f.animation,0x1c,ptr(set));
        word(main,0,n.main_table);word(main,0xc,f.ptr(f.timeline));word(main,0x1c,1);
        word(main,0x28,ptr(set));word(main,0x2c,address(&key));word(main,0x4c,ptr(block));
        word(block,4,ptr(resource));word(block,0x20,ptr(block_owner));
        size_ref={ptr(sizes),ptr(sizes)+static_cast<uint32_t>(sizes.size()),ptr(sizes)+static_cast<uint32_t>(sizes.size())};
        offset_ref[0]=ptr(offsets);
        word(cookie,0x24,ptr(set));word(cookie,0x3c,ptr(targets));word(cookie,0x48,ptr(aux));
        word(cookie,0x34,ptr(size_ref));word(cookie,0xc,ptr(filter));word(filter,8,address(&mask));
        word(selected_list,8,ptr(selected));word(selected_list,12,ptr(selected)+2);
        word(cookie,0x18,ptr(selected_list));word(cookie,0x1c,ptr(selected_list));word(cookie,0x20,ptr(selected_list));
        cookie_ref[0]=ptr(cookie);word(layout,0x34,ptr(size_ref));word(layout,0x38,ptr(offset_ref));
        word(buffer,4,ptr(layout));word(buffer,8,ptr(value));word(buffer,12,1);unit={ptr(buffer),0};
        word(f.object,0x98,root.ptr());word(f.node,0xd0,root.ptr());
        probe.root=root.ptr();bind(&root);active=this;pose_action=&callback;
    }
    ~Pipeline(){pose_action=nullptr;active=nullptr;}
    void bind(OwnedNode* owner) {
        uint32_t r=owner?owner->ptr():0;run(n.bind,{ptr(cookie),address(&r)});
        if(owner) check(targets[0]==(owner==&root?child.ptr():other_child.ptr()) && aux[0]==0,
                        "original UID Bind publishes the exact owned quaternion target");
    }
    void prime_owned_block() {
        const auto clip=load(f.ptr(f.timeline),0x30);auto& s=p.for_clip(clip);
        // Explicit resource-cache fixture seam: all supplied blocks were hashed.
        // No timeline, source identity, sample, writer or matrix is substituted.
        word(block,0x18,ptr(clips)+clip*12);word(block,0x1c,address(s.interval.data()));
        word(block_owner,0x10,address(s.blocks.data()));key=0;
        check(run(n.code(0x8307f0),{ptr(main),load(f.ptr(f.timeline),4)})==address(s.blocks.data()),"original getAnimationData returns exact owned cache block");
    }
    void publish() {const float weight=1;run(n.publish,{ptr(blender),ptr(buffer),address(&weight),ptr(cookie_ref)});}
    static void callback() {try {check(active!=nullptr,"owned pipeline callback");active->consume();} catch(...) {pose_error=std::current_exception();}}
    void consume() {
        const auto k=f.ptr(f.timeline),clip=load(k,0x30),time=load(k,4);
        check(clip==load(f.ptr(f.slot)),"component and Main share actual selected controller before finish");
        prime_owned_block();value={0x7fc00001,0x7fc00002,0x7fc00003,0x7fc00004};
        const auto expected=p.reference(n,clip,time);const auto before=child.bytes;
        const auto result=run(n.compute,{ptr(main),bits(0),ptr(cookie_ref),ptr(unit)});
        check((result&255u)==0 && load(k,4)==time,"original Main returns AL0 and consumes post-update native time");
        check(value==expected,"original Main dynamic channel mapping and sampler fill exact blending-unit value");
        publish();check(targets[0]==child.ptr() && child.rotation()==value,"original bound typed publication succeeds");
        check((load(child.ptr(),0xf4)&0x140)==0x140,"original writer dirty publication");
        for(size_t i=0;i<before.size();++i) if(!((i>=0x9c&&i<0xac)||(i>=0xf4&&i<0xf8))) check(child.bytes[i]==before[i],"typed writer field fence");
        run(n.absolute,{child.ptr(),0});
        check(child.rotation()==value,"original absolute generation preserves successfully written local rotation");
        check((load(child.ptr(),0xf4)&0xe0)==0,"original generation consumes local dirty bits");
        trace.push_back({probe.pose_clip,clip,time,targets[0],load(child.ptr(),0xd0),value,child.absolute()});
        if(after_publish) after_publish(*this);
    }
};
Pipeline* Pipeline::active=nullptr;


// Concrete retained topology: right hand -> dummy_R -> rifle -> AR -> pivot
// (CSceneNode carriers), then a separate identity CMeshSceneNode child.
// Custom factory31fdd0 + constructNode8edaa7/8edb09 prove that last edge.
// All storage and intrusive links below are owned fixture inputs, not live leases.
struct DrawTrace { Trace source; M model; uint32_t weapon,root,node,mesh; };
struct RigidBranch {
    const Native& native;const PoseNative& n;
    Fixture f;Pipeline p;
    OwnedNode dummy,rifle,ar,pivot,draw;
    std::vector<uint8_t> snodes;
    std::array<uint8_t,0x54> mesh{},replacement_mesh{};
    std::array<uint8_t,0x240> driver{};
    std::array<uint32_t,23> driver_table{};
    std::vector<DrawTrace> draws;
    M physical{};M observed{};unsigned observed_calls=0;
    bool replace_owner=false,replace_mesh=false;
    static RigidBranch* active;
    RigidBranch(const Native& nn,const PoseNative& pn,Payload& payload,const std::filesystem::path& resource):
      native(nn),n(pn),f(nn),p(pn,payload,f),
      dummy(pn.node_table,"dummy_R-node",false),rifle(pn.node_table,"WEAPON_AssaultRifle-node",false),
      ar(pn.node_table,"AR-node",false),pivot(pn.node_table,"AR-node_PIVOT",false),
      draw(pn.image.address(0x1121db0,0xbc),"AR-node_PIVOT",false),
      snodes(input(resource/"branch-snodes.bin","d300af39a367c83b17f2f9a3ff7bd284a2c05e1d45ded0995b06817feabafa35")) {
        check(snodes.size()==5*80,"exact retained right-hand-to-pivot SNode inputs");
        const auto mesh_table=n.image.address(0x110ebe8,0x48);
        check(load(mesh_table,0x44)==n.code(0x80ba90) && load(mesh_table,0x14)==n.code(0x81c600),"actual rigid mesh model and RET4 geometry slots");
        check(load(load(draw.ptr()),0xb8)==n.code(0xa02440) && load(load(draw.ptr()),0x48)==n.code(0x9cd4c0),"actual factory-installed rigid draw-node matrix slots");
        check(load(load(draw.ptr()),0x60)==n.code(0x8a2290),"actual rigid-node UID getter");
        word(draw.bytes,0x104,address(draw.metadata.data()));
        p.child.attach(dummy);dummy.attach(rifle);rifle.attach(ar);ar.attach(pivot);pivot.attach(draw);
        OwnedNode* chain[]{&p.child,&dummy,&rifle,&ar,&pivot};
        for(size_t i=0;i<5;++i) {
            auto& node=*chain[i];const auto s=address(snodes.data()+i*80);
            run(n.code(0x9cd1b0),{node.ptr(),s+0xc});
            run(n.code(0x9cd0f0),{node.ptr(),s+0x18});
            run(n.code(0x9cd040),{node.ptr(),s+0x28});
            // Explicit traversal eligibility fixture precondition read by9cd578;
            // never promoted to a runtime visibility/ordinary-FPS predicate.
            word(node.bytes,0xf4,load(node.ptr(),0xf4)|0x1000u);
        }
        const std::array<uint32_t,3> zero{},one{bits(1),bits(1),bits(1)};
        const Q identity{0,0,0,bits(1)};
        run(n.code(0x9cd1b0),{draw.ptr(),address(zero.data())});
        run(n.code(0x9cd0f0),{draw.ptr(),address(identity.data())});
        run(n.code(0x9cd040),{draw.ptr(),address(one.data())});
        word(draw.bytes,0xf4,load(draw.ptr(),0xf4)|0x1000u);
        check(load(rifle.ptr(),0xb4)==0x3f7fffefu && (load(rifle.ptr(),0xf4)&0x40000u)==0,"retained non-unit Zscale preserved by original setter");
        // A supplied physical ancestor, held fixed. It is deliberately not
        // attenuated, reclassified as a weapon animation, or sampled as Main.
        word(p.root.bytes,0x40,bits(13));word(p.root.bytes,0x44,bits(-7));word(p.root.bytes,0x48,bits(2));
        word(p.root.bytes,0xf4,load(p.root.ptr(),0xf4)&~0x80000u);physical=p.root.absolute();
        word(mesh,0,mesh_table);word(replacement_mesh,0,mesh_table);
        // Empty genuine CMesh row vectors naturally return null from81c600.
        // Full original renderInternal exits before materials/tasks/GPU calls.
        // This is first-model-sink evidence, never a nonempty/admitted draw.
        word(draw.bytes,0x108,address(mesh.data()));word(draw.bytes,0xe8,f.ptr(f.render_manager));
        word(f.render_manager,0x10c,4); // Supplied native weapon-list fixture context.
        word(f.render_manager,0x10,address(driver.data()));word(f.node,0xec,draw.ptr());
        word(f.node,0xd0,p.root.ptr());word(draw.bytes,0xec,draw.ptr());
        driver_table[0x58/4]=reinterpret_cast<uint32_t>(&model_callback);word(driver,0,address(driver_table.data()));
        active=this;p.after_publish=&after;
        check(current_weapon()==f.ptr(f.weapon) && current_root()==p.root.ptr(),"original inventory selects exact supplied owned weapon/root");
    }
    ~RigidBranch(){p.after_publish=nullptr;active=nullptr;}
    uint32_t current_weapon() const {return run(native.inventory,{f.ptr(f.inventory_manager),1});}
    uint32_t current_root() const {return load(load(current_weapon(),4),0x98);}
    static void __cdecl model_callback(uint32_t receiver,uint32_t type,uint32_t value,uint32_t flags) noexcept { try {
        auto& b=*active;++b.observed_calls;b.observed=matrix(value);
        // Only observation callback is synthetic. Native model copy/wrapper,
        // mesh+44, renderInternal and later genuine null-buffer return remain original.
        probe.ok=probe.ok && receiver==address(b.driver.data()) && type==2 && flags==0;
        nova::weapon_placement::RigidDrawFields binding{
            b.f.ptr(b.f.render_manager),b.current_weapon(),b.draw.ptr(),load(b.draw.ptr(),0x108),
            receiver,value,load(b.draw.ptr()),load(load(b.draw.ptr(),0x108))};
        auto read=[](uint32_t p,uint32_t o){return load(p,o);};
        check(nova::weapon_placement::current_rigid_topology(binding,read),"same production topology guard accepts actual original model2 arguments");
        auto wrong=binding;wrong.model=b.p.child.ptr()+0x10;
        check(!nova::weapon_placement::current_rigid_topology(wrong,read),"sibling or hand matrix cannot certify actual rigid draw");
        if(b.replace_owner)word(b.f.inventory_manager,0x160,1);
        if(b.replace_mesh)word(b.draw.bytes,0x108,address(b.replacement_mesh.data()));
        binding.weapon=b.current_weapon();
        if(b.replace_owner||b.replace_mesh)
            check(!nova::weapon_placement::current_rigid_topology(binding,read),"callback replacement invalidates current native branch binding");
    
    } catch(...) { std::fprintf(stderr,"V01h guest callback failure: model_callback\n"); std::abort(); }
}
    static void after(Pipeline& pipeline) {check(active && &active->p==&pipeline,"one owned branch callback");active->generate_and_consume();}
    void generate_and_consume() {
        const auto carrier=rifle.rotation();const auto owner=current_weapon(),owner_root=current_root(),mesh_before=load(draw.ptr(),0x108);
        run(n.absolute,{dummy.ptr(),1});
        check(p.root.absolute()==physical && rifle.rotation()==carrier,"original descendant update preserves physical origin and native rifle local rotation");
        check(draw.absolute()==pivot.absolute(),"factory identity mesh child consumes exact transformed pivot matrix");
        check(draw.absolute()!=p.child.absolute(),"rigid model includes downstream rifle/pivot transforms, not the hand matrix alone");
        const auto before=observed_calls;run(n.code(0x8ddad0),{draw.ptr(),1});
        check(probe.ok && observed_calls==before+1 && observed==draw.absolute(),"full original renderInternal supplies exact generated rigid-node matrix to model2");
        check(matrix(address(driver.data())+0x1ec)==draw.absolute(),"original driver model wrapper stores exact rigid model after callback returns");
        draws.push_back({p.trace.back(),observed,owner,owner_root,draw.ptr(),mesh_before});
    }
    void select(uint32_t clip,float time) {
        word(f.slot,0,clip);probe.pose_clip=clip;
        run(n.set_clip,{f.ptr(f.timeline),clip});run(native.jump,{f.ptr(f.timeline),bits(time)});
    }
};
RigidBranch* RigidBranch::active=nullptr;

void current_rigid_variation(const Native& native,const PoseNative& n,Payload& payload,const std::filesystem::path& resources) {
    RigidBranch b(native,n,payload,resources);b.select(56,6600);b.p.consume();const auto a=b.draws.back();
    b.select(56,7000);b.p.consume();const auto z=b.draws.back();
    check(a.source.local!=z.source.local && a.model!=z.model,"actual right-hand Main variation changes current rigid rifle model, unlike LeftUpperArm sibling");
    check(a.node==z.node && a.mesh==z.mesh && a.root==z.root && z.root==b.p.root.ptr(),"same supplied current branch consumes changing source without rebinding");
    check(z.source.target==b.p.child.ptr() && b.p.root.absolute()==b.physical,"source is actual right hand; physical ancestor remains intact");
}
void selected_ahead_rigid(const Native& native,const PoseNative& n,Payload& payload,const std::filesystem::path& resources,bool aim) {
    RigidBranch b(native,n,payload,resources);b.f.transition(aim);run(n.set_clip,{b.f.ptr(b.f.timeline),114});
    b.f.tick(1);check(probe.ok && probe.events==0 && b.draws.size()==1,"native unfinished transition reaches actual rigid model sink");
    b.f.tick(1);check(probe.ok && probe.events==1 && b.draws.size()==2,"native transition writer/matrix/model precede accepted finish");
    const auto consumed=b.draws.back();const auto successor=aim?68u:20u;
    check(consumed.source.controller==114 && consumed.source.time==bits(aim?13334.0f:13599.0f),"current rigid model belongs to consumed transition source/time");
    check(load(b.f.ptr(b.f.slot))==successor && load(b.f.ptr(b.f.timeline),0x30)==successor,"accepted native selection/controller already advance ahead of consumed rigid matrix");
    check(b.draw.absolute()==consumed.model && matrix(address(b.driver.data())+0x1ec)==consumed.model,"new selection does not replace previous model or restore sight alignment by itself");
    b.f.tick(1);check(probe.ok && b.draws.size()==3 && b.draws.back().source.controller==successor,"next actual publication reaches rigid sink with successor source");
    // Endpoint equality is allowed; source identity must remain explicit.
}

// The reached WeaponRoot non-additive overlap uses actual typed+10 at842964.
// This witness supplies two original Main computations and controlled native
// coefficients; it does NOT claim to reconstruct live Main2/posture selection.
void later_native_contributor(const Native& native,const PoseNative& n,Payload& payload,const std::filesystem::path& resources) {
    RigidBranch b(native,n,payload,resources);b.select(56,6600);b.p.consume();
    const auto first=b.draws.back();const auto first_value=b.p.value;
    auto second_k=b.f.timeline;auto second_main=b.p.main;auto second_buffer=b.p.buffer;
    Q second{};uint32_t second_key=0;
    std::array<uint32_t,2> second_unit{address(second_buffer.data()),0};
    word(second_main,0xc,address(second_k.data()));word(second_main,0x2c,address(&second_key));
    word(second_buffer,8,address(second.data()));
    run(n.set_clip,{address(second_k.data()),56});run(native.jump,{address(second_k.data()),bits(7000)});
    const auto result=run(n.compute,{address(second_main.data()),bits(0),b.p.ptr(b.p.cookie_ref),address(second_unit.data())});
    check((result&255u)==0 && second==payload.reference(n,56,bits(7000)) && second!=first_value,"second actual Main controller yields its own nonzero source sample");
    check(load(b.f.ptr(b.f.timeline),4)==bits(6600) && load(b.f.ptr(b.f.slot))==56,"independent contributor leaves first Main source and selected state unchanged");
    const std::array<Q,2> values{first_value,second};const std::array<float,2> weights{0.5f,0.5f};Q combined{};
    const auto blend=load(n.track_table,0x10);check(blend==n.code(0x92b3d0),"actual typed combiner used by842964");
    run(blend,{payload.track,address(values.data()),address(weights.data()),2,address(combined.data())});
    check(combined!=first_value && combined!=second,"native quaternion combination represents both supplied contributors");
    b.p.value=combined;b.p.publish();run(n.absolute,{b.p.child.ptr(),0});b.generate_and_consume();
    check(b.p.child.rotation()==combined && b.draws.back().model!=first.model,"later native combination reaches current rigid model through original final writer and recursive generation");
    check(b.current_root()==first.root && load(b.f.ptr(b.f.timeline),4)==first.source.time,"same owner and first Main clock cannot certify final composite model provenance");
    // p.trace intentionally still names the first source: the changed model
    // demonstrates why this single-source diagnostic trace is NOT a receipt.
}
void current_owner_callback_loss(const Native& native,const PoseNative& n,Payload& payload,const std::filesystem::path& resources) {
    RigidBranch b(native,n,payload,resources);b.select(56,6600);b.p.consume();const auto prior=b.draws.back();
    b.replace_owner=true;b.replace_mesh=true;b.select(56,7000);b.p.consume();const auto old=b.draws.back();
    check(old.root==prior.root && old.mesh==prior.mesh && old.node==prior.node,"old supplied source reaches model callback before adversarial owner change");
    check(b.current_weapon()==b.f.ptr(b.f.replacement) && b.current_root()!=old.root && load(b.draw.ptr(),0x108)!=old.mesh,"callback invalidates weapon/root/mesh association before renderInternal returns");
    check(old.source.target==b.p.child.ptr() && b.draw.absolute()==old.model,"successful old writer and equal matrix bytes do not grant current owner authority");
    // Current empty geometry prevents a GPU path. Static8ddb28 proves the
    // later +14 receiver is reloaded. No runtime buffer-reload observation is claimed.
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"usage: weapon-rigid-pose-binding-tests <ELF> <V01h resource>");
        if(!std::filesystem::is_regular_file(argv[1])) {std::puts("SKIP: local library absent");return 77;}
        nova::ImportRegistry registry;nova::bionic::register_data(registry);nova::bionic::register_pthread(registry);
        nova::bionic::register_stdlib(registry);nova::bionic::register_string(registry);nova::bionic::CxaRegistry cxa(registry);
        nova::ElfImage image(argv[1],registry);Native native(image);const PoseNative n(image);Payload payload(n,argv[2]);
        current_rigid_variation(native,n,payload,argv[2]);selected_ahead_rigid(native,n,payload,argv[2],false);selected_ahead_rigid(native,n,payload,argv[2],true);
        later_native_contributor(native,n,payload,argv[2]);current_owner_callback_loss(native,n,payload,argv[2]);native.restore_calls();
        std::puts("PASS: original right-hand source/writer -> rigid descendant/model2 on owned storage; no admitted draw, final contributor cohort or live O09/lifetime authority");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
