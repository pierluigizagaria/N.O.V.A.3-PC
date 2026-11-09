#pragma once
#include "loader/elf_image.h"
#include "ui/scale.h"
#include <memory>
namespace nova::ui {
// Quiescent installation before guest initializers. No game startup is needed
// to validate the exact original hook sites.
class NativeScale {
public:
    NativeScale(const ElfImage&,RequestedScale&);
    ~NativeScale();
#ifdef NOVA_UI_SCALE_FIXTURE
    struct PauseBindings {
        uint32_t create=0,update=0,release=0,app=0,current=0,device=0,width=0,height=0,touch=0;
        std::array<uint32_t,4> events{};
        std::array<uint32_t,4> confirmation{};
    };
    NativeScale(RequestedScale&,const std::array<uint32_t,6>&,uint32_t height=0);
    void fixture_pause_bindings(PauseBindings);
    static uint64_t fixture_create(uint32_t,bool confirmation=false);
    static uint64_t fixture_update(uint32_t,bool confirmation=false);
    static uint64_t fixture_release(uint32_t,bool confirmation=false);
    static uint64_t fixture_draw_begin();
    static uint64_t fixture_touch(unsigned,uint32_t,uint32_t,uint32_t);
    static uint64_t fixture_viewport(uint32_t,uint32_t);
    static uint64_t fixture_projection(uint32_t);
    static uint64_t fixture_screen(uint32_t);
    static uint64_t fixture_pause(uint32_t,bool confirmation=false);
    static uint64_t fixture_manager(uint32_t);
    static uint64_t fixture_hit(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
#endif
    NativeScale(const NativeScale&)=delete;
    NativeScale& operator=(const NativeScale&)=delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
