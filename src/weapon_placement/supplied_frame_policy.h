#pragma once
#include "weapon_placement/position_policy.h"
#include "weapon_placement/position_gate.h"
#include "camera/viewmodel_eligibility.h"
namespace nova::weapon_placement {
// Value-only future adapter seam: supplied final-oriented projection and fresh
// O09 observation from THIS consumption. No acquisition, cached positive permit,
// ADS weight, target/mesh inference or native write. Requests are never mutated.
// Today every scope returns exact original bytes because the real adapter and
// shared mesh/pose join are absent. Native continuity/retirement remain unproved.
inline Projection projection_for_supplied_frame(const Projection &native,Position requested,
    const camera::viewmodel_eligibility::Decision &eligibility,
    const camera::viewmodel_eligibility::Boundary &current) noexcept {
    if(!kPositionControlAvailable || !eligibility.permits(current)) return original_projection(native);
    return offset_projection(native,requested);
}
} // namespace nova::weapon_placement
