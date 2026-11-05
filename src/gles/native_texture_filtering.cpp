#include "gles/native_texture_filtering.h"
#include "gles/runtime.h"
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nova::gles {
namespace {
constexpr auto name="_ZNK6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE8CTexture16updateParametersEv";
template<class T> T read(uint32_t address,uint32_t offset) { return *reinterpret_cast<const T *>(address+offset); }
}
NativeTextureFiltering *NativeTextureFiltering::instance_=nullptr;
NativeTextureFiltering::NativeTextureFiltering(const ElfImage &image,int quality):quality_(quality) {
    if(quality<0 || quality>2) throw std::invalid_argument("Invalid texture filtering quality");
    if(instance_) throw std::runtime_error("Native texture filtering already installed");
    hook_=std::make_unique<abi::GuestHook>(image,name,
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&update));
    instance_=this;
}
NativeTextureFiltering::~NativeTextureFiltering() { hook_.reset();instance_=nullptr; }
void __cdecl NativeTextureFiltering::update(uint32_t texture) {
    auto &self=*instance_;
    if(!self.quality_ || !current_context()) { abi::invoke(self.hook_->original(),{texture});return; }
    // The checked native entry already owns this texture's current binding.
    // Snapshot requested state; the original clears its dirty bits, not our policy.
    const auto shared=read<uint32_t>(texture,0x10);
    const auto flags=read<uint32_t>(shared,0x24);
    const auto dirty=read<uint16_t>(shared,0x2a);
    const auto mips=read<uint8_t>(shared,0x2f);
    const auto anisotropy=read<float>(shared,0x30);
    abi::invoke(self.hook_->original(),{texture});
    if((flags&7)!=1 || !(dirty&0x108)) return; // 2D, min-filter or anisotropy update.
    const auto filter=(flags>>14)&7;
    const bool eligible=mips>1 && (filter==3 || filter==5);
    if(eligible) ::glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    if(self.quality_!=2) return;
    const float maximum=current_context()->max_anisotropy;
    if(maximum<1) return; // Never issue the extension token without support.
    if(eligible) ::glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,std::min(16.f,maximum));
    else if((dirty&8) && std::isfinite(anisotropy) && anisotropy>=1)
        // A texture may switch to a non-mip/nearest sampler; retire our override.
        ::glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,std::min(anisotropy,maximum));
}
}
