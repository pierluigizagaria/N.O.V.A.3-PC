#pragma once
#include <cstdint>

namespace nova::camera::viewmodel_eligibility {
// Host-issued current-observation witnesses, NOT guest addresses. Only a
// separately proved lifecycle producer may claim allocation generations.
// Zero means missing/unproved; every new native sample has a new serial.
struct OwnerEpochs {
    uint64_t local_root=0, actor=0, health=0, camera=0, weapon=0;
    bool operator==(const OwnerEpochs &) const = default;
};
struct Boundary {
    // A final render-consumption serial, not a frame counter or wall-clock time.
    uint64_t consumption=0;
    // Native producer uses the observed manager-lifetime revision. Other state
    // is freshly queried at consumption, not event-cached. Zero is unproved.
    uint64_t authority_revision=0;
    OwnerEpochs owners;
    bool operator==(const Boundary &) const = default;
};
enum class Reason { unproved, native_non_fps, owner_lost, lifetime_changed, ordinary_fps };
enum class WeaponPolicy { native, custom };

// Defined in viewmodel_eligibility_native.h for the current local SP/MP owner.
// O08/V01 must not implement substitutes or reuse a prior consumption permit.
class NativeProducer;
namespace fixture { class Producer; }

class Decision {
  public:
    Decision() = default; // Missing proof always means native behavior.
    static Decision native(Reason reason=Reason::unproved) noexcept;
    // current must come from THIS actual consumer, never from a saved Decision.
    bool permits(const Boundary &current) const noexcept;
    Reason reason() const noexcept { return reason_; }
  private:
    friend class NativeProducer;
    friend class fixture::Producer; // Defined only in the synthetic fixture.
    explicit Decision(Boundary boundary) noexcept;
    Boundary boundary_{};
    Reason reason_=Reason::unproved;
};
// This function cannot return world policy. Native weapon fallback must remain
// inside the explicit weapon-pass context, without changing requested settings.
WeaponPolicy weapon_policy(const Decision &, const Boundary &, bool custom_requested) noexcept;
} // namespace nova::camera::viewmodel_eligibility
