#pragma once
#include "abi/guest_hook.h"
#include <memory>
namespace nova::gles {
class NativeTextureFiltering {
public:
    NativeTextureFiltering(const ElfImage &,int quality=2);
    ~NativeTextureFiltering();
    NativeTextureFiltering(const NativeTextureFiltering &)=delete;
    NativeTextureFiltering &operator=(const NativeTextureFiltering &)=delete;
private:
    static void __cdecl update(uint32_t texture);
    static NativeTextureFiltering *instance_;
    int quality_;
    std::unique_ptr<abi::GuestHook> hook_;
};
}
