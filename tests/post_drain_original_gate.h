#pragma once
// Optional, unexecuted original-function gate. No initializers, scene, device or
// game loop. The production adapter still runs through the authored ordinary
// caller. Only event arithmetic and UpdateFromInput execute original ELF bodies.
#include "bionic/imports.h"
#include <memory>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
// Test-local failure boundary shared by every host entry registered with ELF.
// Exit86 cannot satisfy the existing negative runner's exact exit1 contract.
namespace post_drain_fixture {
[[noreturn]] inline void callback_fail_fast(const char *entry) noexcept {
    std::fprintf(stderr,"T13B3F_HOST_CALLBACK_EXCEPTION:%s\n",entry);
    std::fflush(stderr);
    std::_Exit(86); // No unwinding or process-exit callbacks through guest frames.
}
}
namespace post_drain_original {
inline PostDrainWorld *world=nullptr;
inline uint32_t __cdecl level() noexcept try { return world->level; } catch (...) { post_drain_fixture::callback_fail_fast("original.level"); }
inline uint32_t __cdecl player(uint32_t) noexcept try { return world->player; } catch (...) { post_drain_fixture::callback_fail_fast("original.player"); }
inline uint32_t __cdecl app() noexcept try { return world->app; } catch (...) { post_drain_fixture::callback_fail_fast("original.app"); }
inline uint32_t __cdecl no() noexcept try { return 0; } catch (...) { post_drain_fixture::callback_fail_fast("original.no"); }
inline uint32_t button_update=0,zoom_update=0,neutral_button=0,normal_interface=0;
inline void __cdecl later(uint32_t) noexcept try {
    nova::abi::invoke(button_update,{neutral_button});
    nova::abi::invoke(zoom_update,{normal_interface});
} catch (...) { post_drain_fixture::callback_fail_fast("original.later"); }
inline void __cdecl velocity(uint32_t p,uint32_t dt) noexcept try {
    ++world->velocity_samples; world->velocity_dt=dt;
    world->consumed_move_x=*reinterpret_cast<float *>(p+0x1c);
    world->consumed_move_y=*reinterpret_cast<float *>(p+0x24);
} catch (...) { post_drain_fixture::callback_fail_fast("original.velocity"); }
class Write {
    uint32_t address_;
    std::vector<uint8_t> original_;
    DWORD protection_=0;
    static void copy(uint32_t address,const void *data,size_t size,DWORD final_protection) {
        DWORD old=0,ignored=0;
        if(!VirtualProtect(reinterpret_cast<void *>(address),size,PAGE_EXECUTE_READWRITE,&old))
            throw std::runtime_error("original gate protection");
        std::memcpy(reinterpret_cast<void *>(address),data,size);
        if(!VirtualProtect(reinterpret_cast<void *>(address),size,final_protection,&ignored) ||
           !FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void *>(address),size))
            throw std::runtime_error("original gate restore protection");
    }
  public:
    Write(uint32_t address,const void *data,size_t size) : address_(address),original_(size) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void *>(address),&info,sizeof(info)))
            throw std::runtime_error("original gate protection query");
        protection_=info.Protect;
        std::memcpy(original_.data(),reinterpret_cast<void *>(address),size);
        try { copy(address,data,size,protection_); }
        catch(...) {
            // A throwing constructor has no destructor. Roll back bytes AND
            // the pre-mutation protection even when copy failed after memcpy.
            try { copy(address,original_.data(),size,protection_); }
            catch(...) { std::terminate(); }
            throw;
        }
    }
    ~Write() { try { copy(address_,original_.data(),original_.size(),protection_); } catch(...) { std::terminate(); } }
    Write(const Write &)=delete;
};
inline nova::ImportRegistry &data_imports(nova::ImportRegistry &imports) {
    nova::bionic::register_data(imports);
    return imports;
}
class Gate {
    nova::ImportRegistry imports_;
    nova::ElfImage image_;
    std::vector<std::unique_ptr<Write>> writes_;
    std::array<uint8_t,0x80> options_{};
    std::array<uint8_t,0x90> button_{};
    void *caller_=nullptr;
    uint32_t read(uint32_t p) { return *reinterpret_cast<uint32_t *>(p); }
    void word(uint32_t address,uint32_t value) { writes_.push_back(std::make_unique<Write>(address,&value,4)); }
    void byte(uint32_t address,uint8_t value) { writes_.push_back(std::make_unique<Write>(address,&value,1)); }
    void entry(const char *name,uint32_t rva,uint32_t replacement) {
        const auto address=image_.symbol(name);
        if(address!=image_.address(rva,5)) throw std::runtime_error("original helper identity mismatch");
        uint8_t patch[5]{0xe9}; const auto displacement=replacement-address-5;
        std::memcpy(patch+1,&displacement,4);
        writes_.push_back(std::make_unique<Write>(address,patch,5));
    }
  public:
    Gate(const char *file,PostDrainWorld &state,const nova::ElfImage &fixture) : image_(file,data_imports(imports_)) {
        world=&state;
        // Original got base is 0x113e340, proved by onEvent's PC thunk+add.
        // Retained live data slots are replaced only inside this isolated image.
        const uint32_t got=image_.base()+0x113e340;
        *reinterpret_cast<float *>(options_.data()+0x44)=1.f;
        word(read(got-0xbec),reinterpret_cast<uint32_t>(options_.data()));
        byte(read(got-0xa0),0); byte(read(got-0x2c0),0); byte(read(got-0xbbc),0);
        entry("_ZN6CLevel8GetLevelEv",0x370fe0,reinterpret_cast<uint32_t>(&level));
        entry("_ZN6CLevel18GetPlayerComponentEv",0x391680,reinterpret_cast<uint32_t>(&player));
        entry("_ZN3glf3App11GetInstanceEv",0xae9c60,reinterpret_cast<uint32_t>(&app));
        entry("_ZN11Application11GetInstanceEv",0x6b6e10,reinterpret_cast<uint32_t>(&app));
        entry("_Z13Is1024x768Resv",0x403310,reinterpret_cast<uint32_t>(&no));
        // Scene-node/quaternion publication is outside this CPU input gate.
        // RotateYaw/RotatePitch retain their ORIGINAL scalar additions.
        entry("_ZN11CGameObject14UpdateRotationEv",0x581f10,reinterpret_cast<uint32_t>(&no));
        // This observation boundary records the movement fed by the ORIGINAL
        // UpdateFromInput CALL at0x233c4f. It does not claim physics/velocity output.
        entry("_ZN15PlayerComponent22ComputeControlVelocityEi",0x1f5630,reinterpret_cast<uint32_t>(&velocity));
        const auto event=image_.symbol("_ZN14CPlayerControl7onEventEPK6IEventPK12EventManager");
        const auto consume=image_.symbol("_ZN15PlayerComponent15UpdateFromInputEi");
        if(event!=image_.address(0x1ebe20,8962) || consume!=image_.address(0x233810,2130))
            throw std::runtime_error("original consumer identity mismatch");
        state.native_event_entry=event; state.native_consumer_entry=consume;
        // Actual unconditional button writer and neutral zoom branch from the
        // retained normal-interface interval run AFTER immediate delivery.
        button_update=image_.symbol("_ZN7CButton6UpdateEv");
        zoom_update=image_.symbol("_ZN21CLevelInterfaceNormal16UpdateZoomSliderEv");
        if(button_update!=image_.address(0x3db100,722) || zoom_update!=image_.address(0x364170,1028))
            throw std::runtime_error("original later writer identity mismatch");
        neutral_button=reinterpret_cast<uint32_t>(button_.data());
        *reinterpret_cast<uint32_t *>(button_.data())=image_.symbol("_ZTV7CButton")+8;
        normal_interface=read(state.level+0x17c);
        word(normal_interface+0x78,neutral_button);
        state.native_later_writer=reinterpret_cast<uint32_t>(&later);
        const auto call=image_.address(0x6c5352,5);
        uint32_t delta=read(call+1);
        if(*reinterpret_cast<uint8_t *>(call)!=0xe8 || call+5+delta!=image_.symbol("_ZN12EventManager12UpdateEventsEi"))
            throw std::runtime_error("original ordinary CALL admission mismatch");
        const uint8_t continuation[]{0x8b,0x86,0xb0,0x01,0x00,0x00,0x29,0xbe,0xf0,0x02,0x00,0x00};
        if(std::memcmp(reinterpret_cast<void *>(image_.address(0x21ca18,sizeof(continuation))),continuation,sizeof(continuation)))
            throw std::runtime_error("original immediate caller read/write mismatch");
        // Execute the ORIGINAL immediate caller instruction window, copied from
        // this loaded image, not a C approximation. ESI/EDI are established by
        // Player::Update's retained prologue (t07-player-update lines966..979).
        // Only the relative CALL is relocated to the isolated look observer.
        // The source image, its caller and its epilogue are NOT patched.
        const uint8_t setup[]{0x55,0x89,0xe5,0x56,0x57,0x83,0xec,0x10,
            0x8b,0x75,0x08,0x8b,0x7d,0x0c};
        const uint8_t finish[]{0x83,0xc4,0x10,0x5f,0x5e,0x5d,0xc3};
        std::vector<uint8_t> code(setup,setup+sizeof(setup));
        const auto window=image_.address(0x21ca00,0x24);
        const auto *bytes=reinterpret_cast<const uint8_t *>(window);
        code.insert(code.end(),bytes,bytes+0x24);
        code.insert(code.end(),finish,finish+sizeof(finish));
        if(bytes[0]!=0x8b || bytes[1]!=0x06 || bytes[9]!=0xff ||
           bytes[10]!=0x50 || bytes[11]!=0x58 || bytes[0x13]!=0xe8 ||
           window+0x18+read(window+0x14)!=image_.symbol("_ZN15PlayerComponent12UpdateLookAtEi"))
            throw std::runtime_error("original immediate caller CALL-window mismatch");
        caller_=VirtualAlloc(nullptr,code.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(!caller_) throw std::runtime_error("isolated original caller window allocation");
        const auto look=fixture.symbol("fixture_look_at");
        const auto consume_bridge=fixture.symbol("fixture_consume");
        const auto table=fixture.symbol("_ZTV15PlayerComponent")+8;
        if(!look || !consume_bridge) throw std::runtime_error("caller bridge symbol");
        const auto delta_look=look-reinterpret_cast<uint32_t>(caller_)-sizeof(setup)-0x18;
        const auto delta32=static_cast<uint32_t>(delta_look);
        std::memcpy(code.data()+sizeof(setup)+0x14,&delta32,4);
        std::memcpy(caller_,code.data(),code.size());
        DWORD old=0;
        if(!VirtualProtect(caller_,code.size(),PAGE_EXECUTE_READ,&old) ||
           !FlushInstructionCache(GetCurrentProcess(),caller_,code.size()))
            throw std::runtime_error("isolated original caller window protection");
        word(table+0x58,consume_bridge);
        state.native_caller_entry=reinterpret_cast<uint32_t>(caller_);

    }
    ~Gate() {
        world->native_event_entry=world->native_consumer_entry=world->native_caller_entry=world->native_later_writer=0;
        writes_.clear(); if(caller_) VirtualFree(caller_,0,MEM_RELEASE); world=nullptr;
    }
};
}
