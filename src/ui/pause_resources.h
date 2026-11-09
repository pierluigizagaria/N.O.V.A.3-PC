#pragma once
#include "abi/guest_hook.h"
#include "ui/menu_sprite_layout.h"
#include <array>
#include <vector>
#include <stdexcept>

namespace nova::ui {
// Borrow shared metrics while a native UI owner draws or constructs its canvas.
// Other owners must see their original font and sprite metrics afterwards.
class PauseResources {
    struct Saved { uint32_t object;std::array<float,2> scale;bool frames; };
    std::vector<Saved> saved_;
    uint32_t compute_;
    void rebuild(uint32_t object) const {
        for(unsigned i=0;i<4;++i)abi::invoke(compute_,{object});
    }
    void set(uint32_t object,float scale,bool frames) {
        for(const auto& s:saved_)if(s.object==object)return;
        Saved saved{object,{},frames};
        std::memcpy(saved.scale.data(),reinterpret_cast<void*>(object+0xb0),8);
        saved_.push_back(saved);
        const std::array<float,2> value{scale,scale};
        std::memcpy(reinterpret_cast<void*>(object+0xb0),value.data(),8);
        if(frames)rebuild(object);
    }
public:
    explicit PauseResources(uint32_t compute):compute_(compute) {}
    void font(uint32_t object,unsigned pixels) {
        uint32_t ttf=0,size=0;
        std::memcpy(&ttf,reinterpret_cast<void*>(object+0xc8),4);
        if(ttf)std::memcpy(&size,reinterpret_cast<void*>(ttf+0x54),4);
        if(!pixels || pixels>256 || !size || size>256)
            throw std::runtime_error("Pause font has invalid native metrics");
        set(object,float(pixels)/size,false);
    }
    void icons(uint32_t object) {
        const auto scale=pause_sprite_scale(object,1280,720);
        if(scale)set(object,*scale,true);
    }
    void icons_at_layout(uint32_t object,float layout) {
        const auto density=menu_sprite_density(object);
        if(!density)return;
        const auto scale=layout / *density;
        std::array<float,2> current;
        std::memcpy(current.data(),reinterpret_cast<void*>(object+0xb0),8);
        if(current!=std::array<float,2>{scale,scale})set(object,scale,true);
    }
    ~PauseResources() {
        for(auto it=saved_.rbegin();it!=saved_.rend();++it) {
            std::memcpy(reinterpret_cast<void*>(it->object+0xb0),it->scale.data(),8);
            if(it->frames)rebuild(it->object);
        }
    }
    PauseResources(const PauseResources&)=delete;
};
}
