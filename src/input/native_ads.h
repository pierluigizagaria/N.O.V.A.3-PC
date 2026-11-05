#pragma once
#include <cstdint>
#include <cmath>

namespace nova::input {
enum class NativeAds { unknown, inactive, pending, active };
// Only call inside an already-owned live normal-player input consumer. This
// resolves current native storage; it supplies no render/lifetime eligibility.
// GetWeaponComponent (0x263510) proves the 24-byte vector record layout.
// SwitchAim (0x6a2807) consumes +0x61; Weapon::Update (0x194df4) publishes it.
// +0x60 is submission, not active ADS. No input request or host shadow is read.
template<class Read32,class Read8>
NativeAds current_native_ads(uint32_t player,Read32 read32,Read8 read8) {
    if(!player) return NativeAds::unknown;
    const auto manager=read32(player,0x170);
    if(!manager) return NativeAds::unknown;
    const auto index=static_cast<int32_t>(read32(manager,0x160));
    const auto begin=read32(manager,0x148),end=read32(manager,0x14c);
    if(index<0 || !begin || end<begin || (end-begin)%24 ||
       static_cast<uint32_t>(index)>=(end-begin)/24) return NativeAds::unknown;
    const auto weapon=read32(begin,static_cast<uint32_t>(index)*24+12);
    if(!weapon) return NativeAds::unknown;
    if(read8(weapon,0x61)) return NativeAds::active;
    return read8(weapon,0x60) ? NativeAds::pending : NativeAds::inactive;
}
struct NativeAimScale { float factor=1.f; bool aiming=false; };
// Aim publishes player+2c BEFORE pending+60; native sniper zoom updates the
// same field. UnAim/StartRush restore 1. Consume the native field even during
// entry: active+61 alone would delay scaling until the animation completes.
template<class Read32,class Read8,class ReadFloat>
NativeAimScale current_native_aim_scale(uint32_t player,Read32 read32,Read8 read8,ReadFloat read_float) {
    if(!player) return {};
    const auto factor=read_float(player,0x2c);
    const auto state=current_native_ads(player,read32,read8);
    return {std::isfinite(factor) && factor>=0 ? factor : 1.f,
        state==NativeAds::pending || state==NativeAds::active};
}
}
