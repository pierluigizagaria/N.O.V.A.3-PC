#pragma once
#include <atomic>

namespace nova::platform {
// GL publishes only after a successful first swap; the UI owns window reveal.
// Cancellation releases a parked renderer even when window reveal fails.
class StartupPresentation {
public:
    enum class State { preparing, frame_ready, visible, cancelled };
    void presented() {
        auto expected=State::preparing;
        state_.compare_exchange_strong(expected,State::frame_ready);
    }
    bool needs_reveal() const { return state_.load()==State::frame_ready; }
    void revealed() {
        auto expected=State::frame_ready;
        state_.compare_exchange_strong(expected,State::visible);
    }
    void cancel() { state_=State::cancelled; }
    bool waiting() const { return state_.load()==State::frame_ready; }
    bool visible() const { return state_.load()==State::visible; }
private:
    std::atomic<State> state_{State::preparing};
};
}
