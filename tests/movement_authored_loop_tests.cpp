// O08g original-function prerequisite. AUTHORED ONLY; never built/run by author.
// Actual captured Walk record75 and authored loop-start bytes remain private in local/.
// Synthetic owned receiver/cookie storage is NOT a live owner or O09 authority.
#ifndef NOVA_O08G_RELEASE
#error This explicit opt-in fixture requires Release.
#endif
#include "abi/guest_call.h"
#include "movement_wobble/movement_wobble.h"
#include "bionic/imports.h"
#include "bionic/runtime.h"
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
    mutable unsigned blend_calls=0;
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
        ++blend_calls;
        check(dot(first,second)>.95,"stay on authenticated native near-reference branch");
        std::array<Q,2> values{first,second};std::array<float,2> weights{1-amount,amount};Q out{};
        invoke(slot(0x10),{receiver(),address(values.data()),address(weights.data()),2,address(out.data())});return out;
    }
    Q add(const Q& first,const Q& second) const {
        std::array<Q,2> values{first,second};const std::array<float,2> weights{1,1};Q out{};
        invoke(slot(0x1c),{receiver(),address(values.data()),address(weights.data()),2,address(out.data())});return out;
    }
};

// Data-only fixture ownership. No live owner, O09 capsule or production installer.
struct Cursor {
    std::array<uint8_t,16> cache{};
    uint32_t key=0;
    void prepare() {
        cache.fill(0);put(cache.data(),4,0xff7fffff);cache[13]=1;
    }
};
struct Samples {
    std::vector<uint8_t> record,descriptor,times,values,snode,uid,channel;
    std::array<uint8_t,4+276*8> blocks{};
    unsigned segment=0;float clip_start=0,clip_end=0,segment_start=0,segment_end=0;
    Samples(const std::filesystem::path& dir,const std::filesystem::path& extra,const Native& n,unsigned selected):segment(selected) {
        check(segment==2 || segment==3 || segment==5 || segment==6,"only the selected Walk/RushWalk segments");
        clip_start=segment<5?6366.f:12166.f;clip_end=segment<5?7166.f:12699.f;
        segment_start=segment==2?4300.f:segment==3?6433.f:segment==5?10433.f:12233.f;
        segment_end=segment==2?6433.f:segment==3?8233.f:segment==5?12233.f:18200.f;
        record=blob(dir,"record75-sanimation.bin",32,"09f636a6f77f2114023f536c576c9e7b1be25f972b68eff70a78f72978ede593");
        descriptor=blob(dir,"record75-descriptor.bin",32,"3bba42e41b1f189d1ae58eaa76f1cf72c34274f431a795e330b13ade9db27686");
        times=segment==2 ? blob(dir,"record75-segment2-time-payload.bin",80,"a4b5141b18785327c003ba91cd2a71aa849a0f0e91e5cc0ea24acd34a0c4d086")
                         : blob(dir,"record75-segment3-time-payload.bin",102,"8d5037a1a9c85031d6d3d3028c39dbdfaa9ae562685b02ab25c58dee37550edf");
        values=segment==2 ? blob(dir,"record75-segment2-value-payload.bin",640,"ebbf36a7beb10e4f0d77ef461f509d35a88a1b63fbf4dd3a0187ce6dba930de9")
                          : blob(dir,"record75-segment3-value-payload.bin",816,"b63d38fcfad30dd496137a3c8887ade625a885190681027ad8eef0d482b66a2b");
        if(segment==5) {
            times=blob(extra,"record75-segment5-time-payload.bin",98,"de5c9e4367c9dd9376ca7f3a393e71db894065f88729e238f17aabffb3a735c3");
            values=blob(extra,"record75-segment5-value-payload.bin",784,"7be33ab93e37b1e6a5c021c3a6411aab0dde70ba72c5fb44326468ce91489a4a");
        } else if(segment==6) {
            times=blob(extra,"record75-segment6-time-payload.bin",76,"506a72c7df90d261de2baec78a0866201761b89a3fe77632030b3bd20d14b0d9");
            values=blob(extra,"record75-segment6-value-payload.bin",608,"68780a383c87eba480151141af535023311410273c13af72ae9c4ecf3f23de83");
        }
        snode=blob(dir,"upperarm-snode.bin",80,"5c1e4672617bb126d688c445ba0816e8060cadc80743b889b725b15504237ed7");
        uid=blob(dir,"upperarm-uid.bin",23,"c59bc2b68b9fe538f40c710e721a617abb0100ba4249a9bd36f28fb66e1438ab");
        channel=blob(dir,"record75-channel.bin",12,"13003ee7a1261fcd0218bb52295ed22b737edff712515cd9e9d2d58d60481118");
        uid.push_back(0);put(snode.data(),0,address(uid.data()));put(channel.data(),0,address(uid.data()));
        put(record.data(),8,address(descriptor.data()));put(record.data(),0x10,address(channel.data()));put(record.data(),0x14,n.receiver());
        put(blocks.data(),0,276);
        put(blocks.data(),4+150*8,static_cast<uint32_t>(times.size()));put(blocks.data(),8+150*8,address(times.data()));
        put(blocks.data(),4+151*8,static_cast<uint32_t>(values.size()));put(blocks.data(),8+151*8,address(values.data()));
        check(word(descriptor.data(),4)==3 && word(descriptor.data(),0x18)==151 && word(descriptor.data(),0x1c)==0x100000,"actual ushort time / float4 stride16");
        check(std::string(reinterpret_cast<const char*>(uid.data()))=="Bip01_L_UpperArm01-node","actual selected UID");
    }
    Q bind() const { return quat(snode.data(),24); } // Node initialization ONLY.
    Q sample(const Native& n,float time,Cursor& cursor,uint32_t native_blocks) const {
        check(time>=clip_start && time<clip_end,"proved selected loop interval, end exclusive");
        check(time>=segment_start && time<=segment_end,"authentic retained segment covers requested time");
        check(native_blocks==address(blocks.data()),"native data lookup returned the selected captured block vector");
        cursor.prepare();
        const std::array<uint32_t,3> accessor{address(record.data()),native_blocks,address(cursor.cache.data())};Q out{};
        invoke(n.slot(0x44),{n.receiver(),address(accessor.data()),bits(time),address(out.data()),address(&cursor.key),1});return out;
    }
};
struct Catalog {
    std::array<uint8_t,20> library{};
    std::array<uint8_t,0x14> file{};
    std::array<uint8_t,0x24> loaded{};
    std::array<uint8_t,0x40> header{};
    std::array<uint8_t,102*12> clips{};
    bool rush=false;uint32_t index=56,start=6366,end=7166,length=800;
    Catalog(const std::filesystem::path& dir,bool is_rush):rush(is_rush) {
        if(rush) { index=101;start=12166;end=12699;length=533; }
        const auto clip=rush?blob(dir,"rush-clip101.bin",12,"779f4eaf768820c4efaea9d1850e12a82b083a26bb8e1eb351e719b6b76fa1ca")
                            :blob(dir,"walk-clip56.bin",12,"71dc0c75b5904a2c88fea32824cb733324ac477520c843563872965c8ce676b9");
        check(word(clip.data(),4)==start && word(clip.data(),8)==end,"captured native loop bounds");
        std::memcpy(clips.data()+index*12,clip.data(),12);
        // Only the selected captured clip is populated. Names are never dereferenced.
        put(library.data(),0,address(file.data()));put(file.data(),0x10,address(loaded.data()));
        put(loaded.data(),0x20,address(header.data()));put(header.data(),0x38,102);put(header.data(),0x3c,address(clips.data()));
    }
    uint32_t clip() const { return address(clips.data()+index*12); }
};
// Original controller calls this explicit HOST OBSERVER in the adversarial
// callback case. It records intervals only; it is not a game event receiver,
// a replacement CALL, an O09 permit, or an assertion of real audio/script work.
struct EventLog {
    uint32_t vptr=0,count=0;
    std::array<std::array<uint32_t,4>,32> intervals{};
    bool overflow=false;
};
void __cdecl observe_interval(EventLog* log,float from,float to,float start,float end) {
    if(log->count>=log->intervals.size()) { log->overflow=true;return; }
    log->intervals[log->count++]={bits(from),bits(to),bits(start),bits(end)};
}
struct Controller {
    std::array<uint8_t,0x148> component{};
    std::array<uint8_t,0x38> slot{};
    std::array<uint8_t,0x4c> timeline{};
    std::array<uint32_t,5> observer_table{};
    EventLog events{};
    Controller(const Native& n,const Catalog& catalog,bool looping,bool callbacks) {
        const auto table=n.image.address(0x10f92ec,0x40);
        check(word(reinterpret_cast<const void*>(table),0x10)==n.code(0x8a18c0),"actual jumpTo slot");
        check(word(reinterpret_cast<const void*>(table),0x3c)==n.code(0x8a1b00),"actual clip-index slot");
        put(timeline.data(),0,table);put(timeline.data(),0x30,catalog.index);
        put(component.data(),0x14,1);put(component.data(),0x20,address(slot.data()));
        put(slot.data(),0,catalog.index);put(slot.data(),4,catalog.length);put(slot.data(),8,word(reinterpret_cast<const void*>(catalog.clip()),4));
        put(slot.data(),0xc,word(reinterpret_cast<const void*>(catalog.clip()),8));put(slot.data(),0x18,bits(1.f));
        put(slot.data(),0x28,address(timeline.data()));
        observer_table[4]=reinterpret_cast<uint32_t>(&observe_interval);events.vptr=address(observer_table.data());
        if(callbacks) put(slot.data(),0x2c,address(&events));
        invoke(n.code(0x26ce30),{address(component.data()),0,looping?1u:0u});
        check(bool(word(slot.data(),0x34)&0x1000)==looping,"original loop setter, not clip-name inference");
        invoke(n.code(0x8a18c0),{address(timeline.data()),bits(static_cast<float>(catalog.start))});
    }
    uint32_t advance(const Native& n,uint32_t dt) {
        return invoke(n.code(0x2736c0),{address(component.data()),0,dt});
    }
    float time() const { float out;auto u=word(timeline.data(),4);std::memcpy(&out,&u,4);return out; }
    std::array<uint32_t,8> logical() const {
        return {word(slot.data(),0),word(slot.data(),4),word(slot.data(),0x18),word(slot.data(),0x1c),
                word(slot.data(),0x20),word(slot.data(),0x34),word(timeline.data(),4),word(timeline.data(),0x34)};
    }
};
// Original8307f0 -> original8305d0 cache-hit path. The fixture supplies already
// owned blocks; it neither executes streaming IO nor proves production grab/drop.
// Separate J+4c fields are essential: a private key cache alone is insufficient.
struct DataLookup {
    std::array<uint8_t,88> animator{};
    std::array<uint8_t,0x84> set{};
    std::array<std::array<uint8_t,0x24>,2> block{};
    std::array<std::array<uint8_t,0x14>,2> loaded{};
    std::array<std::array<uint32_t,2>,2> ranges{};
    DataLookup(const Native& n,const Catalog& catalog,Controller& controller,const Samples& s2,const Samples& s3) {
        ranges={{{static_cast<uint32_t>(s2.segment_start),static_cast<uint32_t>(s2.segment_end)},
                 {static_cast<uint32_t>(s3.segment_start),static_cast<uint32_t>(s3.segment_end)}}};
        const auto table=n.image.address(0x1100d0c,0x48);
        check(word(reinterpret_cast<const void*>(table),0x44)==n.code(0x9ceca0),"actual Main timeline accessor");
        put(animator.data(),0,table);put(animator.data(),0xc,address(controller.timeline.data()));
        put(animator.data(),0x28,address(set.data()));put(set.data(),0x44,address(catalog.library.data()));
        for(size_t i=0;i<2;++i) {
            put(block[i].data(),4,word(catalog.library.data(),0));put(block[i].data(),0x18,catalog.clip());
            put(block[i].data(),0x1c,address(ranges[i].data()));put(block[i].data(),0x20,address(loaded[i].data()));
            put(loaded[i].data(),0x10,address((i==0?s2:s3).blocks.data()));
        }
        select(0);
    }
    void select(size_t i) { check(i<2,"selected retained block index");put(animator.data(),0x4c,address(block[i].data())); }
    uint32_t data(const Native& n,float time) {
        const auto retained=word(animator.data(),0x4c);
        const auto* b=reinterpret_cast<const uint8_t*>(retained);
        const auto* range=reinterpret_cast<const uint32_t*>(word(b,0x1c));
        check(time>=static_cast<float>(range[0]) && time<=static_cast<float>(range[1]),"cache-hit-only precondition excludes streaming miss/IO");
        const auto result=invoke(n.code(0x8307f0),{address(animator.data()),bits(time)});
        check(word(animator.data(),0x4c)==retained,"original cache-hit lookup retains its own lease slot");return result;
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
    std::array<uint8_t,0x50> cookie{},blender{};
    std::array<uint8_t,0x10> channel_list{};
    std::array<uint8_t,0x0c> mask{};
    std::array<uint32_t,1> tracks{},targets{},aux{};
    uint16_t selected_channel=0,channel_offset=0;
    uint32_t offset_pointer=0,mask_bits=1,cookie_pointer=0;
    Consumer(const Native& n,Node& node) {
        // One selected compiled channel at synthetic index0, not resource index75.
        tracks[0]=n.receiver();put(set.data(),0,n.set_table);put(set.data(),4,1);
        put(set.data(),0x18,address(tracks.data()));put(set.data(),0x1c,address(tracks.data())+4);
        offset_pointer=address(&channel_offset);put(set.data(),0x38,address(&offset_pointer));
        put(channel_list.data(),8,address(&selected_channel));put(channel_list.data(),0xc,address(&selected_channel)+2);
        put(cookie.data(),0x18,address(channel_list.data()));put(cookie.data(),0x24,address(set.data()));
        put(cookie.data(),0x3c,address(targets.data()));put(cookie.data(),0x48,address(aux.data()));
        put(mask.data(),8,address(&mask_bits));put(cookie.data(),0xc,address(mask.data()));
        cookie_pointer=address(cookie.data());put(blender.data(),0,n.image.address(0x11010cc,1));put(blender.data(),0x28,1);
        bind(n,node.at());
    }
    void bind(const Native& n,uint32_t node) { invoke(n.code(0x8591d0),{address(cookie.data()),0,node,0}); }
    void apply(const Native& n,const Q& main,const Q& other) {
        std::array<Q,2> values{main,other};const std::array<float,2> weights{1,1};
        std::array<uint32_t,4> buffer{0,address(set.data()),address(values.data()),2};
        const auto before_values=values;const auto before_weights=weights;
        // Original selected-list/filter/null-target wrapper ->81aec0 -> actual
        // typed+24 -> current target+98. No game CALL/vtable replacement.
        invoke(n.code(0x81b060),{address(blender.data()),address(buffer.data()),address(weights.data()),address(&cookie_pointer)});
        check(word(set.data(),4)==1,"native temporary set reference remains balanced");
        check(std::memcmp(values.data(),before_values.data(),sizeof values)==0 && weights==before_weights,
              "original composition inputs and native weights preserved");
    }
};
// Test-local kernel proposal ONLY. The pointer means data availability, never
// ordinary-FPS authority. No exported adapter, installer, settings or UI uses it.
Q reduced_sample(const Native& n,const Q& current,const Q* authored_start,int percent) {
    return nova::movement_wobble::reduce_sample(current,authored_start,
        nova::movement_wobble::Strength::from_percent(percent),
        [&](const Q& first,const Q& second,float amount) { return n.blend(first,second,amount); });
}
void witness(const Native& n,const Catalog& catalog,const Samples& s2,const Samples& s3,bool callbacks) {
    Controller c100(n,catalog,true,callbacks),c0(n,catalog,true,callbacks),c50(n,catalog,true,callbacks);
    Controller reference_controller(n,catalog,true,false);
    DataLookup d100(n,catalog,c100,s2,s3),d0(n,catalog,c0,s2,s3),d50(n,catalog,c50,s2,s3);
    DataLookup reference_lookup(n,catalog,reference_controller,s2,s3);
    const float loop_start=static_cast<float>(catalog.start),loop_end=static_cast<float>(catalog.end);
    Cursor reference_cursor;
    const auto authored_start=s2.sample(n,loop_start,reference_cursor,reference_lookup.data(n,loop_start));
    // This is an interpolated native time sample in SEGMENT2/5, not its first key,
    // a copied frame191/365, the skeletal bind, an average, or a guessed Idle pose.
    Node node100(n,s3),node0(n,s3),node50(n,s3),replacement(n,s3);
    Consumer out100(n,node100),out0(n,node0),out50(n,node50);
    std::array<Controller*,3> controllers{&c100,&c0,&c50};
    std::array<DataLookup*,3> lookups{&d100,&d0,&d50};
    std::array<Consumer*,3> consumers{&out100,&out0,&out50};
    std::array<Node*,3> nodes{&node100,&node0,&node50};
    std::array<std::array<Cursor,2>,3> cursors{};
    constexpr std::array<int,3> strengths{100,0,50};
    const std::array<uint32_t,8> deltas=catalog.rush?std::array<uint32_t,8>{134,200,199,134,200,199,17,117}
                                                         :std::array<uint32_t,8>{234,400,166,234,400,166,17,217};
    const std::array<uint32_t,8> expected_absolute=catalog.rush?std::array<uint32_t,8>{12300,12500,12166,12300,12500,12166,12183,12300}
                                                                    :std::array<uint32_t,8>{6600,7000,6366,6600,7000,6366,6383,6600};
    // Synthetic independent additive input, expressly NOT a real ADS/recoil trace.
    const Q other{0.f,0.f,.03f,static_cast<float>(std::sqrt(1.0-.03*.03))};
    const auto expected_zero=n.add(authored_start,other);
    const auto original_node100=node100.data,original_node0=node0.data,original_node50=node50.data;
    double max_input_gap=0,max_output_gap=0;
    Q last_original{};
    for(size_t phase=0;phase<deltas.size();++phase) {
        std::array<Q,3> originals{};
        for(size_t i=0;i<3;++i) {
            auto& controller=*controllers[i];auto& lookup=*lookups[i];
            check(controller.advance(n,deltas[phase])==1,"sustained native loop stays active after wrap");
            const float time=controller.time();check(time==static_cast<float>(expected_absolute[phase]),"original timeline advances and wraps, independent of strength");
            const size_t segment=time<=s2.segment_end?0:1;lookup.select(segment);
            const auto& samples=segment==0?s2:s3;
            originals[i]=samples.sample(n,time,cursors[i][segment],lookup.data(n,time));
            const auto logical=controller.logical();const auto component=controller.component;
            const auto slot=controller.slot;const auto timeline=controller.timeline;
            const auto live_j=lookup.animator;const auto live_cursor=cursors[i][segment];
            const auto events=controller.events;
            const auto refreshed_reference=s2.sample(n,loop_start,reference_cursor,reference_lookup.data(n,loop_start));
            check(exact(refreshed_reference,authored_start),"all phases use the SAME native loop-start sample");
            check(controller.logical()==logical && controller.component==component && controller.slot==slot && controller.timeline==timeline,
                  "reference sampling does not change live controller/timeline/fields");
            check(lookup.animator==live_j && cursors[i][segment].cache==live_cursor.cache && cursors[i][segment].key==live_cursor.key,
                  "reference lookup/sampler does not replace live J+4c lease or live cache/key");
            check(controller.events.count==events.count && controller.events.intervals==events.intervals,"reference sampling dispatches no live events");
            const auto calls=n.blend_calls;
            const auto reduced=reduced_sample(n,originals[i],&authored_start,strengths[i]);
            if(i==0) check(exact(reduced,originals[i]) && n.blend_calls==calls,"100 bypasses interpolation exactly");
            if(i==1) check(exact(reduced,authored_start) && n.blend_calls==calls,"0 is the fixed authored pose, not slowed time");
            if(i==2) {
                const double whole=angle(authored_start,originals[i]),half=angle(authored_start,reduced);
                check(half<=whole+0.00001,"50 cannot amplify the selected angular excursion");
                if(whole>.005) check(half>.0001 && half<whole-.0001,"50 is a real intermediate reduction");
            }
            consumers[i]->apply(n,reduced,other);
            check(exact(nodes[i]->rotation(),n.add(reduced,other)),"reduced Main input reaches original current native composition");
        }
        check(exact(originals[0],originals[1]) && exact(originals[0],originals[2]),"all strengths sample the same advancing original input");
        check(c100.logical()==c0.logical() && c100.logical()==c50.logical(),"strength does not alter native controller state");
        check(c100.events.count==c0.events.count && c100.events.count==c50.events.count &&
              c100.events.intervals==c0.events.intervals && c100.events.intervals==c50.events.intervals,"identical original callback dispatch across strengths");
        check(exact(node0.rotation(),expected_zero),"zero holds the shared authored pose at every phase and wrap before composition");
        const auto input_gap=angle(originals[0],authored_start),output_gap=angle(node100.rotation(),node0.rotation());
        if(input_gap>max_input_gap) max_input_gap=input_gap;
        if(output_gap>max_output_gap) max_output_gap=output_gap;
        last_original=originals[0];
    }
    check(max_input_gap>.005 && max_output_gap>.005,"original-function nonzero100-versus-authored0 witness, sign-equivalent quaternions cannot pass");
    check(!c100.events.overflow && !c0.events.overflow && !c50.events.overflow,"bounded observer capacity");
    if(callbacks) {
        check(c100.events.count==10,"two native wrap splits plus ordinary intervals");
        check(c100.events.intervals[2]==std::array<uint32_t,4>{bits(static_cast<float>(expected_absolute[1])),bits(loop_end),bits(loop_start),bits(loop_end)} &&
              c100.events.intervals[3]==std::array<uint32_t,4>{bits(loop_start),bits(loop_start),bits(loop_start),bits(loop_end)},"original wrap callback boundaries, not generated event timing");
    } else check(c100.events.count==0,"actual selected Walk has no event tracks; null receiver remains null");
    node100.only_rotation_changed(original_node100);node0.only_rotation_changed(original_node0);node50.only_rotation_changed(original_node50);

    const auto calls=n.blend_calls;
    check(exact(reduced_sample(n,last_original,nullptr,0),last_original),"missing/unknown reference stays exact native");
    check(exact(reduced_sample(n,last_original,&authored_start,101),last_original),"maximum clips to native100");
    check(exact(reduced_sample(n,last_original,&authored_start,-1),authored_start),"minimum clips to authored0");
    check(n.blend_calls==calls,"fallback/endpoints do not call native interpolation");
    // Original filter and missing-target paths, not fake death/ADS predicates.
    out0.mask_bits=0;const auto before_filter=node0.data;out0.apply(n,last_original,other);
    check(node0.data==before_filter,"original filtered channel makes no typed write");out0.mask_bits=1;
    out0.bind(n,0);const auto before_loss=node0.data;out0.apply(n,authored_start,other);
    check(node0.data==before_loss,"original selected-list wrapper skips a cleared target");
    out0.bind(n,replacement.at());out0.apply(n,authored_start,other);
    check(node0.data==before_loss && exact(replacement.rotation(),expected_zero),"original current cookie rebind writes only new destination");
    // Retained synthetic old storage is deliberate; this is not destruction,
    // weak Bind(null), generation reuse, or a production lifetime receipt.
    const Q other_later{.02f,0.f,0.f,static_cast<float>(std::sqrt(1.0-.02*.02))};
    out0.apply(n,authored_start,other_later);
    check(exact(replacement.rotation(),n.add(authored_start,other_later)) && !exact(replacement.rotation(),expected_zero),
          "zero preserves a changing independent additive contribution, not whole-pose freeze");
    out0.apply(n,reduced_sample(n,last_original,nullptr,0),other);
    check(exact(replacement.rotation(),n.add(last_original,other)),"unknown reference fallback reaches the original typed consumer");

    Controller nonloop(n,catalog,false,false);DataLookup nonloop_data(n,catalog,nonloop,s2,s3);Cursor nonloop_cursor;
    check(nonloop.advance(n,catalog.length+1)==0 && nonloop.time()==loop_end-1,"original non-loop completion clamps to end-1, distinguishes loop from entry/exit");
    nonloop_data.select(1);const auto nonloop_sample=s3.sample(n,nonloop.time(),nonloop_cursor,nonloop_data.data(n,nonloop.time()));
    out0.apply(n,reduced_sample(n,nonloop_sample,nullptr,0),other);
    check(exact(replacement.rotation(),n.add(nonloop_sample,other)),"non-loop negative-control has no authored locomotion reference and stays native");
    std::printf("AUTHORED-LOOP witness clip=%u callbacks=%u max original/reference=%.9f consumed100/0=%.9f radians; native time=%u\n",
                catalog.index,callbacks?1u:0u,max_input_gap,max_output_gap,word(c100.slot.data(),0x1c));
}
}
int main(int argc,char** argv) {
    try {
        check(argc==4,"usage: movement-authored-loop-tests <local ELF> <O08f private resource dir> <O08g private resource dir>");
        const std::filesystem::path library=argv[1],resource=argv[2],extra=argv[3];
        if(!std::filesystem::is_regular_file(library)||!std::filesystem::is_regular_file(resource/"record75-sanimation.bin")||
           !std::filesystem::is_regular_file(extra/"walk-clip56.bin")) { std::puts("SKIP: private original inputs missing");return 77; }
        nova::ImportRegistry registry;nova::bionic::register_data(registry);
        // Original acosf/sinf wrappers reach these existing port imports for
        // the Walk start bracket. No sampler/slerp CALL is replaced.
        nova::bionic::register_math(registry);
        nova::ElfImage image(library,registry);
        unsigned math_imports=0;
        for(const auto& imported:image.imports()) if(imported.name=="acos" || imported.name=="sin") {
            check(!imported.trapped && !imported.placeholder,"reached existing math import is bound");++math_imports;
        }
        check(math_imports==2,"both exact reached math imports are present");
        const Native native(image);const Catalog walk(extra,false),rush(extra,true);
        const Samples segment2(resource,extra,native,2),segment3(resource,extra,native,3),segment5(resource,extra,native,5),segment6(resource,extra,native,6);
        const auto s2_times=segment2.times,s2_values=segment2.values,s3_times=segment3.times,s3_values=segment3.values;
        const auto s5_times=segment5.times,s5_values=segment5.values,s6_times=segment6.times,s6_values=segment6.values;
        const std::array<std::vector<uint8_t>,4> records{segment2.record,segment3.record,segment5.record,segment6.record};
        const std::array<std::vector<uint8_t>,4> descriptors{segment2.descriptor,segment3.descriptor,segment5.descriptor,segment6.descriptor};
        witness(native,walk,segment2,segment3,false);witness(native,walk,segment2,segment3,true);
        witness(native,rush,segment5,segment6,false);witness(native,rush,segment5,segment6,true);
        check(segment2.times==s2_times && segment2.values==s2_values && segment3.times==s3_times && segment3.values==s3_values,"captured resource payloads unchanged");
        check(segment5.times==s5_times && segment5.values==s5_values && segment6.times==s6_times && segment6.values==s6_values,"sprint captured resource payloads unchanged");
        check(records==std::array<std::vector<uint8_t>,4>{segment2.record,segment3.record,segment5.record,segment6.record} &&
              descriptors==std::array<std::vector<uint8_t>,4>{segment2.descriptor,segment3.descriptor,segment5.descriptor,segment6.descriptor},"native metadata is not rewritten for strength");
        std::puts("PASS: original Walk/RushWalk arm-channel loop/current/reference/current-cookie witness on synthetic owned storage; production/O09/draw binding not proved");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
