#pragma once
#include "input/controls.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace nova::input {
// CPlayerControl's pending look inputs at PlayerComponent + 0x48. These are
// consumed by UpdateFromInput, not actor/camera state. A direct actor yaw change
// made earlier in the frame loses to the pending absolute yaw from touch input.
struct GuestLookInput {
    float yaw_rate, yaw_rate_target, yaw_rate_step, yaw_target;
    uint8_t yaw_pending;
    uint8_t padding[3];
    float pitch_target, pitch_delta, pitch_override;
};
static_assert(sizeof(GuestLookInput) == 0x20);
static_assert(offsetof(GuestLookInput,yaw_pending) == 0x10);
static_assert(offsetof(GuestLookInput,pitch_target) == 0x14);
inline void queue_mouse_look(GuestLookInput &input, float yaw, float pitch, Look delta) {
    input.yaw_rate = input.yaw_rate_target = input.yaw_rate_step = 0;
    input.yaw_target = yaw + delta.yaw;
    input.yaw_pending = 1;
    input.pitch_target = pitch + delta.pitch;
    input.pitch_delta = delta.pitch;
    input.pitch_override = 0;
}
// The actual hook ordering, shared with the synthetic guest contract test.
// Eligibility stays lazy: no pending sample means no control/lock queries.
template<class Eligible, class Prepare, class Consume, class Readback>
void mouse_look_boundary(bool &pending, Eligible eligible, Prepare prepare,
                         Consume consume, Readback readback) {
    bool applied = false;
    if (pending && eligible()) {
        prepare();
        pending = false;
        applied = true;
    }
    consume();
    if (applied) readback();
}
inline bool sprint_requested(const FrameInput &input) {
    return input.down(Action::sprint) && input.down(Action::forward) && !input.down(Action::back);
}
inline float sprint_heading(const FrameInput &input) {
    if (!sprint_requested(input)) return 0;
    return 45.f * (static_cast<int>(input.down(Action::left)) - static_cast<int>(input.down(Action::right)));
}
// Run the original velocity calculation in the requested movement basis. The
// actor's view is restored before the caller continues physics/camera/network
// work. No velocity, rush flag, speed or stamina is supplied by the host.
template<class ReadYaw, class Rotate, class Compute>
void with_movement_heading(float heading, ReadYaw read_yaw, Rotate rotate, Compute compute) {
    if (!heading) { compute(); return; }
    const float saved = read_yaw();
    rotate(heading);
    try { compute(); }
    catch (...) { rotate(saved - read_yaw()); throw; }
    rotate(saved - read_yaw());
}
inline bool hide_scheme_draw(bool gameplay, uint32_t drawing, uint32_t normal_scheme) {
    return gameplay && drawing && drawing == normal_scheme;
}
// Saves may retain preferred slots for unowned weapon variants. Keep valid
// loadout order, then include other owned inventory weapons (campaign pickups).
// Build PC slots without changing the game's saved loadout or unlocking items.
template<class Resolve, class Available>
std::vector<uint32_t> available_weapon_slots(std::span<const uint32_t> preferred,
    std::span<const uint32_t> inventory, Resolve resolve, Available available) {
    std::vector<uint32_t> result;
    std::set<int> seen;
    auto add = [&](uint32_t id) {
        int index = resolve(id);
        if (index >= 0 && available(index) && seen.insert(index).second && result.size() < 5)
            result.push_back(id);
    };
    for (auto id : preferred) add(id);
    for (auto id : inventory) add(id);
    return result;
}
enum class SlotStatus { absent, unavailable, selected };
struct SlotResult { SlotStatus status; uint32_t id = 0; int index = -1; };
// Slot entries are IDs. The resolver must search this player's inventory,
// never the global weapon-definition catalog (their orders differ in MP).
template<class Resolve, class Available, class Select>
SlotResult select_weapon_slot(std::span<const uint32_t> slots, size_t slot,
                              Resolve resolve, Available available, Select select) {
    if (slot >= slots.size()) return {SlotStatus::absent};
    const auto id = slots[slot];
    const int index = resolve(id);
    if (index < 0 || !available(index)) return {SlotStatus::unavailable,id,index};
    select(index);
    return {SlotStatus::selected,id,index};
}
}
