#include "platform/native_detail.h"
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nova::platform {
NativeDetail *NativeDetail::instance_=nullptr;
NativeDetail::NativeDetail(const ElfImage &image,int multiplier):multiplier_(multiplier) {
    if(multiplier<1 || multiplier>4) throw std::invalid_argument("lod_distance must be an integer from 1 to 4");
    if(instance_) throw std::runtime_error("Native detail adapter already installed");
    constexpr auto setter="_ZN21CCustomColladaFactory16SetLodsDistScaleEf";
    setter_=abi::require_function(image,setter);
    // All three original producers: level setup, per-frame FOV compensation,
    // and the authored/script setter. No shared global is permanently replaced.
    constexpr std::array<const char *,3> owners{
        "_ZN6CLevel7PreInitEi", "_ZN6CLevel6UpdateEv", "_ZN6CLevel16SetLodsDistScaleEf"};
    constexpr std::array<uint32_t,3> offsets{0x11c,0x24ee,0x22};
    for(size_t i=0;i<calls_.size();++i)
        calls_[i]=std::make_unique<abi::GuestCall>(image,owners[i],offsets[i],setter,
            reinterpret_cast<uint32_t>(&set_distance));
    instance_=this;
}
NativeDetail::~NativeDetail() {
    for(auto &call:calls_) call.reset();
    instance_=nullptr;
}
void __cdecl NativeDetail::set_distance(uint32_t factory,float native_scale) {
    const auto &self=*instance_;
    // Native code squares this value. Keep zero/negative/nonfinite inputs exact
    // and avoid overflowing its squared result. Never compound a prior frame.
    const float enlarged=native_scale*static_cast<float>(self.multiplier_);
    if(self.multiplier_!=1 && native_scale>0 && std::isfinite(enlarged) &&
       enlarged<=std::sqrt(std::numeric_limits<float>::max())) native_scale=enlarged;
    abi::invoke(self.setter_,{factory,std::bit_cast<uint32_t>(native_scale)});
}
}
