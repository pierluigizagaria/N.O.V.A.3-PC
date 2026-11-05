#pragma once
// Value-only V01s proposal; no guest pointers, hooks or native authority.
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace nova::weapon_placement {
// Provisional visibility trial budget: one twentieth of each full viewport axis.
// Actual weapon/FOV/ADS/animation visibility acceptance is still required.
inline constexpr float kMaximumOffset=.05f;
struct Position {
    float x=0, y=0; // Full viewport fractions: positive right/up AFTER native orientation.
    bool operator==(const Position &) const = default;
};
inline bool valid(Position value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::abs(value.x)<=kMaximumOffset && std::abs(value.y)<=kMaximumOffset;
}
inline Position canonical(Position value) noexcept {
    if(value.x==0) value.x=0; // Persist/reset negative zero as original0.
    if(value.y==0) value.y=0;
    return value;
}
inline std::optional<float> parse_coordinate(std::string_view text) noexcept {
    if(text.empty()) return {};
    float value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value,std::chars_format::general);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !valid({value,0})) return {};
    return value==0 ? 0.f : value;
}
// UI-lane preference only, not a render mailbox or admission witness.
class RequestedPosition {
  public:
    explicit RequestedPosition(Position initial={}) {
        if(!set(initial)) throw std::invalid_argument("Invalid weapon position pair");
    }
    bool set(Position value) noexcept {
        if(!valid(value)) return false;
        value_=canonical(value); return true;
    }
    void reset() noexcept { value_={}; }
    Position requested() const noexcept { return value_; }
  private:
    Position value_{};
};
using Projection=std::array<float,16>;
inline Projection original_projection(const Projection &native) noexcept {
    Projection out; std::memcpy(out.data(),native.data(),sizeof(out)); return out;
}
// Pure algebra ONLY. Caller supplies an already oriented column-major matrix
// acting on column vectors. Never apply to camera/driver storage or infer an
// admitted mesh from this helper. No projection convention is guessed here:
// row0 += 2*x*row3, row1 += 2*y*row3; clip Z/W are unchanged.
inline Projection offset_projection(const Projection &native,Position offset) noexcept {
    auto out=original_projection(native);
    if(!valid(offset) || (offset.x==0 && offset.y==0)) return out;
    for(float value:native) if(!std::isfinite(value)) return out;
    for(size_t column=0;column<4;++column) {
        const auto base=column*4;
        for(size_t row=0;row<2;++row) {
            const auto delta=row==0 ? offset.x : offset.y;
            if(delta==0) continue;
            const double value=double(native[base+row])+2*double(delta)*native[base+3];
            if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max())
                return original_projection(native);
            out[base+row]=static_cast<float>(value);
        }
    }
    return out;
}
} // namespace nova::weapon_placement
