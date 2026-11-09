#include "camera/native_death_camera.h"
#include "abi/guest_hook.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace nova::camera {
namespace {
constexpr auto update_name = "_ZN15PlayerComponent23MpUpdateDeadStateCameraEv";
constexpr auto matrices_name = "_ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv";
using Vec = std::array<float, 3>;
template<class T> T read(uint32_t p, uint32_t offset = 0) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void *>(p + offset), sizeof(value));
    return value;
}
uint32_t ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
bool finite(const Vec &v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
}

struct NativeDeathCamera::Impl {
    static Impl *active;
    Admission admitted;
    std::unique_ptr<abi::GuestHook> update;
    std::unique_ptr<abi::GuestCall> sweep, matrices, registration;
    uint32_t frame_owner = 0;
    bool frame_ready = false, view_ready = false, obstructed = false;
    bool registering = false;
    uint32_t get_level = 0, get_player = 0;
    uint32_t owner = 0, node = 0, killer = 0, death = 0, target_node = 0;
    Vec target_offset{}, target{}, eye{}, sampled_position{};
    float yaw = 0, pitch = 0, radius = 0;

    Impl(const ElfImage &image, Admission admission) : admitted(admission) {
        if (active || !admitted) throw std::runtime_error("Invalid death camera adapter");
        get_level = abi::require_function(image, "_ZN6CLevel8GetLevelEv");
        get_player = abi::require_function(image, "_ZN6CLevel18GetPlayerComponentEv");
        sweep = std::make_unique<abi::GuestCall>(image, update_name, 0x419,
            "_ZN15CPhysicsManager15SphereSweepTestEfRKN6glitch4core8vector3dIfEES5_RS3_S6_RfRP11CGameObjectii",
            ptr(reinterpret_cast<void *>(&sweep_camera)));
        matrices = std::make_unique<abi::GuestCall>(image, update_name, 0x484,
            matrices_name, ptr(reinterpret_cast<void *>(&recalculate)));
        // The tracker overwrites eye/target again at scene registration. Apply
        // the same resolved view before its base registers/culls the camera.
        registration = std::make_unique<abi::GuestCall>(image,
            "_ZN6glitch5scene29CCameraTargetTrackerSceneNode27onRegisterSceneNodeInternalEPKv",
            0xc1, "_ZN6glitch5scene16CCameraSceneNode27onRegisterSceneNodeInternalEPKv",
            ptr(reinterpret_cast<void *>(&register_camera)));
        update = std::make_unique<abi::GuestHook>(image, update_name,
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},
            ptr(reinterpret_cast<void *>(&update_camera)));
        active = this;
    }
    ~Impl() {
        update.reset(); registration.reset(); matrices.reset(); sweep.reset();
        active = nullptr;
    }
    void reset() noexcept {
        owner = node = killer = death = target_node = 0;
        frame_ready = view_ready = false;
    }
    bool controllable(uint32_t player) const {
        if (!player || owner != player || !node || !view_ready || !admitted() ||
            read<uint32_t>(player, 0x3ac) != node ||
            read<uint32_t>(player, 0x39c) != killer ||
            read<uint32_t>(player, 0x10c) != death ||
            !read<uint8_t>(player, 0x3a0) || !read<uint8_t>(player, 0x3a1) ||
            read<uint8_t>(player, 0x3a2)) return false;
        const auto scene = read<uint32_t>(node, 0xe8);
        return scene && read<uint32_t>(scene, 0xdc) == node &&
            read<uint32_t>(node, 0x304) == target_node;
    }
    void apply(uint32_t camera) {
        // No retained owner/model dereferences on the render path. Destruction
        // retires this identity; scene/target checks also fence camera handoff.
        if (!node || !view_ready || camera != node || !admitted() ||
            read<uint32_t>(camera, 0x304) != target_node) return;
        const auto scene = read<uint32_t>(camera, 0xe8);
        if (!scene || read<uint32_t>(scene, 0xdc) != camera) return;
        std::memcpy(reinterpret_cast<void *>(camera + 0x40), eye.data(), 12);
        std::memcpy(reinterpret_cast<void *>(camera + 0x10c), target.data(), 12);
    }
    static uint32_t __cdecl update_camera(uint32_t player) noexcept {
        auto &s = *active;
        // Retire the preceding death before native FastNodeUpdate can register
        // a reused camera while initializing this one's fresh framing.
        if (s.owner && (s.owner != player || s.death != read<uint32_t>(player, 0x10c) ||
            s.node != read<uint32_t>(player, 0x3ac) || s.killer != read<uint32_t>(player, 0x39c)))
            s.reset();
        // Each native update must reach its collision path to publish a view.
        // Native invalid-target/mount/respawn branches therefore retire it.
        s.frame_owner = s.admitted() ? player : 0;
        s.frame_ready = s.view_ready = false;
        const auto result = abi::invoke(s.update->original(), {player});
        if (!s.frame_ready) s.reset();
        s.frame_owner = 0;
        return result;
    }
    static uint32_t __cdecl sweep_camera(uint32_t physics, uint32_t radius,
        uint32_t from, uint32_t to, uint32_t hit, uint32_t normal,
        uint32_t fraction, uint32_t object, uint32_t group, uint32_t mask) noexcept {
        auto &s = *active;
        const auto player = s.frame_owner;
        if (player && !read<uint8_t>(player, 0x3a2)) {
            const auto camera = read<uint32_t>(player, 0x3ac);
            const auto actor = read<uint32_t>(player, 0x39c);
            if (camera && actor) {
                // SetPositionRemotePlayer writes these presented XYZ values
                // immediately. The scene node only receives a local setter;
                // its absolute transform is updated later by scene traversal.
                const auto position = read<Vec>(actor, 0x54);
                const auto native_target = read<Vec>(from);
                const auto native_eye = read<Vec>(to);
                const auto camera_eye = read<Vec>(camera, 0x40);
                const auto timestamp = read<uint32_t>(player, 0x10c);
                Vec offset{};
                float distance2 = 0;
                for (size_t i = 0; i < 3; ++i)
                    offset[i] = camera_eye[i] - native_target[i];
                for (const auto value : offset) distance2 += value * value;
                if (finite(position) && finite(native_target) && finite(native_eye) &&
                    finite(camera_eye) && std::isfinite(distance2) && distance2 > .0001f) {
                    if (s.owner != player || s.node != camera || s.killer != actor || s.death != timestamp) {
                        s.owner = player; s.node = camera; s.killer = actor; s.death = timestamp;
                        s.target_node = read<uint32_t>(camera, 0x304);
                        for (size_t i = 0; i < 3; ++i)
                            s.target_offset[i] = native_target[i] - position[i];
                        constexpr auto degrees = 180.f / std::numbers::pi_v<float>;
                        s.radius = std::sqrt(distance2);
                        s.yaw = std::atan2(offset[1], offset[0]) * degrees;
                        s.pitch = std::clamp(-std::atan2(offset[2],
                            std::hypot(offset[0], offset[1])) * degrees, -80.f, 80.f);
                    }
                    // Follow the presented model with an independent mouse
                    // orbit. Native aim/turn/run animation cannot move it.
                    for (size_t i = 0; i < 3; ++i)
                        s.target[i] = position[i] + s.target_offset[i];
                    s.sampled_position = position;
                    constexpr auto radians = std::numbers::pi_v<float> / 180.f;
                    const auto horizontal = s.radius * std::cos(s.pitch * radians);
                    s.eye = {s.target[0] + horizontal * std::cos(s.yaw * radians),
                             s.target[1] + horizontal * std::sin(s.yaw * radians),
                             s.target[2] - s.radius * std::sin(s.pitch * radians)};
                    s.frame_ready = true;
                    // Update the native stack copies too: its near-target
                    // clearance arithmetic after the sweep reads these values.
                    std::memcpy(reinterpret_cast<void *>(from), s.target.data(), 12);
                    std::memcpy(reinterpret_cast<void *>(to), s.eye.data(), 12);
                }
            }
        }
        const auto result = abi::invoke(s.sweep->original(),
            {physics,radius,from,to,hit,normal,fraction,object,group,mask});
        s.obstructed = (result & 0xff) != 0;
        return result;
    }
    static uint32_t __cdecl recalculate(uint32_t camera) noexcept {
        auto &s = *active;
        if (s.frame_ready && s.node == camera) {
            if (s.obstructed) {
                // The original routine has applied its clearance/near-target
                // correction to the eye anchor. Use that result immediately;
                // filtering it would let the camera drift through walls.
                const auto anchor = read<uint32_t>(s.frame_owner, 0x3a4);
                s.eye = read<Vec>(anchor, 0x40);
            }
            // Sweep the actual orbit eye, including its authored initial child
            // offset. Adding a child offset after clearance could cross a wall.
            s.view_ready = true;
            s.apply(camera);
        }
        return abi::invoke(s.matrices->original(), {camera});
    }
    static uint32_t __cdecl register_camera(uint32_t camera, uint32_t context) noexcept {
        auto &s = *active;
        if (!s.registering && camera == s.node && s.view_ready) {
            // Local dead-state updates can precede the remote actor's movement
            // update. Reacquire the live owner and finish the camera against the
            // position actually drawn, not the preceding frame's scene matrix.
            const auto level = abi::invoke(s.get_level);
            const auto player = level ? abi::invoke(s.get_player, {level}) : 0;
            if (s.controllable(player)) {
                const auto actor = read<uint32_t>(player, 0x39c);
                const auto position = read<Vec>(actor, 0x54);
                if (finite(position) && position != s.sampled_position) {
                    s.registering = true;
                    // Reuse native validation, target exclusion and wall/near-
                    // target clearance at this new position. Translating the
                    // already-cleared eye alone could carry it through a wall.
                    update_camera(player);
                    s.registering = false;
                }
            }
        }
        s.apply(camera);
        return abi::invoke(s.registration->original(), {camera,context});
    }
};
NativeDeathCamera::Impl *NativeDeathCamera::Impl::active = nullptr;
NativeDeathCamera::NativeDeathCamera(const ElfImage &image, Admission admission)
    : impl_(std::make_unique<Impl>(image, admission)) {}
NativeDeathCamera::~NativeDeathCamera() = default;
bool NativeDeathCamera::controllable(uint32_t player) noexcept {
    const auto *s = Impl::active;
    return s && s->controllable(player);
}
void NativeDeathCamera::rotate(uint32_t player, float yaw, float pitch) noexcept {
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
    if (auto *s = Impl::active; s && s->controllable(player)) {
        s->yaw = std::remainder(s->yaw + yaw, 360.f);
        s->pitch = std::clamp(s->pitch + pitch, -80.f, 80.f);
        // Do not publish an unswept eye here. The next native camera update
        // applies these angles before its original collision query.
    }
}
void NativeDeathCamera::forget(uint32_t node) noexcept {
    if (auto *s = Impl::active; s && s->node == node) s->reset();
}
}
