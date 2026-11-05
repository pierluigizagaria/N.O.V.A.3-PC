#pragma once
#include "abi/guest_hook.h"
#include "movement_wobble/movement_wobble.h"
#include <memory>
namespace nova::movement_wobble {
// Install/remove only while guest execution is quiescent. Reduce only authored
// camera-branch inputs in the local player's sustained Walk/Rush animation.
class NativePremixLocomotion {
  public:
    NativePremixLocomotion(const ElfImage&,const RequestedStrength&);
    ~NativePremixLocomotion();
    NativePremixLocomotion(const NativePremixLocomotion&)=delete;
    void bind_render_thread();
    // Host lane BEFORE NativeControls::begin_frame/JNI step; never in a borrowed native caller.
    void prepare_step();
    void finish_step() noexcept; // HOST after JNI, before pause/teardown callbacks.
    struct Counters { uint64_t acquisitions=0,leases=0,commits=0; };
    Counters counters() const { return counters_; } // Bound lane, quiescent audit only.
  private:
    [[maybe_unused]] const ElfImage& image_;const RequestedStrength& requested_;
    std::unique_ptr<abi::GuestCall> call_;
    uint32_t original_=0,lane_=0,main_table_=0,set_table_=0,timeline_table_=0,track_table_=0,position_table_=0,node_table_=0;
    uint32_t manager_slot_=0,get_block_=0,drop_=0,release_=0,sample_=0,blend_=0;
    Counters counters_{};
    struct Prepared;
    std::unique_ptr<Prepared> prepared_;
    static NativePremixLocomotion* instance_;
    static thread_local unsigned depth_;
    static void __cdecl compute(uint32_t,uint32_t,uint32_t,uint32_t) noexcept;
    void execute(uint32_t,uint32_t,uint32_t,uint32_t);
};
}
