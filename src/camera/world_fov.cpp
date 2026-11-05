#include "camera/world_fov.h"
#include <bit>
#include <cmath>
#include <numbers>

namespace nova::camera {
namespace {
constexpr double pi = std::numbers::pi;
bool angle(double value) { return std::isfinite(value) && value > 0 && value < pi; }
bool valid(const ReferenceLens &lens) {
    return angle(lens.native_vertical) && angle(lens.reference_vertical);
}
}
std::optional<ReferenceLens> pseudo_horizontal_lens(float native_vertical,
                                                   int32_t writer_width, int32_t writer_height) {
    if (!angle(native_vertical) || writer_width <= 0 || writer_height <= 0) return {};
    // Capture the guest screen ratio used by THIS producer, never a later
    // viewport/aspect value. setAspectRatio need not rewrite native FOV.
    const double writer_aspect = double(writer_width) / writer_height;
    ReferenceLens result{native_vertical, native_vertical * (writer_aspect / reference_aspect)};
    return valid(result) ? std::optional(result) : std::nullopt;
}
std::optional<ReferenceLens> vertical_lens(float native_vertical) {
    ReferenceLens result{native_vertical, native_vertical};
    return valid(result) ? std::optional(result) : std::nullopt;
}
std::optional<ReferenceLens> affine_reference_lens(const ReferenceLens &before,
                                                 float native_before, float native_after,
                                                 float native_alpha) {
    if (!valid(before) || !angle(native_after) || !std::isfinite(native_alpha) ||
        std::bit_cast<uint32_t>(before.native_vertical) != std::bit_cast<uint32_t>(native_before))
        return {};
    const double reference = double(native_after) + (1.0 - native_alpha) *
        (before.reference_vertical - double(native_before));
    ReferenceLens result{native_after, reference};
    return valid(result) ? std::optional(result) : std::nullopt;
}
std::optional<float> world_vertical_fov(const ReferenceLens &native,
                                       float native_default_pseudo_degrees,
                                       std::optional<float> horizontal_degrees) {
    if (!horizontal_degrees || !valid(native) || !std::isfinite(*horizontal_degrees) ||
        *horizontal_degrees < 60 || *horizontal_degrees > 120) return {};
    const double base = native_default_pseudo_degrees * (pi / 180.0) / reference_aspect;
    if (!angle(base)) return {};
    const double reference_lens = std::tan(*horizontal_degrees * (pi / 360.0)) / reference_aspect;
    const double zoom = std::tan(native.reference_vertical * .5) / std::tan(base * .5);
    const double effective = 2.0 * std::atan(reference_lens * zoom);
    return angle(effective) ? std::optional(float(effective)) : std::nullopt;
}
std::optional<double> horizontal_fov(double vertical_radians, double aspect) {
    if (!angle(vertical_radians) || !std::isfinite(aspect) || aspect <= 0) return {};
    const double result = 2.0 * std::atan(std::tan(vertical_radians * .5) * aspect);
    return angle(result) ? std::optional(result) : std::nullopt;
}
std::optional<NativeFovDefaults> native_fov_defaults(double aspect) {
    if (!std::isfinite(aspect) || aspect<=0) return {};
    const auto world=horizontal_fov((70.0/aspect)*pi/180.0,aspect);
    const auto weapon=horizontal_fov((90.0/aspect)*pi/180.0,aspect);
    if (!world || !weapon) return {};
    return NativeFovDefaults{float(*world*180.0/pi),float(*weapon*180.0/pi)};
}
std::optional<float> weapon_vertical_fov(const ReferenceLens &lens,
                                        std::optional<float> horizontal_degrees) {
    if (!horizontal_degrees || !valid(lens) || !std::isfinite(*horizontal_degrees) ||
        *horizontal_degrees<60 || *horizontal_degrees>130) return {};
    // PROVISIONAL framing policy: scale the native canonical tangent extent.
    const double native=reference_aspect*std::tan((90.0/reference_aspect)*pi/360.0);
    const double multiplier=std::tan(*horizontal_degrees*pi/360.0)/native;
    const double result=2*std::atan(multiplier*std::tan(lens.reference_vertical*.5));
    const float rounded=float(result);
    if (!angle(result) || !angle(rounded) || !std::isnormal(rounded) ||
        !std::isfinite(1.0f/std::tan(rounded*.5f))) return {};
    return rounded;
}
} // namespace nova::camera
