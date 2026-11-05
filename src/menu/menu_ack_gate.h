#pragma once
#include <cstdint>
// Host-only, default-OFF diagnostic proposal. No guest calls or writes.
// Feed one fresh snapshot after a complete ordinary step while GL is parked.
namespace nova::diagnostic::t09h {
enum class State { unknown, logo, intro, load_menu, splash, menu, other };
enum class Action { wait, dispatch_balanced_pair, stop_observed, stop_failed };
enum class Reason { none, unsafe_snapshot, unexpected_state, startup_timeout,
    unsupported_splash, owner_changed, delivery_failed, draw_timeout, observed };
struct Snapshot {
    std::uint64_t frame=0, splash_identity=0, manager_identity=0, generation=0;
    State state=State::unknown;
    bool valid=false, player_absent=false, gameplay_absent=false;
    bool underlying_menu=false, moga_clear=false, acknowledgement_clear=false;
    bool viewport_stable=false, no_other_input=false, cpu_complete=false;
    bool two_targets_registered=false, targets_healthy=false;
    bool any_begin=false, any_return_with_events=false;
};
struct Decision { Action action; Reason reason; };
class Gate {
    enum class Phase { waiting, dispatch_pending, released, terminal };
    Phase phase_=Phase::waiting;
    std::uint64_t released_=0, splash_=0, manager_=0, generation_=0;
    Decision terminal_{Action::wait,Reason::none};
    Decision fail(Reason r) noexcept {
        phase_=Phase::terminal;return terminal_={Action::stop_failed,r};
    }
public:
    // Exactly two SDL events dispatched under one barrier, native action1 then0.
    // False/partial delivery requires existing owned-input cleanup while GL stays
    // parked in the same Splash, then unsuccessful ordinary shutdown. No retry.
    bool delivered_pair(bool complete) noexcept {
        if(phase_!=Phase::dispatch_pending || !complete) {
            fail(Reason::delivery_failed);return false;
        }
        phase_=Phase::released;return true;
    }
    Decision observe(const Snapshot& s) noexcept {
        if(phase_==Phase::terminal)return terminal_;
        if(!s.valid || !s.player_absent || !s.gameplay_absent || !s.no_other_input)
            return fail(Reason::unsafe_snapshot);
        if(phase_==Phase::dispatch_pending)return fail(Reason::delivery_failed);
        if(s.frame>=1200)return fail(Reason::startup_timeout);
        if(phase_==Phase::waiting) {
            if(s.frame>=1200)return fail(Reason::startup_timeout);
            if(s.state==State::logo || s.state==State::intro || s.state==State::load_menu)
                return {Action::wait,Reason::none};
            if(s.state!=State::splash)return fail(Reason::unexpected_state);
            if(s.frame<10 || !s.underlying_menu || !s.moga_clear ||
               !s.acknowledgement_clear || !s.viewport_stable || !s.cpu_complete ||
               !s.two_targets_registered || !s.targets_healthy || s.any_begin ||
               !s.splash_identity || !s.manager_identity || !s.generation)
                return fail(Reason::unsupported_splash);
            splash_=s.splash_identity;manager_=s.manager_identity;generation_=s.generation;
            released_=s.frame;phase_=Phase::dispatch_pending;
            return {Action::dispatch_balanced_pair,Reason::none};
        }
        if(s.manager_identity!=manager_ || s.generation!=generation_ ||
           !s.targets_healthy || !s.two_targets_registered)
            return fail(Reason::owner_changed);
        if(s.frame<=released_)return fail(Reason::unsafe_snapshot);
        if(s.state!=State::splash && s.state!=State::menu)
            return fail(Reason::unexpected_state);
        if(s.state==State::splash && s.splash_identity!=splash_)
            return fail(Reason::owner_changed);
        if(s.state==State::menu && s.any_return_with_events) {
            phase_=Phase::terminal;
            return terminal_={Action::stop_observed,Reason::observed};
        }
        if(s.frame-released_>=120)return fail(Reason::draw_timeout);
        return {Action::wait,Reason::none};
    }
};
} // namespace nova::diagnostic::t09g
