// V01d authored fixture; never built or executed by the source-only author.
// Original list, programmable publisher, nonempty flush and timeline consumers.
// Synthetic records/callbacks only; no original drawImpl/GPU or game initializers.
#ifndef NOVA_V01D_RELEASE
#error This opt-in fixture must be built explicitly in Release.
#endif
#include "abi/guest_call.h"
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
using Matrix = std::array<float, 16>;
Matrix identity() { Matrix m{}; m[0]=m[5]=m[10]=m[15]=1; return m; }
Matrix matrix_at(uint32_t p, uint32_t offset) {
    Matrix m; std::memcpy(m.data(), reinterpret_cast<const void*>(p + offset), 64); return m;
}
uint32_t float_word(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }
struct Native {
    uint32_t list, publish, implementation, flush, update, time, length, jump;
    explicit Native(const nova::ElfImage& image) {
        check(digest(image.source_bytes()) == "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e", "pinned ELF identity");
        auto at = [&](uint32_t va, uint32_t size) {
            const auto fn=image.address(va,size); check(image.executable(fn) && image.executable(fn+size-1), "original executable mapping"); return fn;
        };
        list=at(0x337b90,831); publish=at(0x89e650,155);
        implementation=at(0x9211c0,993); flush=at(0x8cc200,12);
        update=at(0x2736c0,973); time=at(0x26cc70,19); length=at(0x26cc50,19); jump=at(0x8a18c0,18);
        check(publish==image.symbol("_ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj"), "publisher symbol");
        check(flush==image.symbol("_ZN6glitch5video12IVideoDriver5flushEv"), "flush symbol");
        check(update==image.symbol("_ZN19CAnimationComponent25UpdateAnimationControllerEii"), "timeline consumer symbol");
        // Entire source identity above authenticates the unchanged mapped functions.
        // No byte replacement, hook, initializer, factory or real driver construction.
    }
    void projection(uint32_t driver,const Matrix& m) const {
        const uint32_t args[]{driver,1,address(m.data()),0}; nova::abi::call_guest(publish,args,4);
    }
    void model(uint32_t driver,const Matrix& m) const {
        const uint32_t args[]{driver,2,address(m.data()),0}; nova::abi::call_guest(publish,args,4);
    }
    void render_list(uint32_t manager,uint32_t vector,bool clear) const {
        const uint32_t args[]{manager,0,vector,clear ? 1u : 0u}; nova::abi::call_guest(list,args,4);
    }
};
struct DrawRecord {
    uint32_t driver=0, stream=0, primitives=0, binding=0, vertices=0, indices=0;
    Matrix projection{}, raw_projection{}, model{}, raw_model{};
};
struct Driver;
struct DrawProbe {
    const Native* native=nullptr;
    std::array<DrawRecord,8> records{};
    size_t count=0;
    bool overflow=false, nested_once=false;
    Driver* nested_driver=nullptr;
    Matrix nested_projection{};
    uint32_t logical_current=0;
} probe;
uint32_t __cdecl attributes(uint32_t baker) { return load(baker,4); }
void __cdecl draw_observer(uint32_t driver,uint32_t streams,uint32_t primitives,uint32_t binding);
struct Driver {
    alignas(16) std::array<uint8_t,0x1400> bytes{};
    alignas(4) std::array<uint8_t,0x68> batch{};
    alignas(4) std::array<uint8_t,12> streams{};
    std::array<uint32_t,48> table{};
    std::array<uint32_t,6> baker_table{};
    std::array<uint32_t,2> baker{};
    std::array<uint32_t,1> pool{};
    uint32_t attributes_marker=0x13579;
    explicit Driver(const Native& n) {
        table[0x58/4]=n.implementation; table[0xb8/4]=n.flush;
        table[0xbc/4]=reinterpret_cast<uint32_t>(&draw_observer);
        baker_table[0x14/4]=reinterpret_cast<uint32_t>(&attributes);
        baker[0]=address(baker_table.data()); baker[1]=address(&attributes_marker);
        pool[0]=address(batch.data());
        word(bytes,0,address(table.data())); word(bytes,0x298,pool[0]);
        word(bytes,0x2a0,address(baker.data()));
        word(bytes,0x288,address(pool.data())); word(bytes,0x28c,address(pool.data())+4);
        word(bytes,0x14c,address(&attributes_marker));
        word(batch,8,address(streams.data())); word(streams,0,1);
        word(batch,0x3c,4); word(batch,0x40,2);
        // No mapped native buffers: +0x44==0 skips IBuffer::unmap.
        // Empty equal attribute ranges skip dereference and comparison.
        // One-element pool remains owned throughout original return.
    }
    Driver(const Driver&)=delete;
    Driver& operator=(const Driver&)=delete;
    uint32_t ptr() const { return address(bytes.data()); }
    void pending() { word(batch,0x30,12); word(batch,0x38,6); }
    bool empty() const { return load(address(batch.data()),0x30)==0 && load(address(batch.data()),0x38)==0; }
};
void __cdecl draw_observer(uint32_t driver,uint32_t streams,uint32_t primitives,uint32_t binding) {
    if(probe.count>=probe.records.size()) { probe.overflow=true; return; }
    auto& r=probe.records[probe.count++]; r.driver=driver; r.stream=load(streams);
    r.primitives=primitives; r.binding=binding; r.vertices=load(r.stream,8);
    r.indices=load(primitives,8); r.projection=matrix_at(driver,0x9bc);
    r.raw_projection=matrix_at(driver,0x1ac); r.model=matrix_at(driver,0x9fc);
    r.raw_model=matrix_at(driver,0x1ec);
    if(probe.nested_once) {
        probe.nested_once=false; // Bound this deliberate reentry to one level.
        probe.logical_current=probe.nested_driver->ptr();
        probe.native->projection(probe.nested_driver->ptr(),probe.nested_projection);
    }
}
void reset_probe(const Native& n,uint32_t current) { probe={}; probe.native=&n; probe.logical_current=current; }
void flush_contract(const Native& n) {
    Driver d(n), replacement(n);
    Matrix base=identity(), placed=identity(), child=identity(), model=identity();
    base[0]=2; placed[0]=3; child[0]=4; model[12]=7;
    n.projection(d.ptr(),base); n.model(d.ptr(),model);
    const auto before_view=matrix_at(d.ptr(),0x97c);
    const auto before_raw_view=matrix_at(d.ptr(),0x16c);
    reset_probe(n,d.ptr()); d.pending(); n.projection(d.ptr(),placed);
    check(probe.count==1 && !probe.overflow,"original nonempty flush reaches draw exactly once");
    const auto& first=probe.records[0];
    check(first.projection==base && first.raw_projection==base,"old pending batch flushes before new projection publication");
    check(first.model==identity() && first.raw_model==identity(),"original flush publishes identity model during aggregate draw");
    check(first.vertices==3 && first.indices==3 && first.binding==0,"actual accumulated counts and null binding reach observer");
    check(matrix_at(d.ptr(),0x9bc)==placed && matrix_at(d.ptr(),0x1ac)==placed,"original programmable and raw projections commit afterward");
    check(matrix_at(d.ptr(),0x9fc)==model && matrix_at(d.ptr(),0x1ec)==model && d.empty(),"model restored and batch offsets cleared");
    check(load(address(d.streams.data()))==1,"original intrusive reference is balanced");
    reset_probe(n,d.ptr()); d.pending(); n.projection(d.ptr(),base);
    check(probe.count==1 && probe.records[0].projection==placed,"explicit retirement flush consumes pending placement first");
    check(matrix_at(d.ptr(),0x9bc)==base && matrix_at(d.ptr(),0x97c)==before_view && matrix_at(d.ptr(),0x16c)==before_raw_view,"baseline is republished without view changes");

    // Counterexample: the original flush has not cleared its pending batch at
    // drawImpl callback entry. Same-driver reentry can consume it a second time.
    reset_probe(n,d.ptr()); d.pending(); probe.nested_once=true;
    probe.nested_driver=&d; probe.nested_projection=child;
    n.projection(d.ptr(),placed);
    check(probe.count==2 && !probe.overflow,"same-driver nested publication reflushes still-pending batch");
    check(probe.records[0].stream==probe.records[1].stream && probe.records[1].projection==base,
          "nested draw uses the same pending source and old projection");
    check(matrix_at(d.ptr(),0x9bc)==placed && load(address(d.streams.data()))==1,"outer publication wins after nested return");

    // Owner replacement is simulated by changing only the harness's current
    // owner. Old storage remains allocated; never exercise a use-after-free.
    n.projection(d.ptr(),base); reset_probe(n,d.ptr()); d.pending();
    probe.nested_once=true; probe.nested_driver=&replacement; probe.nested_projection=child;
    n.projection(d.ptr(),placed);
    check(probe.logical_current==replacement.ptr() && matrix_at(replacement.ptr(),0x9bc)==child,
          "nested replacement owns its independent publication");
    check(matrix_at(d.ptr(),0x9bc)==placed,"original outer call still writes old retained storage after logical owner change");
    // These are original consumer properties on synthetic storage, not a safe
    // production lifetime protocol or proof of real scene callback behavior.
}
struct ListHarness;
struct Binding { uint32_t table; ListHarness* owner; uint32_t id; };
void __cdecl list_callback(uint32_t receiver,uint32_t data);
struct ListHarness {
    const Native& native; Driver& driver;
    alignas(4) std::array<uint8_t,0x1b0> manager{};
    alignas(4) std::array<uint8_t,0xf0> node_a{},node_b{},node_child{};
    std::array<uint32_t,3> callback_table{};
    std::array<uint32_t,3> vector{},child_vector{};
    std::array<uint32_t,6> entries{};
    std::array<uint32_t,4> child_entries{};
    Binding a{},b{},child{};
    Matrix native_projection=identity();
    unsigned calls_a=0,calls_b=0,calls_child=0;
    bool nested=false, change_owner=false, saw_pass_zero=true, saw_previous_node=true;
    uint32_t owner_epoch=1;
    ListHarness(const Native& n,Driver& d):native(n),driver(d) {
        callback_table[2]=reinterpret_cast<uint32_t>(&list_callback);
        a={address(callback_table.data()),this,1}; b={address(callback_table.data()),this,2};
        child={address(callback_table.data()),this,3};
        word(node_a,0xec,address(&a)); word(node_b,0xec,address(&b)); word(node_child,0xec,address(&child));
        entries={address(node_a.data()),11,address(node_b.data()),22,0,0};
        child_entries={address(node_child.data()),33,0,0};
        vector={address(entries.data()),address(entries.data())+16,address(entries.data())+24};
        child_vector={address(child_entries.data()),address(child_entries.data())+8,address(child_entries.data())+16};
    }
    uint32_t ptr() const { return address(manager.data()); }
};
void __cdecl list_callback(uint32_t receiver,uint32_t data) {
    auto& binding=*reinterpret_cast<Binding*>(receiver); auto& x=*binding.owner;
    x.saw_pass_zero=x.saw_pass_zero && load(x.ptr(),0x10c)==0;
    const uint32_t expected=binding.id==1 ? address(x.node_a.data()) : binding.id==2 ? address(x.node_b.data()) : address(x.node_child.data());
    x.saw_previous_node=x.saw_previous_node && load(x.ptr(),0xac)==expected;
    if(binding.id==1) {
        ++x.calls_a; x.saw_previous_node=x.saw_previous_node && data==11;
        x.native.projection(x.driver.ptr(),x.native_projection);
        if(x.change_owner) { ++x.owner_epoch; word(x.manager,0xdc,address(x.node_child.data())); }
        if(x.nested) x.native.render_list(x.ptr(),address(x.child_vector.data()),false);
    } else if(binding.id==2) { ++x.calls_b; x.saw_previous_node=x.saw_previous_node && data==22; }
    else { ++x.calls_child; x.saw_previous_node=x.saw_previous_node && data==33; }
}
void auxiliary_contract(const Native& n) {
    Driver d(n); Matrix early=identity(); early[0]=3;
    ListHarness x(n,d); x.native_projection[0]=2;
    n.projection(d.ptr(),early); reset_probe(n,d.ptr()); d.pending(); x.change_owner=true;
    n.render_list(x.ptr(),address(x.vector.data()),false);
    check(x.calls_a==1 && x.calls_b==1 && x.owner_epoch==2,"actual auxiliary loop continues after logical owner replacement");
    check(x.saw_pass_zero && x.saw_previous_node,"pass0 and exact node/data dispatch metadata");
    check(probe.count==1 && probe.records[0].projection==early,"auxiliary callback reaches actual original programmable flush");
    check(matrix_at(d.ptr(),0x9bc)==x.native_projection,"auxiliary callback can overwrite an earlier projection copy");
    check(x.vector[1]==x.vector[0]+16,"false clear flag removes only sentinel");
    n.render_list(x.ptr(),address(x.vector.data()),true);
    check(x.vector[1]==x.vector[0],"true clear flag clears entries");

    ListHarness nested(n,d); nested.nested=true;
    n.render_list(nested.ptr(),address(nested.vector.data()),false);
    check(nested.calls_a==1 && nested.calls_child==1 && nested.calls_b==0,
          "same-manager nested list leaves sentinel metadata; outer next entry is skipped");
    // A lexical interval/positive entry token alone is not a reentrancy fence.
    // No claim that this adversarial callback occurred in the live game.
}
void timeline_contract(const Native& n) {
    alignas(4) std::array<uint8_t,0x148> animation{};
    alignas(4) std::array<uint8_t,0x38> slot{};
    alignas(4) std::array<uint8_t,0x4c> controller{};
    std::array<uint32_t,5> table{}; table[4]=n.jump;
    word(animation,0x20,address(slot.data())); word(controller,0,address(table.data()));
    word(slot,0,2); word(slot,4,100); word(slot,8,1000); word(slot,0x18,float_word(1));
    word(slot,0x1c,10); word(slot,0x28,address(controller.data())); controller[0x34]=1;
    const uint32_t args[]{address(animation.data()),0,5};
    nova::abi::call_guest(n.update,args,3);
    const uint32_t get_args[]{address(animation.data()),0};
    check(nova::abi::call_guest(n.time,get_args,2)==15 && nova::abi::call_guest(n.length,get_args,2)==100,"actual native slot time and length");
    check(load(address(controller.data()),4)==float_word(1015) && controller[0x34]==0,
          "actual UpdateAnimationController calls original jumpTo with clip start plus time");
    word(slot,0x18,float_word(.5f));
    const uint32_t short_tick[]{address(animation.data()),0,1};
    nova::abi::call_guest(n.update,short_tick,3);
    check(load(address(slot.data()),0x1c)==15,"native dt times speed is truncated on this ordinary branch");
    word(slot,8,2000); const uint32_t no_tick[]{address(animation.data()),0,0};
    nova::abi::call_guest(n.update,no_tick,3);
    check(load(address(controller.data()),4)==float_word(2015),"same normalized time does not identify the clip interval");
    // No ADS pose/channel, sight transform, arbitrary speed sign, loop, event
    // dispatch, or alignment blend is certified by this time-only witness.
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"usage: weapon-presentation-interval-tests <local ELF path>");
        const std::filesystem::path library=argv[1];
        if(!std::filesystem::is_regular_file(library)) { std::puts("SKIP: local game library absent"); return 77; }
        nova::ImportRegistry registry; nova::bionic::register_data(registry);
        nova::ElfImage image(library,registry); const Native native(image);
        flush_contract(native); auxiliary_contract(native); timeline_contract(native);
        std::puts("PASS: original auxiliary dispatch, programmable nonempty flush and timeline witnesses; no ADS alignment or placement acceptance");
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
