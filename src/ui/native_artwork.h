#pragma once
#include "loader/elf_image.h"
#include <memory>
#include <array>
namespace nova::ui {
class NativeArtwork {
public:
    explicit NativeArtwork(const ElfImage&);
    ~NativeArtwork();
    // Share the PaintModule boundary already owned by NativeHudScale. The view
    // lives for one synchronous native draw and never owns native allocations.
    static uint32_t prepare_draw(uint32_t sprite,uint32_t module,uint32_t& palette,
                                 std::array<uint32_t,50>& view);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
