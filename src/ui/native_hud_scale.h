#pragma once
#include "loader/elf_image.h"
#include "ui/scale.h"
#include <memory>
namespace nova::ui {
class NativeHudScale {
public:
    NativeHudScale(const ElfImage&,RequestedScale&);
    ~NativeHudScale();
#ifdef NOVA_UI_SCALE_FIXTURE
    struct Bindings { std::array<uint32_t,10> functions; uint32_t device,width,height,clip; uint32_t player_component=0,is_sniper=0; };
    NativeHudScale(RequestedScale&,Bindings);
    static uint64_t fixture_hud(uint32_t);
    static uint64_t fixture_frame(const std::array<uint32_t,8>&);
    static uint64_t fixture_font(const std::array<uint32_t,10>&);
    static uint64_t fixture_radar(uint32_t);
    static uint64_t fixture_crosshair(uint32_t,uint32_t,uint32_t);
    static uint64_t fixture_sprite(unsigned,const std::array<uint32_t,10>&);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
