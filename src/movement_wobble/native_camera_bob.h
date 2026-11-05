#pragma once
#include "abi/guest_hook.h"
#include "camera/native_world_fov.h"
#include "movement_wobble/movement_wobble.h"
#include "movement_wobble/camera_view.h"
#include <memory>

namespace nova::movement_wobble {
class NativeCameraBob {
  public:
    NativeCameraBob(const ElfImage&,const RequestedStrength&,bool fixture=false);
    ~NativeCameraBob();
    void bind_render_thread();
    void prepare_step();
    void finish_step() noexcept;
    struct Counters { uint64_t registrations=0,admitted=0,matrices=0; };
    Counters counters()const{return counters_;}
    // Shared native matrix builder, including FOV-triggered rebuilds. The native
    // builder produces VIEW/frustum together from a scoped camera-only input.
    static void recalculate(uint32_t original,uint32_t node);
  private:
    static NativeCameraBob* instance_;
    const RequestedStrength& requested_;
    std::unique_ptr<abi::GuestCall> registration_;
    uint32_t original_registration_=0,lane_=0,node_table_=0;
    camera::NativeWorldFov::PreMixOwnerCapsule capsule_;
    CameraView reference_{},native_{},reduced_{};
    uint32_t reference_node_=0,reference_parent_=0,reference_weapon_=0,ready_node_=0;
    uint64_t reference_generation_=0,report_at_=0;
    int reported_strength_=-1;
    unsigned depth_=0;
    Counters counters_{};
    static uint32_t __cdecl register_camera(uint32_t,uint32_t) noexcept;
    void capture(uint32_t);
};
}
