#pragma once
#include "loader/elf_image.h"
#include "ui/scale.h"
#include <memory>
namespace nova::ui {
// Owns CHud's checked drawNode call and matching HUD button coordinates,
// coexisting with the controls hook on CHud::Render's entry.
class NativeHudNodes {
public:
    NativeHudNodes(const ElfImage&,RequestedScale&);
    ~NativeHudNodes();
#ifdef NOVA_UI_SCALE_FIXTURE
    struct Bindings { uint32_t draw,find,set_scale,update,release,height; };
    NativeHudNodes(RequestedScale&,Bindings);
    static uint64_t fixture_draw(uint32_t,uint32_t);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
