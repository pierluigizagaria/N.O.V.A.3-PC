#pragma once
#include "abi/guest_hook.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace sprite_fixture {
inline uint32_t ptr(const void* p) { return reinterpret_cast<uint32_t>(p); }
inline uint32_t& word(uint32_t p,unsigned offset=0) { return *reinterpret_cast<uint32_t*>(p+offset); }
inline uint16_t& half(uint32_t p,unsigned offset=0) { return *reinterpret_cast<uint16_t*>(p+offset); }
inline unsigned allocated=0;
inline uint32_t __cdecl alloc(uint32_t n,uint32_t) { ++allocated;return ptr(std::calloc(1,n)); }
inline void __cdecl free(uint32_t p) { if(p) {--allocated;std::free(reinterpret_cast<void*>(p));} }
inline void check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
struct Runtime {
    nova::ImportRegistry imports;
    nova::bionic::CxaRegistry cxa;
    std::unique_ptr<nova::ElfImage> image;
    std::unique_ptr<nova::abi::GuestHook> allocate,deallocate;
    Runtime(const char* path):cxa(imports) {
        nova::bionic::register_boot(imports);nova::audio::register_opensl(imports);nova::gles::register_imports(imports);
        image=std::make_unique<nova::ElfImage>(path,imports);
        allocate=std::make_unique<nova::abi::GuestHook>(*image,"_Z11GlitchAllocjN6glitch6memory13E_MEMORY_HINTE",std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x28},ptr(reinterpret_cast<void*>(&alloc)));
        deallocate=std::make_unique<nova::abi::GuestHook>(*image,"_Z10GlitchFreePv",std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x18},ptr(reinterpret_cast<void*>(&free)));
    }
    uint32_t fn(const char* name) { return nova::abi::require_function(*image,name); }
};
struct Sprite {
    Runtime& runtime;
    std::array<uint32_t,50> data{};
    uint32_t address() {return ptr(data.data());}
    Sprite(Runtime& r,const std::filesystem::path& path):runtime(r) {
        std::ifstream input(path,std::ios::binary);check(bool(input),"sprite fixture readable");
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        std::array<uint32_t,16> file{};
        nova::abi::invoke(r.fn("_ZN6glitch2io15CMemoryReadFileC1EPvlPKcbb"),{ptr(file.data()),ptr(bytes.data()),static_cast<uint32_t>(bytes.size()),ptr("fixture.bsprite"),0,1});
        nova::abi::invoke(r.fn("_ZN7CSpriteC1Ev"),{address()});
        uint32_t file_ref=ptr(file.data());
        nova::abi::invoke(r.fn("_ZN7CSprite14LoadSpriteDataEN5boost13intrusive_ptrIN6glitch2io9IReadFileEEE"),{address(),ptr(&file_ref)});
        check(file[1]==1,"file reference balance");
        nova::abi::invoke(r.fn("_ZN6glitch2io15CMemoryReadFileD1Ev"),{ptr(file.data())});
    }
    ~Sprite() { half(address(),0x98)=0;nova::abi::invoke(runtime.fn("_ZN7CSpriteD1Ev"),{address()}); }
};
}
