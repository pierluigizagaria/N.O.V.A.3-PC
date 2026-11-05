// O08f original-function prerequisite. AUTHORED ONLY; never built/run by author.
// Actual captured Walk record75 and native SNode reference remain private in local/.
// Synthetic owned receiver/cookie storage is NOT a live owner or O09 authority.
#ifndef NOVA_O08F_RELEASE
#error This explicit opt-in fixture requires Release.
#endif
#include "abi/guest_call.h"
#include "bionic/imports.h"
#include "loader/elf_image.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
static_assert(sizeof(void*)==4,"32-bit original guest ABI required");
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
uint32_t bits(float f) { uint32_t u; std::memcpy(&u,&f,4); return u; }
uint32_t word(const void* p,size_t off) { uint32_t u; std::memcpy(&u,static_cast<const uint8_t*>(p)+off,4); return u; }
void put(void* p,size_t off,uint32_t u) { std::memcpy(static_cast<uint8_t*>(p)+off,&u,4); }
uint32_t invoke(uint32_t fn,std::initializer_list<uint32_t> args) {
    return nova::abi::call_guest(fn,args.begin(),args.size());
}
using Q=std::array<float,4>;
bool exact(const Q& a,const Q& b) { return std::memcmp(a.data(),b.data(),16)==0; }
double dot(const Q& a,const Q& b) {
    double d=0; for(size_t i=0;i<4;++i) d+=static_cast<double>(a[i])*b[i]; return d;
}
double angle(const Q& a,const Q& b) {
    double d=std::abs(dot(a,b))/std::sqrt(dot(a,a)*dot(b,b));
    if(d>1) d=1;
    return 2*std::acos(d);
}
Q quat(const void* p,size_t off=0) { Q q; std::memcpy(q.data(),static_cast<const uint8_t*>(p)+off,16); return q; }
std::vector<uint8_t> blob(const std::filesystem::path& dir,const char* name,size_t size,const char* expected) {
    auto file=dir/name; check(std::filesystem::file_size(file)==size,"selected private resource size");
    std::ifstream input(file,std::ios::binary); check(input.good(),"selected private resource open");
    std::vector<uint8_t> result(size);
    input.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(size));
    check(input.good() && digest(result)==expected,"selected private resource SHA256"); return result;
}
struct Native {
    const nova::ElfImage& image;
    std::array<uint32_t,1> track{};
    uint32_t table=0,node_table=0,set_table=0;
    explicit Native(const nova::ElfImage& i):image(i) {
        check(digest(i.source_bytes())=="a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e","pinned original ELF");
        table=i.address(0x1127168,0x50); node_table=i.address(0x1120b90,0xa0); set_table=i.address(0x11103e8,0x18);
        track[0]=table;
        for(auto entry : {std::array<uint32_t,2>{0x0c,0x8fa540},{0x10,0x92b3d0},{0x1c,0x909440},
                          {0x20,0x92b910},{0x24,0x9093e0},{0x2c,0x92b750},{0x30,0x8fa580},
                          {0x44,0x811110},{0x48,0x8111c0},{0x4c,0x904350}})
            check(word(reinterpret_cast<const void*>(table),entry[0])==code(entry[1]),"authenticated actual quaternion slot");
        check(word(reinterpret_cast<const void*>(node_table),0x98)==code(0x9cd0f0),"actual node rotation slot");
        check(word(reinterpret_cast<const void*>(set_table),0x14)==code(0x859160),"actual current-set track lookup");
    }
    uint32_t code(uint32_t va) const { auto p=image.address(va,1);check(image.executable(p),"original code address");return p; }
    uint32_t receiver() const { return address(track.data()); }
    uint32_t slot(uint32_t offset) const { return word(reinterpret_cast<const void*>(table),offset); }
    Q identity() const { Q out{};invoke(slot(0x0c),{receiver(),address(out.data())});return out; }
    Q blend(const Q& first,const Q& second,float amount) const {
        check(dot(first,second)>.95,"stay on authenticated native near-reference branch");
        std::array<Q,2> values{first,second};std::array<float,2> weights{1-amount,amount};Q out{};
        invoke(slot(0x10),{receiver(),address(values.data()),address(weights.data()),2,address(out.data())});return out;
    }
    Q add(const Q& first,const Q& second) const {
        std::array<Q,2> values{first,second};const std::array<float,2> weights{1,1};Q out{};
        invoke(slot(0x1c),{receiver(),address(values.data()),address(weights.data()),2,address(out.data())});return out;
    }
};
struct Samples {
    std::vector<uint8_t> record,descriptor,times,values,snode,uid,channel;
    std::array<uint8_t,4+276*8> blocks{};
    std::array<uint32_t,3> accessor{};
    std::array<uint8_t,16> cache{};
    explicit Samples(const std::filesystem::path& dir,const Native& n) {
        record=blob(dir,"record75-sanimation.bin",32,"09f636a6f77f2114023f536c576c9e7b1be25f972b68eff70a78f72978ede593");
        descriptor=blob(dir,"record75-descriptor.bin",32,"3bba42e41b1f189d1ae58eaa76f1cf72c34274f431a795e330b13ade9db27686");
        times=blob(dir,"record75-segment3-time-payload.bin",102,"8d5037a1a9c85031d6d3d3028c39dbdfaa9ae562685b02ab25c58dee37550edf");
        values=blob(dir,"record75-segment3-value-payload.bin",816,"b63d38fcfad30dd496137a3c8887ade625a885190681027ad8eef0d482b66a2b");
        snode=blob(dir,"upperarm-snode.bin",80,"5c1e4672617bb126d688c445ba0816e8060cadc80743b889b725b15504237ed7");
        uid=blob(dir,"upperarm-uid.bin",23,"c59bc2b68b9fe538f40c710e721a617abb0100ba4249a9bd36f28fb66e1438ab");
        channel=blob(dir,"record75-channel.bin",12,"13003ee7a1261fcd0218bb52295ed22b737edff712515cd9e9d2d58d60481118");
        // Explicit relocation/postload boundary, not generated sample content.
        uid.push_back(0);put(snode.data(),0,address(uid.data()));put(channel.data(),0,address(uid.data()));
        put(record.data(),8,address(descriptor.data()));put(record.data(),0x10,address(channel.data()));
        put(record.data(),0x14,n.receiver());
        put(blocks.data(),0,276);
        put(blocks.data(),4+150*8,static_cast<uint32_t>(times.size()));put(blocks.data(),8+150*8,address(times.data()));
        put(blocks.data(),4+151*8,static_cast<uint32_t>(values.size()));put(blocks.data(),8+151*8,address(values.data()));
        accessor={address(record.data()),address(blocks.data()),address(cache.data())};
        check(word(descriptor.data(),4)==3 && word(descriptor.data(),0x18)==151 && word(descriptor.data(),0x1c)==0x100000,
              "actual ushort times / float4 stride16");
        check(std::string(reinterpret_cast<const char*>(uid.data()))=="Bip01_L_UpperArm01-node","exact selected UID");
    }
    uint32_t at() const { return address(accessor.data()); }
    Q bind() const { return quat(snode.data(),24); }
    Q key(const Native& n,uint32_t index) {
        check(index<values.size()/16,"selected key bound");Q out{};
        invoke(n.slot(0x30),{n.receiver(),at(),index,address(out.data())});
        check(exact(out,quat(values.data(),index*16)),"original typed key read is exact captured payload");return out;
    }
    Q sample(const Native& n,float time,uint32_t& selected_key) {
        check(time>=6366 && time<=7166,"actual selected Walk interval");cache.fill(0);selected_key=0;Q out{};
        invoke(n.slot(0x44),{n.receiver(),at(),bits(time),address(out.data()),address(&selected_key),1});return out;
    }
};
struct Node {
    alignas(16) std::array<uint8_t,0x144> data{};
    Node(const Native& n,const Samples& s) {
        put(data.data(),0,n.node_table);put(data.data(),0x134,address(s.snode.data()));
        std::memcpy(data.data()+0x90,s.snode.data()+12,12);
        invoke(n.code(0x9cd0f0),{at(),address(s.snode.data()+24)});
        check(exact(rotation(),s.bind()),"original setter installs the authenticated native bind bytes");
        std::memcpy(data.data()+0xac,s.snode.data()+40,12);
        auto actual=invoke(n.code(0x8a25c0),{at()});
        check(actual==address(s.uid.data()),"original node UID getter binds selected SNode");
    }
    uint32_t at() const { return address(data.data()); }
    Q rotation() const { return quat(data.data(),0x9c); }
    void only_rotation_changed(const std::array<uint8_t,0x144>& before) const {
        for(size_t i=0;i<data.size();++i)
            if(!((i>=0x9c && i<0xac)||(i>=0xf4 && i<0xf8)))
                check(data[i]==before[i],"position/scale/owner and other node fields preserved");
    }
};
struct Consumer {
    std::array<uint8_t,0x84> set{};
    std::array<uint8_t,0x50> cookie{};
    std::array<uint8_t,0x50> blender{};
    std::array<uint32_t,1> tracks{},targets{},aux{};
    uint32_t cookie_pointer=0;
    Consumer(const Native& n,Node& node) {
        // One selected compiled channel at synthetic index0. Resource record75
        // is NOT asserted to be live compiled channel75 (native insertion sorts).
        tracks[0]=n.receiver();put(set.data(),0,n.set_table);put(set.data(),4,1);
        put(set.data(),0x18,address(tracks.data()));put(set.data(),0x1c,address(tracks.data())+4);
        put(cookie.data(),0x24,address(set.data()));put(cookie.data(),0x3c,address(targets.data()));
        put(cookie.data(),0x48,address(aux.data()));cookie_pointer=address(cookie.data());
        put(blender.data(),0,n.image.address(0x11010cc,1));put(blender.data(),0x28,1);
        bind(n,node);
    }
    void bind(const Native& n,Node& node) {
        invoke(n.code(0x8591d0),{address(cookie.data()),0,node.at(),0});
    }
    void apply(const Native& n,const Q& main,const Q& other) {
        check(invoke(n.code(0x859160),{address(set.data()),0})==n.receiver(),"original current-set receiver");
        std::array<Q,2> values{main,other};const std::array<float,2> weights{1,1};
        // Original V01e reached dispatcher -> actual +24 -> quaternion add ->
        // currently bound target +98. No replacement callback or copied model.
        invoke(n.code(0x81aec0),{address(blender.data()),0,address(values.data()),address(weights.data()),2,address(&cookie_pointer)});
        check(word(set.data(),4)==1,"temporary native set retention balanced without destructor");
    }
};
void witness(const Native& n,Samples& s) {
    const auto bind=s.bind(),identity=n.identity();
    check(!exact(bind,identity),"native bind reference is nonidentity; identity is not a valid substitute");
    // Actual frame198 and210 samples; both lie inside6366..7166 at native30Hz.
    const auto raw198=s.key(n,6),raw210=s.key(n,18);
    check(angle(raw198,raw210)>.005 && angle(raw198,bind)>.005,"actual in-Walk nonzero variation and bind difference");
    const auto descriptor_before=s.descriptor,times_before=s.times,values_before=s.values;
    uint32_t key100=0,key0=0;const auto original100=s.sample(n,6600,key100);
    const auto independently_sampled0=s.sample(n,6600,key0);
    check(exact(original100,independently_sampled0) && key100==key0,"zero endpoint still performs identical original sampling/time/key selection");
    check(key100>=5 && key100<=7,"native sampled index remains within declared neighboring Walk keys");
    check(angle(original100,bind)>.005,"original sample is nonzero relative to actual native bind");
    // Test-only candidate endpoints. No production setting/eligibility/hook.
    constexpr unsigned original_default_reset_max=100;
    static_assert(original_default_reset_max==100);
    uint32_t key_later=0;const auto original_later=s.sample(n,7000,key_later);
    check(key_later>=17 && key_later<=19 && angle(original100,original_later)>.005,
          "original time sampler varies within the same Walk interval; quaternion sign alone cannot pass");
    const Q candidate100=original100,candidate0=bind;
    const Q candidate50=n.blend(bind,original100,.5f);
    check(exact(candidate100,original100) && exact(candidate0,bind),"exact original100 and native-bind0 endpoints");
    check(angle(candidate50,bind)<angle(original100,bind) && angle(candidate50,bind)>.001,"native blend gives an actual intermediate reduction");
    Q weight_only{};const float zero_weight=0;
    invoke(n.slot(0x10),{n.receiver(),address(original100.data()),address(&zero_weight),1,address(weight_only.data())});
    check(exact(weight_only,original100) && !exact(weight_only,bind),"single-child weight0 is an ineffective reduction");

    Node target(n,s),replacement(n,s);Consumer consumer(n,target);
    const auto before=target.data;
    // Explicit synthetic unrelated additive term. It is NOT the purported
    // native locomotion payload, a real ADS/recoil trace, or a live pose receipt.
    const Q other{0.f,0.f,.03f,static_cast<float>(std::sqrt(1.0-.03*.03))};
    const auto preserved_other=other;
    consumer.apply(n,candidate100,other);const auto output100=target.rotation();
    const auto expected100=n.add(original100,other);
    check(exact(output100,expected100),"original100 reaches current typed consumer unchanged before native composition");
    consumer.apply(n,candidate0,other);const auto output0=target.rotation();
    check(exact(output0,n.add(bind,other)) && angle(output100,output0)>.005,"real nonzero100-versus-bind0 consumed quaternion witness");
    check(!exact(output0,identity) && !exact(output0,bind),"zero preserves the independent additive term and native bind pose");
    consumer.apply(n,original_later,other);const auto output_later=target.rotation();
    check(exact(output_later,n.add(original_later,other)) && angle(output100,output_later)>.005,
          "actual time-varying Walk contribution reaches the original current consumer");
    consumer.apply(n,bind,other);
    check(exact(target.rotation(),output0),"bind candidate removes this temporal variation with other term unchanged");
    check(exact(other,preserved_other),"independent additive input remains exact");
    target.only_rotation_changed(before);

    const auto old_target=target.data;
    consumer.bind(n,replacement);consumer.apply(n,candidate100,other);
    check(target.data==old_target && exact(replacement.rotation(),expected100),"original dispatcher uses current cookie destination after rebind");
    check(s.descriptor==descriptor_before && s.times==times_before && s.values==values_before,"actual resource inputs unchanged");
    std::printf("Witness: original versus bind reference %.9f radians; consumed100 versus0 %.9f radians\n",
                angle(original100,bind),angle(output100,output0));
    // This does not admit a live weapon, verify ADS/recoil, sample O09, publish
    // render output, or establish that native bind is the accepted gameplay neutral.
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"usage: movement-weapon-locomotion-tests <local ELF> <private captured resource directory>");
        const std::filesystem::path library=argv[1],resource=argv[2];
        if(!std::filesystem::is_regular_file(library)||!std::filesystem::is_regular_file(resource/"record75-sanimation.bin")) {
            std::puts("SKIP: local original library or private selected evidence absent");return 77;
        }
        nova::ImportRegistry registry;nova::bionic::register_data(registry);
        nova::ElfImage image(library,registry);const Native native(image);Samples selected(resource,native);
        witness(native,selected);
        std::puts("PASS: original selected Walk-to-bind quaternion witness on synthetic storage; live activation unproved");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
