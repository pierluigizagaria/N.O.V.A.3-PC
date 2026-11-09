#pragma once
#include <algorithm>
#include <cstring>
#include <optional>
#include <string_view>
#include <cstdint>

namespace nova::ui {
// These are logical descriptor names, not the substituted HD texture slots.
// NativeArtwork changes sampled rectangles/textures, never this name list.
inline std::optional<float> menu_descriptor_density(std::string_view name) {
    // GS_InGameMenu::Create requests Chatboard.bsprite, not the similarly
    // named menu_elements_nova3 resource used by the front-end menu.
    if(name=="chatboard_960x640.tga")return 1.f;
    if(name=="chatboard_1720x1280.tga")return 2.f;
    if(name=="ranks_960x640.tga")return 1.f;
    if(name=="ranks_1720x1280.tga")return 2.f;
    if(name=="game_actions_icons_960x640.tga")return 1.f;
    if(name=="game_actions_icons_1720x1280.tga")return 2.f;
    if(name=="game_actions_icons.tga" || name=="upgrades.tga")return .5f;
    if(name=="upgrades_960x640.tga")return 1.f;
    if(name=="upgrades_1720x1280.tga")return 2.f;
    return {};
}
inline std::optional<float> menu_sprite_density(uint32_t sprite) {
    if(!sprite)return {};
    const auto word=[](uint32_t p) {uint32_t value;std::memcpy(&value,reinterpret_cast<void*>(p),4);return value;};
    const auto begin=word(sprite+0xa0),end=word(sprite+0xa4);
    if(!begin || end<=begin || end-begin>512 || (end-begin)%4)return {};
    const auto name=word(begin);
    if(!name)return {};
    return menu_descriptor_density(reinterpret_cast<const char*>(name));
}
inline std::optional<float> pause_sprite_scale(uint32_t sprite,unsigned width,unsigned height) {
    if(!width || !height)return {};
    const auto density=menu_sprite_density(sprite);
    if(!density)return {};
    return std::min(width/960.f,height/640.f)/(*density);
}
inline bool prepare_pause_sprite(uint32_t sprite,unsigned width,unsigned height) {
    const auto scale=pause_sprite_scale(sprite,width,height);
    if(!scale)return false;
    std::memcpy(reinterpret_cast<void*>(sprite+0xb0),&*scale,4);
    std::memcpy(reinterpret_cast<void*>(sprite+0xb4),&*scale,4);
    return true;
}
template<class Compute>
inline uint64_t rebuild_pause_sprite(uint32_t sprite,unsigned width,unsigned height,Compute compute) {
    const bool prepared=prepare_pause_sprite(sprite,width,height);
    const auto result=compute(sprite);
    // ComputeFrameRects traverses ascending IDs, reading cached nested bounds.
    // Resolve forward references with bounded original passes and rounding.
    // The real-resource regression verifies the complete cache is stable.
    if(prepared)for(unsigned pass=1;pass<4;++pass)compute(sprite);
    return result;
}
}
