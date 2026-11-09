#include "platform/native_logo.h"
#include <algorithm>
#include <stdexcept>

namespace nova::platform {
NativeLogo *NativeLogo::instance_=nullptr;
NativeLogo::NativeLogo(const ElfImage &image,Now now):now_(now) {
    if(instance_ || !now_) throw std::logic_error("Invalid native logo adapter ownership");
    paint_=std::make_unique<abi::GuestCall>(image,"_ZN7GS_Logo6RenderEv",0xc6,
        "_ZNK7CSprite10PaintFrameEiffjffh",reinterpret_cast<uint32_t>(&paint));
    instance_=this;
}
NativeLogo::~NativeLogo() { paint_.reset(); instance_=nullptr; }
void NativeLogo::visible() {
    if(!visible_) { start_=now_(); visible_=true; }
}
uint32_t __cdecl NativeLogo::paint(uint32_t sprite,uint32_t frame,uint32_t x,uint32_t y,
                                  uint32_t flags,uint32_t sx,uint32_t sy,uint32_t alpha) {
    auto &self=*instance_;
    const double elapsed=self.visible_ ? std::chrono::duration<double>(self.now_()-self.start_).count() : 0;
    // Provisional PC opening: 650 ms smooth fade-in of the supplied static logo.
    // No media player, asset substitution, native timer write or loading delay.
    const double t=std::clamp(elapsed/.650,0.,1.);
    const auto opacity=static_cast<uint32_t>((alpha&255u)*(t*t*(3.-2.*t))+.5);
    return abi::invoke(self.paint_->original(),{sprite,frame,x,y,flags,sx,sy,opacity});
}
}
