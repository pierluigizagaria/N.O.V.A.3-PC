#pragma once
#include "weapon_placement/weapon_pose_binding.h"
namespace nova::weapon_placement {
// Readiness only. An actual per-consumer adapter, orientation and native ADS
// composition/cleanup proof are absent. A flag flip is not their implementation.
inline constexpr bool kPositionAdapterConnected=false;
inline constexpr bool kPositionControlAvailable=
    weapon_pose::kConsumedWeaponPoseBindingAvailable && kPositionAdapterConnected;
} // namespace nova::weapon_placement
