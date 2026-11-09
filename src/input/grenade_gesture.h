#pragma once
#include <cstdint>

namespace nova::input {
enum class GrenadeCommand { none, throw_grenade, detonate };
class GrenadeGesture {
    bool pending_=false,consumed_=false;
    uint64_t started_=0,epoch_=0;
  public:
    // Provisional PC gesture threshold: one deliberate hold, never a repeat.
    static constexpr uint64_t hold_ms=350;
    void cancel() { pending_=consumed_=false; }
    GrenadeCommand sample(bool down,bool pressed,uint64_t now,uint64_t epoch) {
        if(epoch!=epoch_) { cancel();epoch_=epoch; }
        if(pressed && !pending_ && !consumed_) { pending_=true;started_=now; }
        if(consumed_) { if(!down) consumed_=false;return GrenadeCommand::none; }
        if(!pending_) return GrenadeCommand::none;
        if(now-started_>=hold_ms) {
            pending_=false;consumed_=down;return GrenadeCommand::detonate;
        }
        if(!down) { pending_=false;return GrenadeCommand::throw_grenade; }
        return GrenadeCommand::none;
    }
};
}
