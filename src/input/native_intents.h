#pragma once
namespace nova::input {
// Covers the complete adapter invocation, including guest queries before the
// validated action boundary and after original Update. Thread-local depth is
// supplied by its caller; this introduces no cross-thread lock or lifetime grant.
class NativeInputInvocation {
    unsigned &depth_;
    bool outer_;
  public:
    explicit NativeInputInvocation(unsigned &depth) : depth_(depth),outer_(depth++==0) {}
    ~NativeInputInvocation() { --depth_; }
    NativeInputInvocation(const NativeInputInvocation &)=delete;
    NativeInputInvocation &operator=(const NativeInputInvocation &)=delete;
    bool outer() const { return outer_; }
};
// Physical desire is independent of native permission. No accepted-action
// shadow, simulated button cadence, animation clock or timeout belongs here.
// Fresh native current-slot identity, never a host accepted-reload cache.
inline bool native_reload_blocks_aim(bool pending,int current,int configured) {
    return pending || (configured>=0 && current>=0 && current==configured);
}
class NativeAimIntent {
    bool wanted_=false,blocked_=false,reload_=false;
  public:
    void cancel() { *this={}; }
    void cancel_input() { wanted_=false; reload_=false; }
    void block_until_release() { blocked_=true; cancel_input(); }
    void sample(bool held) {
        if(!held) blocked_=false;
        wanted_=held && !blocked_;
    }
    bool button() const { return wanted_; }
    bool wanted() const { return wanted_; }
    bool permit_start(bool native_allowed,bool native_submitted,bool enabled,bool hud_enabled,bool native_reload_busy=false) const {
        return wanted_ && !reload_ && !native_reload_busy && enabled && hud_enabled && native_allowed && !native_submitted;
    }
    bool permit_exit() const { return !wanted_ && !reload_; }
    // A tap during known native ADS entry waits for that transition, not a
    // guessed number of milliseconds. Transfer once when the live branch says
    // ADS is no longer entering; context/weapon/input cancellation discards it.
    void offer_reload(bool edge) { reload_|=edge; }
    bool reload_pending() const { return reload_; }
    void cancel_reload() { reload_=false; }
    bool take_reload() { const bool pending=reload_; reload_=false; return pending; }
};
}
