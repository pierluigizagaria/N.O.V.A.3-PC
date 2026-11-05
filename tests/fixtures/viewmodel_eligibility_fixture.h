#pragma once
#include "camera/viewmodel_eligibility.h"

namespace nova::camera::viewmodel_eligibility::fixture {
// Fabricated proof for the protocol specification ONLY. This is not a native
// predicate and must never be included in production or an effect module.
class Producer {
  public:
    static Decision ordinary(Boundary boundary) noexcept { return Decision(boundary); }
};
} // namespace nova::camera::viewmodel_eligibility::fixture
