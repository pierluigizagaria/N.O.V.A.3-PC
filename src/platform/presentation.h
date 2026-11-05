#pragma once
#include <cstdint>
#include <stdexcept>

namespace nova::platform {
struct PresentationRect { int x=0,y=0,w=0,h=0; };
// Integer pixels, top-left origin. Shared by GL output and inverse UI touches.
inline PresentationRect fit_presentation(int width,int height,int guest_width,int guest_height) {
    if(guest_width<=0 || guest_height<=0) throw std::invalid_argument("Invalid guest presentation dimensions");
    PresentationRect rect;
    if(width<=0 || height<=0) return rect;
    if(int64_t(width)*guest_height<=int64_t(height)*guest_width) {
        rect.w=width; rect.h=static_cast<int>(int64_t(width)*guest_height/guest_width);
    } else {
        rect.h=height; rect.w=static_cast<int>(int64_t(height)*guest_width/guest_height);
    }
    rect.x=(width-rect.w)/2; rect.y=(height-rect.h)/2;
    return rect;
}
} // namespace nova::platform
