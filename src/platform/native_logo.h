#pragma once
#include "abi/guest_hook.h"
#include <chrono>
#include <memory>

namespace nova::platform {
// Host presentation policy only: the original sprite, state, timer and loading
// remain native. Time starts after the first frame has actually been revealed.
class NativeLogo {
public:
    using Clock=std::chrono::steady_clock;
    using Now=Clock::time_point (*)();
    explicit NativeLogo(const ElfImage &,Now now=&Clock::now);
    ~NativeLogo();
    void visible(); // Render lane, after the UI has acknowledged first reveal.
private:
    static uint32_t __cdecl paint(uint32_t,uint32_t,uint32_t,uint32_t,
                                  uint32_t,uint32_t,uint32_t,uint32_t);
    static NativeLogo *instance_;
    std::unique_ptr<abi::GuestCall> paint_;
    Now now_;
    Clock::time_point start_{};
    bool visible_=false;
};
}
