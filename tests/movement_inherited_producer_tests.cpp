// O08e prerequisite fixture: AUTHORED ONLY, never built or executed by this author.
// Connect original position setter, relative builder, absolute updater and getter.
// Synthetic owned storage; no initializer, render, animation clock or O09 capsule.
#ifndef NOVA_O08E_RELEASE
#error This opt-in fixture must be built explicitly in Release.
#endif
#include "abi/guest_call.h"
#include "abi/guest_hook.h"
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
    uint32_t position=0, update=0, getter=0, internal=0, object_position=0;
    uint32_t empty_table=0, root_table=0, bound_table=0;
    explicit Native(const nova::ElfImage& image) {
        check(digest(image.source_bytes()) == "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e", "pinned original ELF");
        auto code=[&](uint32_t va,uint32_t length) {
            auto p=image.address(va,length);
            check(image.executable(p) && image.executable(p+length-1),"mapped original code");
            return p;
        };
        position=code(0x9cd1b0,0x86); update=code(0x9cd4d0,0x18f);
        object_position=code(0x58aee0,0x41);
        getter=code(0x9cd4c0,11); internal=code(0xa02440,0x447);
        check(position==image.symbol("_ZN6glitch5scene10ISceneNode11setPositionERKNS_4core8vector3dIfEE"),"position symbol");
        check(internal==image.symbol("_ZN6glitch5scene10ISceneNode30updateAbsolutePositionInternalEv"),"composition symbol");
        // These address points come from actual C1 installers, not a base-name guess.
        empty_table=image.address(0x112e050,0xbc);
        root_table=image.address(0x10fd0f0,0xbc);
        bound_table=image.address(0x1120b90,0xbc);
        for(auto table : {empty_table,root_table,bound_table}) {
            check(load(table,0x48)==getter,"installed parent getter");
            check(load(table,0xa0)==position,"installed position setter");
            check(load(table,0xb8)==internal,"installed absolute update");
        }
        check(load(root_table,0x68)==image.address(0xa27a50,1),"actual root attachment slot");
    }
    void set_position(uint32_t node,const std::array<float,3>& value) const {
        const uint32_t args[]{node,address(value.data())};
        nova::abi::call_guest(position,args,2);
    }
    void refresh(uint32_t node) const {
        const uint32_t args[]{node,0}; nova::abi::call_guest(update,args,2);
    }
    uint32_t absolute_pointer(uint32_t node) const {
        const uint32_t args[]{node}; return nova::abi::call_guest(getter,args,1);
    }
};
struct Node {
    alignas(16) std::array<uint8_t,0x1d0> bytes{};
    Node* synthetic_parent=nullptr;
    explicit Node(uint32_t table) {
        word(bytes,0,table);
        const auto unit=identity();
        std::memcpy(bytes.data()+0x10,unit.data(),64);
        std::memcpy(bytes.data()+0x50,unit.data(),64);
        word(bytes,0xa8,float_word(1)); // Local quaternion w.
        for(size_t offset : {size_t(0xac),size_t(0xb0),size_t(0xb4)}) word(bytes,offset,float_word(1));
        // Synthetic identity components/relative/absolute bits, derived from
        // original setter/builder branches. This is not a constructor fixture.
        word(bytes,0xf4,0xf8000);
        word(bytes,0xd4,ptr()+0xd4); word(bytes,0xd8,ptr()+0xd4);
    }
    Node(const Node&)=delete;
    Node& operator=(const Node&)=delete;
    uint32_t ptr() const { return address(bytes.data()); }
    void parent(Node* value) {
        // Single-child intrusive list layout from original a27a8e..a27aaa.
        // Establish synthetic topology only; do not claim native addChild lifetime.
        if(synthetic_parent) {
            word(synthetic_parent->bytes,0xd4,synthetic_parent->ptr()+0xd4);
            word(synthetic_parent->bytes,0xd8,synthetic_parent->ptr()+0xd4);
        }
        synthetic_parent=value;
        word(bytes,4,value ? value->ptr()+0xd4 : 0);
        word(bytes,8,value ? value->ptr()+0xd4 : 0);
        if(value) {
            check(load(value->ptr(),0xd4)==value->ptr()+0xd4,"single-child synthetic topology");
            word(value->bytes,0xd4,ptr()+4); word(value->bytes,0xd8,ptr()+4);
        }
        word(bytes,0xd0,value ? value->ptr() : 0);
        word(bytes,0xf4,load(ptr(),0xf4)|0x100);
    }
    void clean_absolute_dirty() { word(bytes,0xf4,load(ptr(),0xf4)&~0x100u); }
    Matrix absolute() const { return matrix_at(ptr(),0x10); }
    std::array<uint32_t,3> local_bits() const {
        return {load(ptr(),0x90),load(ptr(),0x94),load(ptr(),0x98)};
    }
};
Matrix translated(float x,float y,float z) {
    auto result=identity(); result[12]=x; result[13]=y; result[14]=z; return result;
}
void inherited_origin_contract(const Native& n) {
    Node physical(n.empty_table), weapon(n.root_table), bound(n.bound_table), other(n.empty_table);
    weapon.parent(&physical); bound.parent(&weapon);
    const std::array<float,3> zero{0.f,-0.f,0.f};
    n.set_position(weapon.ptr(),zero); n.set_position(bound.ptr(),zero);
    n.set_position(physical.ptr(),{8.f,-4.f,2.f});
    n.refresh(physical.ptr()); n.refresh(weapon.ptr()); n.refresh(bound.ptr());
    const auto origin_a=translated(8.f,-4.f,2.f);
    check(physical.absolute()==origin_a && weapon.absolute()==origin_a && bound.absolute()==origin_a,
          "zero local sample inherits nonzero physical origin through ORIGINAL methods");
    check(bound.local_bits()==std::array<uint32_t,3>{0,0x80000000u,0},
          "original zero local values remain intact");
    check(n.absolute_pointer(weapon.ptr())==weapon.ptr()+0x10,"getter returns parent absolute matrix storage");

    // This models the proved producer's value COPY, not execution of Player or
    // WeaponManager. Different physical origins are not removable bob samples.
    physical.clean_absolute_dirty(); weapon.clean_absolute_dirty(); bound.clean_absolute_dirty();
    n.set_position(physical.ptr(),{16.f,3.f,5.f});
    check(n.absolute_pointer(physical.ptr())==physical.ptr()+0x10 && physical.absolute()==origin_a,
          "getter does not refresh a dirty producer");
    n.refresh(weapon.ptr()); n.refresh(bound.ptr());
    check(bound.absolute()==origin_a,"child update reads stale parent until parent refresh runs");
    n.refresh(physical.ptr()); n.refresh(weapon.ptr()); n.refresh(bound.ptr());
    const auto origin_b=translated(16.f,3.f,5.f);
    check(bound.absolute()==origin_b,"ordered producer then consumer refresh publishes current origin");

    // Deliberately synthetic local probe, NOT an authenticated locomotion key.
    // It discriminates a local contribution from the complete inherited matrix.
    n.set_position(weapon.ptr(),{.25f,-.5f,.125f});
    n.refresh(weapon.ptr()); n.refresh(bound.ptr());
    check(bound.absolute()==translated(16.25f,2.5f,5.125f),"original nonidentity parent/local composition");
    n.set_position(weapon.ptr(),zero); n.refresh(weapon.ptr()); n.refresh(bound.ptr());
    check(bound.absolute()==origin_b,"removing a synthetic local probe preserves physical origin");
    check(bound.absolute()!=identity(),"zero local contribution is not zero final transform");

    // Logical owner replacement only: both old/new synthetic buffers remain live.
    n.set_position(other.ptr(),{-2.f,7.f,11.f}); n.refresh(other.ptr());
    weapon.parent(&other); n.refresh(weapon.ptr()); n.refresh(bound.ptr());
    check(bound.absolute()==translated(-2.f,7.f,11.f),"actual parent pointer determines new inherited origin");
    check(physical.absolute()==origin_b,"old owner buffer remains unmodified");
    weapon.parent(nullptr); n.set_position(weapon.ptr(),zero);
    n.refresh(weapon.ptr()); n.refresh(bound.ptr());
    check(weapon.absolute()==identity() && bound.absolute()==identity(),"detached identity path is separate from translated parent");
    // No percent setting, no claimed native 100-versus-0 movement witness,
    // no positive FPS decision, no lifetime guarantee for real scene objects.
}
struct ObjectProbe {
    uint32_t object=0, replacement_node=0;
    unsigned calls=0;
    bool wrong_receiver=false;
    std::array<float,3> input{};
} object_probe;
void __cdecl internal_position_observer(uint32_t object,uint32_t value) {
    ++object_probe.calls;
    if(object!=object_probe.object) { object_probe.wrong_receiver=true; return; }
    std::memcpy(object_probe.input.data(),reinterpret_cast<const void*>(value),12);
    if(object_probe.replacement_node)
        std::memcpy(reinterpret_cast<void*>(object+0x98),&object_probe.replacement_node,4);
}
void object_root_contract(const nova::ElfImage& image,const Native& n) {
    // ONE checked call substitution prevents physics/zone/MP singleton access.
    // The real SetPosition wrapper, installed node setter and downstream
    // parent/root/bound consumers still run. Never call the substituted callee.
    nova::abi::GuestCall boundary(image,
        "_ZN11CGameObject11SetPositionERKN6glitch4core8vector3dIfEE",0x19,
        "_ZN11CGameObject19SetPositionInternalERKN6glitch4core8vector3dIfEE",
        reinterpret_cast<uint32_t>(&internal_position_observer));
    Node physical(n.empty_table), root(n.root_table), bound(n.bound_table), replacement(n.root_table);
    root.parent(&physical); bound.parent(&root);
    alignas(4) std::array<uint8_t,0x100> object{};
    word(object,0x98,root.ptr());
    n.set_position(physical.ptr(),{8.f,-4.f,2.f});
    n.set_position(root.ptr(),{1.f,2.f,3.f});
    const std::array<float,3> zero{0.f,-0.f,0.f};
    object_probe={}; object_probe.object=address(object.data());
    const uint32_t args[]{address(object.data()),address(zero.data())};
    nova::abi::call_guest(n.object_position,args,2);
    check(object_probe.calls==1 && !object_probe.wrong_receiver && object_probe.input==zero,
          "actual object producer forwards the selected zero-local argument");
    n.refresh(physical.ptr()); n.refresh(root.ptr()); n.refresh(bound.ptr());
    check(bound.absolute()==translated(8.f,-4.f,2.f),
          "original object+98 setter path retains nonzero inherited physical origin");

    n.set_position(root.ptr(),{1.f,2.f,3.f});
    const auto old_root_local=root.local_bits();
    object_probe.replacement_node=replacement.ptr();
    nova::abi::call_guest(n.object_position,args,2);
    check(root.local_bits()==old_root_local && replacement.local_bits()==std::array<uint32_t,3>{0,0x80000000u,0},
          "original wrapper reacquires object+98 AFTER callback; cached old root would fail");
    check(load(address(object.data()),0x98)==replacement.ptr(),"synthetic replacement is current owner");
    // Both synthetic buffers remain allocated. This deliberately does not
    // model live destruction, allocator reuse, admission or animation sampling.
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"usage: movement-inherited-producer-tests <local ELF path>");
        const std::filesystem::path library=argv[1];
        if(!std::filesystem::is_regular_file(library)) {
            std::puts("SKIP: local game library absent"); return 77;
        }
        nova::ImportRegistry registry; nova::bionic::register_data(registry);
        nova::ElfImage image(library,registry); const Native original(image);
        inherited_origin_contract(original); object_root_contract(image,original);
        std::puts("PASS: original inherited-origin mechanics on synthetic storage; locomotion reduction remains unproved");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
}
