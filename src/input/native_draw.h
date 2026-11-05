#pragma once
#include "abi/guest_call.h"
#include <span>

namespace nova::input {
// Called only by the checked CLevel rush-overlay call-site replacement, never
// by a global sprite hook. The owner continues its original color/clip cleanup.
inline void draw_rush_overlay(bool gameplay, uint32_t original, std::span<const uint32_t,8> args) {
    if (gameplay && args[1] == 57) return;
    abi::call_guest(original,args.data(),args.size());
}
}
