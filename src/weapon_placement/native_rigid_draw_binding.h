#pragma once
#include "camera/native_world_fov.h"
#include "weapon_placement/rigid_draw_topology.h"
namespace nova::weapon_placement {
// Structural current rigid draw admission, NOT successful pose provenance,
// ADS alignment, buffer visibility, or a placement capability.
class NativeRigidDrawBinding {
 public:
    explicit NativeRigidDrawBinding(const ElfImage&,bool fixture_locations=false);
    camera::viewmodel_eligibility::NativeSample sample(
      const camera::NativeWorldFov::ViewmodelOwnerCapsule&,
      uint32_t supplied_draw,uint32_t supplied_mesh,
      uint32_t supplied_driver,uint32_t supplied_model) const;
 private:
    uint32_t node_vtable_=0,mesh_vtable_=0;
};
} // namespace nova::weapon_placement
