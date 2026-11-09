#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace nova::ui {
struct PauseExtent {
    uint32_t width=0,height=0;
    bool operator==(const PauseExtent&) const = default;
    bool valid() const { return width>=640 && width<=3840 && uint64_t(width)*9==uint64_t(height)*16; }
    std::array<int16_t,2> unproject(std::array<int16_t,2> point,PauseExtent physical) const {
        if(!valid() || !physical.valid())throw std::runtime_error("Invalid pause canvas extent");
        // Preserve out-of-bounds points; clamping could activate an edge button.
        return {static_cast<int16_t>(int64_t(point[0])*width/physical.width),
                static_cast<int16_t>(int64_t(point[1])*height/physical.height)};
    }
};
// Only logical dimensions change. Target allocation and the GL viewport stay
// physical, so native set2DProjection rasterizes the retained UI at full density.
class PauseMetricsScope {
public:
    PauseMetricsScope(uint32_t width,uint32_t height,uint32_t target,PauseExtent canvas)
        :width_(reinterpret_cast<uint32_t*>(width)),height_(reinterpret_cast<uint32_t*>(height)),
         rect_(reinterpret_cast<uint32_t*>(target+0x14)),saved_{*width_,*height_} {
        if(!canvas.valid())throw std::runtime_error("Invalid retained pause canvas");
        std::memcpy(rect_saved_.data(),rect_,sizeof rect_saved_);
        *width_=canvas.width;*height_=canvas.height;
        const std::array<uint32_t,4> logical{0,0,canvas.width,canvas.height};
        std::memcpy(rect_,logical.data(),sizeof logical);
    }
    ~PauseMetricsScope() {
        std::memcpy(rect_,rect_saved_.data(),sizeof rect_saved_);
        *width_=saved_.width;*height_=saved_.height;
    }
    PauseMetricsScope(const PauseMetricsScope&)=delete;
private:
    uint32_t *width_,*height_,*rect_;
    PauseExtent saved_;
    std::array<uint32_t,4> rect_saved_{};
};
}
