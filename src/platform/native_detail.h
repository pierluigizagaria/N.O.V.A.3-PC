#pragma once
#include "abi/guest_hook.h"
#include <array>
#include <memory>

namespace nova::platform {
// Provisional PC default: keep authored mesh detail four times farther away.
// 1 restores the original distance policy, including FOV compensation.
inline constexpr int default_lod_distance = 4;
class NativeDetail {
public:
    NativeDetail(const ElfImage &, int multiplier=default_lod_distance);
    ~NativeDetail();
    NativeDetail(const NativeDetail &)=delete;
    NativeDetail &operator=(const NativeDetail &)=delete;
private:
    static void __cdecl set_distance(uint32_t factory, float native_scale);
    static NativeDetail *instance_;
    int multiplier_;
    uint32_t setter_=0;
    std::array<std::unique_ptr<abi::GuestCall>,3> calls_;
};
}
