#pragma once
#include "camera/viewmodel_eligibility.h"
#include "abi/guest_hook.h"
#include <atomic>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace nova::camera::viewmodel_eligibility {
// Current supplied receiver identities. Never retain these across a callback.
struct NativeOwners {
    uint32_t level=0,player=0,actor=0,game_camera=0,node=0,weapon_manager=0,weapon=0;
    uint32_t inventory=0,end=0,index=0;
    uint64_t camera_generation=0;
};
struct NativeSample { Decision decision; Boundary boundary; };
class NativeProducer {
  public:
    NativeProducer(const ElfImage &,bool fixture_locations=false);
    ~NativeProducer();
    NativeProducer(const NativeProducer &)=delete;
    void bind_render_thread(uint32_t lane) noexcept { lane_=lane; }
    NativeSample sample(const NativeOwners &);
    const char *last_rejection() const { return rejection_; } // Bound render lane only.
  private:
    uint32_t lane_=0,mp_slot_=0,app_slot_=0,level_slot_=0,ingame_vtable_=0;
    uint32_t current_state_=0,is_mp_=0,is_dead_=0,is_cinematic_=0,original_health_=0;
    uint32_t original_ctor1_=0,original_ctor2_=0,original_dtor1_=0,original_dtor2_=0;
    // Only the proven callback-free query cohort holds a shared lease. Native
    // constructors/destructors run outside the lock after pre-invalidation.
    std::shared_mutex manager_lifetime_;
    std::atomic<uint32_t> ready_manager_{0};
    std::atomic<uint64_t> manager_epoch_{0};
    uint64_t consumption_=0;
    const char *rejection_="native-fps-not-sampled";
    std::unique_ptr<abi::GuestCall> health_call_;
    std::vector<std::unique_ptr<abi::GuestHook>> lifecycle_;
    struct Receipt { NativeProducer *owner; uint32_t health=0; unsigned calls=0; bool safe=true; };
    static thread_local Receipt *receipt_;
    static NativeProducer *instance_;
    static uint32_t __cdecl health(uint32_t);
    static void __cdecl ctor1(uint32_t);
    static void __cdecl ctor2(uint32_t);
    static void __cdecl dtor1(uint32_t);
    static void __cdecl dtor2(uint32_t);
    void constructed(uint32_t,uint32_t);
    void destroyed(uint32_t,uint32_t);
};
} // namespace nova::camera::viewmodel_eligibility
