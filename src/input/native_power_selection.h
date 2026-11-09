#pragma once
#include "abi/guest_hook.h"
#include <cstdint>
#include <stdexcept>

namespace nova::input {
// Start the original released-swipe transition. Native Update owns the travel,
// selection commit, callback and wrap; keyboard input supplies only direction.
inline bool animate_power_selection(uint32_t button,int direction) {
    if(!button || (direction!=1 && direction!=-1) || !*reinterpret_cast<const uint8_t*>(button+0xba))return false;
    const auto word=[&](uint32_t offset){return *reinterpret_cast<const uint32_t*>(button+offset);};
    const auto begin=word(0x84),end=word(0x88);
    if(end<begin || (end-begin)%44 || (end-begin)/44>128 || (!begin && end))
        throw std::runtime_error("Native animated power frame layout mismatch");
    if(end-begin<88 || int32_t(word(0xa0))!=0)return false;
    const auto threshold=int32_t(word(0xb4));
    if(threshold<=0 || threshold>32767)throw std::runtime_error("Native power swipe threshold mismatch");
    *reinterpret_cast<int32_t*>(button+0xa0)=direction>0?-threshold:threshold;
    return true;
}
// Original sweep widgets own the available choices and their order. Their
// frame IDs are manager slot IDs, not necessarily contiguous frame indices.
// Call only for the current normal interface and an enabled native button.
inline bool cycle_power_selection(uint32_t button,bool hud,int direction,
                                  uint32_t set_selection,uint32_t callback) {
    if(!button || (direction!=1 && direction!=-1)) return false;
    if(!hud && !*reinterpret_cast<const uint8_t *>(button+0xba)) return false;
    const auto word=[](uint32_t p,uint32_t offset) {
        return *reinterpret_cast<const uint32_t *>(p+offset);
    };
    const auto begin=word(button,hud ? 0x98 : 0x84);
    const auto end=word(button,hud ? 0x9c : 0x88);
    const uint32_t stride=hud ? 4u : 0x2cu;
    if(end<begin || (end-begin)%stride || (end-begin)/stride>128 || (!begin && end))
        throw std::runtime_error("Native power selection frame layout mismatch");
    const auto count=static_cast<int>((end-begin)/stride);
    if(count<2) return false;
    const auto current=static_cast<int32_t>(word(button,hud ? 0xb4 : 0xa4));
    if(current < -1 || current>=count)
        throw std::runtime_error("Native power selection index mismatch");
    int index=current==-1 ? (direction>0 ? count-1 : 0) : current;
    for(int visited=0;visited<count;++visited) {
        index=(index+direction+count)%count;
        if(index==current) return false;
        const auto frame=begin+static_cast<uint32_t>(index)*stride;
        if(!hud && *reinterpret_cast<const uint8_t *>(frame+0x10)) continue;
        const auto id=word(frame,hud ? 0 : 0x18);
        if(id==UINT32_MAX) continue;
        // The original touch Update also selects before calling the callback.
        // Callback retains native sound, manager notification and tutorial flags.
        abi::invoke(set_selection,{button,id});
        abi::invoke(callback,{button,id});
        return true; // No button/owner access after callback-capable continuation.
    }
    return false;
}
}
