#pragma once
#include "platform/native_device_profile.h"
#include <atomic>
#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <optional>
#include <string>
#include <string_view>

namespace nova::platform {
// Native coordinates, independent of the host output/window dimensions.
// Publish a coherent pair only at the checked between-step render boundary.
struct GuestExtent {
    int width=1280,height=720;
    bool operator==(const GuestExtent &) const = default;
    double aspect() const { return double(width)/height; }
    void validate() const {
        if(width<=0 || height<=0) throw std::invalid_argument("Invalid guest extent");
    }
};
// Exact16:9, quarter-aligned native targets. Each size uses the attested GPU_5
// fallback profile; no per-size custom profile exists in either accepted archive.
// Native touch bounds are int16; this finite4K ceiling also bounds resource math.
inline constexpr std::array<GuestExtent,13> standard_render_extents{{
    {640,360},{960,540},{1024,576},{1152,648},{1280,720},{1600,900},{1920,1080},
    {2048,1152},{2304,1296},{2560,1440},{2880,1620},{3200,1800},{3840,2160}}};
inline bool supported_render_extent(GuestExtent extent) {
    return std::find(standard_render_extents.begin(),standard_render_extents.end(),extent)!=standard_render_extents.end();
}
enum class GuestExtentBlock {
    none,
    native_1920_profile,
    native_target_lifecycle
};
struct StartupGuestExtent {
    GuestExtent requested,active;
    GuestExtentBlock block=GuestExtentBlock::none;
};
// Startup-only supported native extents. The first native resize publishes
// both the window and CreationSettings dimensions before GLES driver creation.
// Do not turn an arbitrary SDL output dimension into a native render request.
inline StartupGuestExtent startup_guest_extent(GuestExtent requested={},const NativeDeviceProfile *profile=nullptr) {
    requested.validate();
    StartupGuestExtent selected{requested,{}};
    if(requested==selected.active) return selected;
    if(supported_render_extent(requested) &&
       (requested.width!=1920 || (profile && profile->applied()))) {
        selected.active=requested; return selected;
    }
    // LoadProfile's width-1920 branch replaces multicore CPU tier 2 with tier 1.
    // CPU_S also sets LowGameplay=true; this is not merely a GPU size limit.
    selected.block=requested.width==1920 ? GuestExtentBlock::native_1920_profile
                                         : GuestExtentBlock::native_target_lifecycle;
    return selected;
}
inline std::optional<GuestExtent> parse_render_extent(std::string_view text) {
    for(const auto extent:standard_render_extents)
        if(text==std::to_string(extent.width)+"x"+std::to_string(extent.height)) return extent;
    return {};
}
inline std::string render_extent_name(GuestExtent value) {
    if(!supported_render_extent(value))
        throw std::invalid_argument("Unsupported native render extent");
    return std::to_string(value.width)+"x"+std::to_string(value.height);
}
// The Java callbacks may run on guest threads. One atomic host word prevents a
// torn width/height pair; only the GL transaction publishes provisional/active
// values, with no native step until the transaction completes or rolls back.
class GuestExtentState {
  public:
    explicit GuestExtentState(GuestExtent extent={}) { publish(extent); }
    GuestExtent snapshot() const {
        const auto pair=pair_.load(std::memory_order_acquire);
        return {int(pair&0xffffu),int(pair>>16)};
    }
    void publish(GuestExtent extent) {
        extent.validate();
        if(extent.width>65535 || extent.height>65535)
            throw std::invalid_argument("Guest extent exceeds atomic pair domain");
        pair_.store(uint32_t(extent.width)|(uint32_t(extent.height)<<16),std::memory_order_release);
    }
  private:
    std::atomic<uint32_t> pair_{0};
};
} // namespace nova::platform
