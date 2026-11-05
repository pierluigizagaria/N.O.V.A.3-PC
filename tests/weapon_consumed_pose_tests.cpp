// V01f authored fixture; never built or executed by the source-only author.
// Original typed channel initialization, binding, sampling and destination writes.
// Synthetic scene storage; original sampling and output writers are never substituted.
#ifndef NOVA_V01F_RELEASE
#error This opt-in fixture must be built explicitly in Release.
#endif
#include "abi/guest_call.h"
#include "abi/guest_hook.h"
#include <memory>
#include <fstream>
#include "bionic/imports.h"
#include "loader/elf_image.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <initializer_list>
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

constexpr const char* main_apply="_ZN6glitch7collada25CSceneNodeAnimatorBlender20applyAnimationValuesEfRN5boost13intrusive_ptrINS0_20CAnimationTreeCookieEEE";
unsigned early_logs=0;
void __cdecl early_log(uint32_t,const char*,...) { ++early_logs; }
struct Native {
    uint32_t set_ctor=0,track_factory=0,custom_bind=0,sample=0,publish=0,main_apply_fn=0;
    uint32_t root_table=0,node_table=0,set_table=0,track_table=0,expected_track=0;
    uint32_t log_site=0;
    std::array<uint8_t,5> original_log_call{};
    std::unique_ptr<nova::abi::GuestCall> log_substitution;
    explicit Native(const nova::ElfImage& image) {
        check(digest(image.source_bytes())=="a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e","ELF identity");
        auto at=[&](uint32_t va,uint32_t size) { const auto p=image.address(va,size);
            check(image.executable(p)&&image.executable(p+size-1),"mapped original code");return p; };
        set_ctor=at(0x801270,240);track_factory=at(0x8a46d0,1);custom_bind=at(0xd2690,85);
        sample=at(0x811110,167);publish=at(0x81b060,262);main_apply_fn=at(0x833fe0,440);
        root_table=image.address(0x10fd0f0,0xa4);node_table=image.address(0x1120b90,0xa4);
        set_table=image.address(0x11103e8,60);track_table=image.address(0x11273a8,0x50);
        expected_track=image.address(0x115e6a4,4);
        check(load(root_table-16)==0x1c8 && load(node_table-16)==0x13c,"actual node virtual reference bases");
        check(load(root_table,0x60)==at(0x8a23f0,23) && load(node_table,0x60)==at(0x8a25c0,37),"actual UID getters");
        check(load(root_table,0xa0)==at(0x9cd1b0,134) && load(node_table,0xa0)==at(0x9cd1b0,134),"actual position writer");
        check(load(set_table,0x14)==at(0x859160,17),"actual Ex receiver getter");
        check(load(set_table,0x34)==at(0x859100,17) && load(set_table,0x38)==at(0x859120,17),"actual channel UID/type getters");
        check(load(track_table,0x20)==at(0x91afd0,195) && load(track_table,0x24)==at(0x91b3e0,195),"actual typed writers");
        check(load(track_table,0x44)==sample,"actual native sampling entry");
        log_site=image.address(0x834111,5);
        std::memcpy(original_log_call.data(),reinterpret_cast<const void*>(log_site),5);
        log_substitution=std::make_unique<nova::abi::GuestCall>(image,main_apply,0x834111-0x833fe0,
            "_ZN6glitch2os7Printer4logfENS_11E_LOG_LEVELEPKcz",reinterpret_cast<uint32_t>(&early_log));
    }
    void restore_log() {
        log_substitution.reset();
        check(std::memcmp(original_log_call.data(),reinterpret_cast<const void*>(log_site),5)==0,
              "the only substituted CALL is restored exactly");
    }
};
struct Node {
    alignas(16) std::array<uint8_t,0x200> bytes{};
    std::array<uint8_t,32> metadata{};
    uint32_t refs=0;
    Node(uint32_t table,const char* uid,bool root) {
        word(bytes,0,table); refs=load(table-16)+4;word(bytes,refs,10);
        word(bytes,0xd4,ptr()+0xd4);word(bytes,0xd8,ptr()+0xd4);
        if(root) {word(metadata,0xc,address(uid));word(bytes,0x120,address(metadata.data()));}
        else {word(metadata,0,address(uid));word(bytes,0x134,address(metadata.data()));}
    }
    Node(const Node&)=delete;Node& operator=(const Node&)=delete;
    uint32_t ptr() const {return address(bytes.data());}
    void attach(Node& child) {
        // Explicit synthetic topology from original child-list ABI, not a live
        // addChild/destruction/lifetime fixture. Original UID traversal is used.
        word(bytes,0xd4,child.ptr()+4);word(bytes,0xd8,child.ptr()+4);
        word(child.bytes,4,ptr()+0xd4);word(child.bytes,8,ptr()+0xd4);word(child.bytes,0xd0,ptr());
    }
    std::array<uint32_t,3> position() const {return {load(ptr(),0x90),load(ptr(),0x94),load(ptr(),0x98)};}
    void sentinel() {word(bytes,0x90,bits(17));word(bytes,0x94,bits(-9));word(bytes,0x98,bits(4));word(bytes,0xf4,0x18);}
};
struct Fixture {
    const Native& n;
    Node root,child,other_root,other_child;
    std::array<uint8_t,0x84> set{};
    std::array<uint8_t,0x54> cookie{};
    std::array<uint8_t,12> channel{};
    std::array<uint8_t,32> animation{};
    std::array<uint8_t,32> descriptor{};
    std::array<uint8_t,12> channels_size{};
    std::array<uint8_t,20> selected_list{};
    std::array<uint8_t,16> filter{};
    std::array<uint8_t,0x30> blender_base{};
    std::array<uint8_t,0x80> empty_main{};
    std::array<uint32_t,1> channels{},tracks{},targets{},infos{},cookie_ref{};
    std::array<uint16_t,1> selected{0};
    uint32_t mask=1,track=0;
    explicit Fixture(const Native& native):n(native),root(n.root_table,"fixture-root-a",true),
        child(n.node_table,"AR_ammo-node",false),other_root(n.root_table,"fixture-root-b",true),
        other_child(n.node_table,"AR_ammo-node",false) {
        root.attach(child);other_root.attach(other_child);
        word(channel,0,address("AR_ammo-node"));word(channel,4,1);
        word(animation,0x10,address(channel.data()));
        // Actual type1/null encoding factory path initializes its native static
        // singleton. No vtable pointer is fabricated or installed by this test.
        track=run(n.track_factory,{address(animation.data())});
        check(track==n.expected_track && load(track)==n.track_table,"original factory selects and installs actual position receiver");
        run(n.set_ctor,{address(set.data())});check(load(address(set.data()))==n.set_table,"original set C1 installs actual vtable");
        word(set,4,10);channels[0]=address(channel.data());tracks[0]=track;
        word(set,0xc,address(channels.data()));word(set,0x10,address(channels.data())+4);
        word(set,0x14,address(channels.data())+4);word(set,0x18,address(tracks.data()));
        word(set,0x1c,address(tracks.data())+4);word(set,0x20,address(tracks.data())+4);
        // Cookie storage is synthetic; C2+24 publication/owner provenance is
        // static evidence, not constructor/lifecycle execution in this fixture.
        word(cookie,0x24,address(set.data()));word(cookie,0x3c,address(targets.data()));
        word(cookie,0x48,address(infos.data()));word(cookie,0x34,address(channels_size.data()));
        word(channels_size,0,address(selected.data()));word(channels_size,4,address(selected.data())+1);
        word(selected_list,8,address(selected.data()));word(selected_list,12,address(selected.data())+2);
        word(cookie,0x18,address(selected_list.data()));word(cookie,0xc,address(filter.data()));
        word(filter,8,address(&mask));cookie_ref[0]=address(cookie.data());
        // Exact retained record3 descriptor schema; pointers are synthetic.
        const uint32_t fields[]{1,1,1,6,6,3,7,12u<<16};
        std::memcpy(descriptor.data(),fields,sizeof fields);word(animation,8,address(descriptor.data()));
    }
    void bind(Node* owner) {
        uint32_t p=owner?owner->ptr():0;
        run(n.custom_bind,{address(cookie.data()),address(&p)});
    }
    std::array<uint32_t,3> sample(const std::vector<uint8_t>& time,const std::vector<uint8_t>& values,float t) {
        check(!time.empty() && values.size()==time.size()*12,"exact selected block lengths");
        std::array<uint8_t,80> data{};std::array<uint8_t,16> accessor{},cache{};
        word(data,0,8);word(data,4+6*8,static_cast<uint32_t>(time.size()));word(data,8+6*8,address(time.data()));
        word(data,4+7*8,static_cast<uint32_t>(values.size()));word(data,8+7*8,address(values.data()));
        word(accessor,0,address(animation.data()));word(accessor,4,address(data.data()));word(accessor,8,address(cache.data()));
        std::array<uint32_t,3> value{};uint32_t key=0;
        run(n.sample,{track,address(accessor.data()),bits(t),address(value.data()),address(&key),1});
        check(key<time.size(),"original selector key stays in supplied segment");return value;
    }
    void publish(const std::array<uint32_t,3>& value,bool additive=false,float weight=1) {
        std::array<uint8_t,16> buffer{};std::array<uint8_t,0x3c> layout{};
        std::array<uint16_t,1> offsets{0};uint32_t offset_ptr=address(offsets.data());
        word(layout,0x38,address(&offset_ptr));word(buffer,4,address(layout.data()));
        word(buffer,8,address(value.data()));word(buffer,12,1);
        word(blender_base,0x28,additive?1u:0u);
        run(n.publish,{address(blender_base.data()),address(buffer.data()),address(&weight),address(cookie_ref.data())});
    }
};
std::vector<uint8_t> input(const std::filesystem::path& p,const char* expected) {
    std::ifstream stream(p,std::ios::binary|std::ios::ate);check(static_cast<bool>(stream),"local selected fixture data present");
    auto size=stream.tellg();check(size>0 && size<=4096,"bounded fixture input");stream.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));stream.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));
    check(static_cast<bool>(stream) && digest(data)==expected,"selected payload SHA256");return data;
}
void binding_and_publication(const Native& n,const std::array<uint32_t,3>& value) {
    Fixture f(n);f.bind(&f.root);
    check(f.targets[0]==f.child.ptr() && f.infos[0]==0,"original Bind resolves exact child UID and publishes real target");
    check(load(f.root.ptr(),f.root.refs)==10 && load(f.child.ptr(),f.child.refs)==10,"original temporary owner/target references balance");
    f.child.sentinel();auto before=f.child.bytes;f.publish(value);
    check(f.child.position()==value && f.child.bytes!=before,"original typed writer publishes nonzero native vector");
    check((load(f.child.ptr(),0xf4)&0x180)==0x180,"original writer publishes dirty flags");
    for(size_t i=0;i<before.size();++i) if(!(i>=0x90 && i<0x9c) && !(i>=0xf4 && i<0xf8))
        check(before[i]==f.child.bytes[i],"writer preserves other node fields");
    check(load(address(f.set.data()),4)==10,"original receiver getter temporary retain balances");
    word(f.child.bytes,0xf4,0x18);f.publish(value);
    check(f.child.position()==value && (load(f.child.ptr(),0xf4)&0x180)==0x180,
          "original publication can be idempotent in value; value change is not a pose receipt");
    f.child.sentinel();f.publish(value,true,0);
    check(f.child.position()==value,"original one-contributor added writer copies input despite weight0");
    f.child.sentinel();before=f.child.bytes;f.mask=0;f.publish(value);
    check(f.child.bytes==before,"original filter excludes actual target without publication");
    f.mask=1;f.targets[0]=0;f.publish(value);check(f.child.bytes==before,"original null target skips publication");
    f.bind(&f.root);word(f.selected_list,12,load(address(f.selected_list.data()),8));f.publish(value);
    check(f.child.bytes==before,"original empty selected list publishes nothing");
    word(f.selected_list,12,address(f.selected.data())+2);
    const auto logs=early_logs;run(n.main_apply_fn,{address(f.empty_main.data()),bits(0),address(f.cookie_ref.data())});
    check(early_logs==logs+1 && f.child.bytes==before,"original MainRoot early return is not a destination publication");
    f.bind(&f.other_root);check(f.targets[0]==f.other_child.ptr(),"original rebind same UID changes actual instance");
    f.other_child.sentinel();f.publish(value);
    check(f.other_child.position()==value && f.child.bytes==before,"publication follows rebound root only");
    f.bind(nullptr);check(f.targets[0]==f.other_child.ptr(),"native null Bind preserves stale raw target; it is not a new owner permit");
    check(load(f.other_child.ptr(),0xd0)!=f.root.ptr(),"same UID cannot identify old selected root");
}

std::array<uint32_t,3> sample_contract(const Native& n,const std::filesystem::path& dir) {
    Fixture f(n);
    const auto t0=input(dir/"s0-time.bin","deec6e140d80ed38e9d322ee06654112251bd1218af56e7a8466a1e33e835023");
    const auto v0=input(dir/"s0-value.bin","c7ed478ab90016a747f68f48a4e246ba227bbb0e4de0d1dad649f5c723ea579c");
    const auto t3=input(dir/"s3-time.bin","c00e7f889cfc9216ec818bf2e1682fc6af0d89939c91776669478caf27c9727c");
    const auto v3=input(dir/"s3-value.bin","da15af062859b07fa26e58e4921469572671ad3a86f6a88b953e3b500b0eb43a");
    const auto t6=input(dir/"s6-time.bin","c00e7f889cfc9216ec818bf2e1682fc6af0d89939c91776669478caf27c9727c");
    const auto v6=input(dir/"s6-value.bin","da15af062859b07fa26e58e4921469572671ad3a86f6a88b953e3b500b0eb43a");
    const auto idle=f.sample(t0,v0,0);
    const auto aimed=f.sample(t3,v3,7766);
    const auto entry=f.sample(t6,v6,13333);
    const auto exit=f.sample(t6,v6,13599);
    std::array<uint32_t,3> authentic{};std::memcpy(authentic.data(),v0.data(),12);
    check(idle==authentic && aimed==authentic && entry==authentic && exit==authentic,"actual selected ADS/Idle position samples are identical, not a phase certificate");
    check(idle!=std::array<uint32_t,3>{},"actual selected constant sample is nonzero");
    const auto snode=input(dir/"AR_ammo-node.snode.bin","a321b17b42fbb95dc72c99d51c441938293a240e658788d0aef0967844c527e4");
    check(snode.size()==80,"exact reached SNode record length");
    std::array<uint32_t,3> bind_position{};std::memcpy(bind_position.data(),snode.data()+12,12);
    check(bind_position==authentic,"actual mesh bind position equals this constant channel; not a sight-alignment certificate");
    // Both transition endpoints and both accepted successor clip cohorts go
    // through the original publication chain, not just through a sample observer.
    // V01e proves state/pose ordering separately; these explicit cohorts do not
    // claim to execute acceptance, aggregate the whole skeleton or certify ADS.
    f.bind(&f.root);
    for(const auto& sampled : {entry,aimed,exit,idle}) {
        f.child.sentinel();const auto before=f.child.position();f.publish(sampled);
        check(f.child.position()==sampled && f.child.position()!=before,"original sampled endpoint/successor reaches bound destination");
    }
    return idle;
}


}
int main(int argc,char** argv) {
    try {
        check(argc==3,"usage: weapon-consumed-pose-tests <local ELF> <ignored local selected data directory>");
        if(!std::filesystem::is_regular_file(argv[1])) {std::puts("SKIP: local library absent");return 77;}
        nova::ImportRegistry registry; nova::bionic::register_data(registry);nova::bionic::register_pthread(registry);
        nova::bionic::register_stdlib(registry);nova::bionic::register_string(registry);nova::bionic::CxaRegistry cxa(registry);
        nova::ElfImage image(argv[1],registry);Native native(image);
        const auto value=sample_contract(native,std::filesystem::path(argv[2]));binding_and_publication(native,value);native.restore_log();
        std::puts("PASS: ORIGINAL typed receiver/bind/sample/writer on synthetic scene storage; no live ADS/XY acceptance");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
