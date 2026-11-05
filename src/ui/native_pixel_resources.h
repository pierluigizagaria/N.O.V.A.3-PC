#pragma once
#include "loader/elf_image.h"
#include <memory>
#include <cstdint>
namespace nova::ui {
// Canonical logical sprite/font metrics, independent of startup resolution.
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
