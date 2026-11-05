#pragma once
namespace nova::platform {
struct GraphicsQuality {
    int lod_distance=4;
    // 0 native, 1 trilinear, 2 trilinear with capability-limited16x anisotropy.
    int texture_filtering=2;
    bool operator==(const GraphicsQuality &) const = default;
};
inline bool valid_graphics_quality(GraphicsQuality value) {
    return value.lod_distance>=1 && value.lod_distance<=4 &&
           value.texture_filtering>=0 && value.texture_filtering<=2;
}
}
