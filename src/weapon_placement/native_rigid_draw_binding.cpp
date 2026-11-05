#include "weapon_placement/native_rigid_draw_binding.h"
#include <cstring>
#include <stdexcept>
namespace nova::weapon_placement {
namespace {
uint32_t field(uint32_t p,uint32_t offset) {
    uint32_t v;std::memcpy(&v,reinterpret_cast<const void*>(p+offset),4);return v;
}
}
NativeRigidDrawBinding::NativeRigidDrawBinding(const ElfImage& image,bool fixture_locations) {
    // Constructor-proved address points in the pinned x86 image. No driver
    // layout or synthetic vtable is accepted. Install only while quiescent.
    node_vtable_=fixture_locations ? image.symbol("v01_rigid_node_table") : image.address(0x1121db0,0xbc);
    mesh_vtable_=fixture_locations ? image.symbol("v01_rigid_mesh_table") : image.address(0x110ebe8,0x48);
    auto code=[&](uint32_t va,const char* name) { return fixture_locations ? image.symbol(name) : image.address(va,1); };
    if(!node_vtable_||!mesh_vtable_) throw std::runtime_error("Missing rigid class locations");
    if(field(node_vtable_,8)!=code(0x8ddad0,"v01_rigid_draw_entry")||
       field(node_vtable_,0xb8)!=code(0xa02440,"v01_rigid_absolute")||
       field(mesh_vtable_,0x44)!=code(0x80ba90,"_ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE")||
       field(mesh_vtable_,0x14)!=code(0x81c600,"v01_rigid_buffer"))
        throw std::runtime_error("Rigid draw native class/consumer mismatch");
}
camera::viewmodel_eligibility::NativeSample NativeRigidDrawBinding::sample(
 const camera::NativeWorldFov::ViewmodelOwnerCapsule& owners,
 uint32_t draw,uint32_t mesh,uint32_t driver,uint32_t model) const {
    using camera::NativeWorldFov;
    const auto before=NativeWorldFov::sample_viewmodel_weapon(owners);
    if(!before.eligibility.decision.permits(before.eligibility.boundary)) return {};
    RigidDrawFields f{before.scene_manager,before.weapon,draw,mesh,driver,model,node_vtable_,mesh_vtable_};
    if(!current_rigid_topology(f,field)) return {};
    // All reads and O09's admitted SP queries are callback-free. Reacquire the
    // final Decision; the first positive is never returned as authorization.
    const auto now=NativeWorldFov::sample_viewmodel_weapon(owners);
    if(now.weapon!=before.weapon||now.scene_manager!=before.scene_manager||
       !now.eligibility.decision.permits(now.eligibility.boundary)||
       !current_rigid_topology(f,field)) return {};
    return now.eligibility;
}
} // namespace nova::weapon_placement
