#include "camera/viewmodel_eligibility.h"

namespace nova::camera::viewmodel_eligibility {
namespace {
bool complete(const Boundary &b) noexcept {
    return b.consumption && b.authority_revision && b.owners.local_root &&
        b.owners.actor && b.owners.health && b.owners.camera && b.owners.weapon;
}
}
Decision::Decision(Boundary boundary) noexcept : boundary_(boundary) {
    if (complete(boundary)) reason_=Reason::ordinary_fps;
}
Decision Decision::native(Reason reason) noexcept {
    Decision result;
    // A diagnostic value supplied by a caller cannot mint positive authority.
    result.reason_=reason==Reason::ordinary_fps ? Reason::unproved : reason;
    return result;
}
bool Decision::permits(const Boundary &current) const noexcept {
    return reason_==Reason::ordinary_fps && complete(current) && boundary_==current;
}
WeaponPolicy weapon_policy(const Decision &decision,const Boundary &current,bool requested) noexcept {
    return requested && decision.permits(current) ? WeaponPolicy::custom : WeaponPolicy::native;
}
} // namespace nova::camera::viewmodel_eligibility
