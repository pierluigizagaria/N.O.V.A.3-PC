#pragma once
// O08g source proposal. This module edits a sample value only; it cannot acquire
// owners, create eligibility, retain native resources or publish a native pose.
#include <array>
#include <atomic>
#include <charconv>
#include <optional>
#include <string_view>

namespace nova::movement_wobble {
inline constexpr int kDefaultPercent=100;
inline constexpr int kResetPercent=100;
inline constexpr int kMaximumPercent=100;
class Strength {
  public:
    constexpr Strength() = default;
    static constexpr Strength from_percent(int value) {
        Strength out;out.percent_=value<0?0:(value>kMaximumPercent?kMaximumPercent:value);return out;
    }
    constexpr int percent() const { return percent_; }
    constexpr void reset() { percent_=kResetPercent; }
    bool operator==(const Strength&) const = default;
  private:
    int percent_=kDefaultPercent;
};
// UI writes a host preference only. A future proof-backed GL adapter may read
// it; this class never enters guest code or changes clocks/poses by itself.
class RequestedStrength {
  public:
    explicit RequestedStrength(Strength initial={}):percent_(initial.percent()) {}
    void set(Strength value) { percent_.store(value.percent(),std::memory_order_release); }
    Strength requested() const { return Strength::from_percent(percent_.load(std::memory_order_acquire)); }
  private:
    std::atomic<int> percent_;
};
inline std::optional<Strength> parse_strength(std::string_view text) {
    if(text.empty()) return {};
    int value=0;const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || value<0 || value>100) return {};
    return Strength::from_percent(value);
}
using Quaternion=std::array<float,4>;
// The shared binding owner must prove the selected current Main input and its
// loop-specific authored reference before any PRODUCTION call is admitted.
// NativeBlend is the authenticated typed quaternion blend, not whole-pose gain.
// Missing data falls back to the original sample. No lifecycle token is minted.
template<class NativeBlend>
Quaternion reduce_sample(const Quaternion& current,const Quaternion* authored_start,
                         Strength strength,NativeBlend&& native_blend) {
    if(strength.percent()==100 || !authored_start) return current;
    if(strength.percent()==0) return *authored_start;
    return native_blend(*authored_start,current,static_cast<float>(strength.percent())/100.f);
}
} // namespace nova::movement_wobble
