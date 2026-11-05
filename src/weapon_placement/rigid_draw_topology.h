#pragma once
#include <cstdint>
namespace nova::weapon_placement {
// Only call on the current render lane, inside the supplied native model sink.
// read must be callback-free. These borrowed pointers never outlive this call.
struct RigidDrawFields {
    uint32_t manager,weapon,draw,mesh,driver,model,node_vtable,mesh_vtable;
};
template<class Read> bool current_rigid_topology(const RigidDrawFields& f,Read read) {
    if(!f.manager||!f.weapon||!f.draw||!f.mesh||!f.driver||
       uint64_t(f.draw)+0x10!=f.model) return false;
    // The list dispatcher supplies a live entry and delegate. Do not inspect a
    // foreign caller's draw until these current pointer identities agree.
    const auto current=read(f.manager,0xac);
    if(!current||read(f.manager,0x10c)!=4||read(current,0xec)!=f.draw||
       read(f.manager,0x10)!=f.driver) return false;
    if(read(f.draw,0)!=f.node_vtable||read(f.draw,0xe8)!=f.manager||
       read(f.draw,0x108)!=f.mesh||read(f.mesh,0)!=f.mesh_vtable) return false;
    const auto object=read(f.weapon,4);
    if(!object) return false;
    const auto root=read(object,0x98);
    if(!root) return false;
    auto descendant=[&](uint32_t p) {
        // Work limit only; deeper/unknown trees retain native output. This is
        // neither an allocation generation nor an animation/ADS threshold.
        for(unsigned i=0;i<64&&p;++i) {
            if(p==root) return true;
            const auto parent=read(p,0xd0);
            if(parent==p) return false;
            p=parent;
        }
        return false;
    };
    return descendant(current)&&descendant(f.draw);
}
} // namespace nova::weapon_placement
