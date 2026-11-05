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
    NativeScale(RequestedScale&,const std::array<uint32_t,6>&,uint32_t height=0);
    static uint64_t fixture_viewport(uint32_t,uint32_t);
    static uint64_t fixture_projection(uint32_t);
    static uint64_t fixture_screen(uint32_t);
    static uint64_t fixture_pause(uint32_t);
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
