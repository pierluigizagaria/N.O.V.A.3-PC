#pragma once
#include "loader/elf_image.h"
#include <array>
#include <memory>

namespace nova::ui {
// Extra raster samples, with native glyph metrics and destination geometry.
class NativeMenuFonts {
public:
    explicit NativeMenuFonts(const ElfImage& image);
    ~NativeMenuFonts();
#ifdef NOVA_MENU_FONT_FIXTURE
    struct Bindings { std::array<uint32_t,7> functions; uint32_t construct,set_size,load; };
    explicit NativeMenuFonts(Bindings bindings);
    static uint64_t fixture_cache(const std::array<uint32_t,10>& args);
    static uint64_t fixture_draw(const std::array<uint32_t,7>& args,bool smooth=false);
    static uint64_t fixture_rectangle(const std::array<uint32_t,5>& args);
    static uint64_t fixture_destroy(uint32_t glyph,bool alternate=false);
    static uint64_t fixture_clear(uint32_t atlas);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
