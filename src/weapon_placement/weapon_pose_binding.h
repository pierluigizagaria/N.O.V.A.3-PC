#pragma once

namespace nova::weapon_pose {
// V01g source proposal: the consumed bone cohort is not yet bound to the
// current admitted weapon mesh. UpperArm is not the rigid rifle ancestor.
// This is readiness, never FPS eligibility.
// O08/V01 consumers must retain original output while false.
inline constexpr bool kConsumedWeaponPoseBindingAvailable = false;
} // namespace nova::weapon_pose
