#pragma once
#include "loader/elf_image.h"
#include <memory>
#include <cstdint>
namespace nova::ui {
// 720p logical metrics for the two HUD sprite families, independent of startup
// resolution. Shared menu assets, fonts and weapon overlays stay native.
// Artwork density remains owned by NativeArtwork.
class NativePixelResources {
public:
    explicit NativePixelResources(const ElfImage&);
    ~NativePixelResources();
    static uint32_t load(uint32_t original,const uint32_t* args);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
