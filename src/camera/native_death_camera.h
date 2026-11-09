#pragma once
#include "loader/elf_image.h"
#include <memory>

namespace nova::camera {
// Online on-foot killcam: follow the presented model root without inheriting
// animated camera anchors. Native collision and dead-state transitions remain.
class NativeDeathCamera {
public:
    using Admission = bool (*)();
    NativeDeathCamera(const ElfImage &, Admission);
    ~NativeDeathCamera();
    NativeDeathCamera(const NativeDeathCamera &) = delete;
    NativeDeathCamera &operator=(const NativeDeathCamera &) = delete;
    // Called on the game/render lane with the freshly resolved local player.
    // Only camera angles change; actor input and network state stay untouched.
    static bool controllable(uint32_t player) noexcept;
    static void rotate(uint32_t player, float yaw, float pitch) noexcept;
    static void forget(uint32_t node) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
