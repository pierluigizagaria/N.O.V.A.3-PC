#pragma once
#include <cstdint>
#include <optional>

namespace nova::camera {
inline constexpr double reference_aspect = 16.0 / 9.0;
struct NativeFovDefaults { float world, weapon; };
// Actual horizontal equivalents of the proved canonical native pseudo-angles.
// This supplies host slider positions only; it does not select custom optics.
std::optional<NativeFovDefaults> native_fov_defaults(double aspect=reference_aspect);
// This is provenance of an unmodified native lens, independent of the PC setting.
// The native raw angle stays native; reference_vertical is its 16:9 counterpart.
struct ReferenceLens {
    float native_vertical = 0;
    double reference_vertical = 0;
};
std::optional<ReferenceLens> pseudo_horizontal_lens(float native_vertical,
                                                   int32_t writer_width, int32_t writer_height);
std::optional<ReferenceLens> vertical_lens(float native_vertical);
// For the proved native affine writer Z=X+a*(target-X). This composes its
// reference counterpart from the actual native result, including Z==X no-ops.
// The caller must authenticate the same camera lifetime, origin and writer.
std::optional<ReferenceLens> affine_reference_lens(const ReferenceLens &before,
                                                 float native_before, float native_after,
                                                 float native_alpha);
// Null means native fallback. No setting, camera state, target or matrix is changed.
std::optional<float> world_vertical_fov(const ReferenceLens &native,
                                       float native_default_pseudo_degrees,
                                       std::optional<float> horizontal_degrees);
std::optional<double> horizontal_fov(double vertical_radians, double aspect);
// Canonical native weapon90 framing; actual native zoom/diff remains in lens.
std::optional<float> weapon_vertical_fov(const ReferenceLens &lens,
                                        std::optional<float> horizontal_degrees);
} // namespace nova::camera
