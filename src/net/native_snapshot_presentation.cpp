#include "net/native_snapshot_presentation.h"
#include "net/native_consumables.h"
#include "camera/native_death_camera.h"
#include "abi/guest_hook.h"
#include "loader/sha256.h"
#include "net/community_match.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace nova::net {
namespace {
constexpr auto render_name =
    "_ZN13GameMpManager21GetNextRenderPositionEiRbN6glitch4core8vector3dIfEERSt"
    "6vectorI14PlayerPositionSaIS6_EES9_";
constexpr auto update_name =
    "_ZN21RemotePlayerComponent26UpdateRemotePlayerPositionEi";
constexpr auto mech_rotation_name =
    "_ZN19RemoteMechComponent11SetRotationERKN6glitch4core8vector3dIfEE";
struct MechLook {
  std::array<float, 3> angles;
  float presented_yaw;
  MechLook *previous;
};
thread_local MechLook *mech_look_scope = nullptr;
uint32_t ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T read(uint32_t p, uint32_t offset = 0) {
  T v;
  std::memcpy(&v, reinterpret_cast<const void *>(p + offset), sizeof(v));
  return v;
}
using Render = uint32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t, uint32_t) noexcept;
Render dispatch = nullptr;
// Reuse the existing native-artwork sret adapter convention: GCC returns a
// vector through the first argument and pops that argument with RET 4.
__declspec(naked) void render_entry() {
  __asm {
    push ebp
    mov ebp,esp
    push esi
    push edi
    sub esp,28
    and esp,-16
    mov edi,esp
    lea esi,[ebp+8]
    mov ecx,7
    cld
    rep movsd
    call dword ptr [dispatch]
    lea esp,[ebp-8]
    pop edi
    pop esi
    pop ebp
    ret 4
  }
}
} // namespace

struct NativeSnapshotPresentation::Impl {
  static Impl *active;
  std::mutex mutex;
  bool enabled = false, local = false;
  std::array<pc::History, pc::max_players> history;
  std::array<uint32_t, pc::max_players> generations{};
  std::array<uint32_t, pc::max_players> received{};
  std::array<uint32_t, pc::max_players> received_sequence{};
  std::array<bool, pc::max_players> have_received_sequence{};
  pc::Cadence cadence;
  uint32_t sequence = 0, sent_generation = 0;
  Send send;
  uint32_t clock, is_local, component, rotation;
  uint32_t game_manager = 0, gun_yaw = 0;
  std::unique_ptr<abi::GuestCall> render;
  std::unique_ptr<abi::GuestCall> mech_rotation;
  std::unique_ptr<abi::GuestCheckpoint> mech_look;
  std::unique_ptr<abi::GuestHook> client;
  std::unique_ptr<abi::GuestHook> remote_update;
  std::unique_ptr<camera::NativeDeathCamera> death_camera;
  std::unique_ptr<NativeConsumables> consumables;

  explicit Impl(const ElfImage &image, Send sender)
      : send(sender ? sender : &CommunityMatch::publish) {
    if (active ||
        sha256(image.source_bytes()) !=
            "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e")
      throw std::runtime_error(
          "Native snapshots require the supported Android library");
    clock = abi::require_function(image, "_Z12OS_GetOSTimev");
    is_local = abi::require_function(image, "_ZN9MpManager7IsLocalEv");
    component =
        abi::require_function(image, "_ZNK11CGameObject12GetComponentEi");
    rotation = abi::require_function(
        image, "_ZN11CGameObject11SetRotationERKN6glitch4core8vector3dIfEE");
    gun_yaw = abi::require_function(image,"_ZN22CMachineGunAIComponent10SetBodyYawEf");
    remote_update = std::make_unique<abi::GuestHook>(image,
        "_ZN21RemotePlayerComponent6UpdateEi",
        std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},
        reinterpret_cast<uint32_t>(&update_remote));
    render = std::make_unique<abi::GuestCall>(
        image, update_name, 0xcd, render_name,
        reinterpret_cast<uint32_t>(&render_entry));
    // Native MoveTo and the first part of SetRotation retain the received
    // positions/yaw: those inputs choose footwork, body turns and transitions.
    mech_rotation = std::make_unique<abi::GuestCall>(
        image, "_ZN19RemoteMechComponent6UpdateEi", 0x1aa, mech_rotation_name,
        reinterpret_cast<uint32_t>(&rotate_mech));
    // This verified MOV is after body-turn decisions and before the separate
    // upper-body look calculation. Only our scoped argument copy changes here.
    mech_look = std::make_unique<abi::GuestCheckpoint>(
        image, mech_rotation_name, 0x1c4,
        std::initializer_list<uint8_t>{0x8b,0x86,0xe4,0,0,0},
        reinterpret_cast<uint32_t>(&present_mech_look));
    client = std::make_unique<abi::GuestHook>(
        image, "_ZN13GameMpManager26ClientSendPositionsMessageEv",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x81, 0xec, 0x88, 0, 0,
                                       0},
        reinterpret_cast<uint32_t>(&capture));
    death_camera = std::make_unique<camera::NativeDeathCamera>(image, [] {
      auto *s = active;
      if (!s) return false;
      std::lock_guard lock(s->mutex);
      return s->enabled && !s->local;
    });
    consumables=std::make_unique<NativeConsumables>(image,[] {
      auto* s=active;
      if(!s)return false;
      std::lock_guard lock(s->mutex);
      return s->enabled && !s->local;
    });
    dispatch = &present;
    active = this;
  }
  ~Impl() {
    consumables.reset();
    remote_update.reset();
    death_camera.reset();
    client.reset();
    mech_look.reset();
    mech_rotation.reset();
    render.reset();
    active = nullptr;
    dispatch = nullptr;
  }

  static uint32_t __cdecl update_remote(uint32_t remote,uint32_t delta) noexcept {
    auto &s=*active;
    try {
      const auto gun=read<uint32_t>(remote,0x90);
      const auto slot=read<uint32_t>(remote,0x1c);
      std::optional<pc::Pose> pose;
      if (gun && s.game_manager && slot<pc::max_players) {
        const auto now=abi::invoke(s.clock)-read<uint32_t>(s.game_manager,0x5c);
        {
          std::lock_guard lock(s.mutex);
          if (s.enabled && !s.local && s.generations[slot])
            pose=s.history[slot].sample(now);
        }
        if (pose) {
          const auto ai=abi::invoke(s.component,{gun,0x43});
          if (ai) {
            // Fixed guns bypass GetNextRenderPosition. Feed the same admitted
            // timeline into their native yaw/posture controller before Update.
            auto yaw=std::remainder(pose->yaw-read<float>(remote,0x1e4),360.f);
            uint32_t bits;std::memcpy(&bits,&yaw,4);
            abi::invoke(s.gun_yaw,{ai,bits});
            std::memcpy(reinterpret_cast<void *>(remote+0x84),&pose->pitch,4);
          }
        }
      }
    } catch (...) {
      // Keep the original controller when the precise presentation is absent.
    }
    return abi::invoke(s.remote_update->original(),{remote,delta});
  }

  static void __cdecl present_mech_look() noexcept {
    if (auto *scope = mech_look_scope)
      scope->angles[2] = scope->presented_yaw;
  }

  static uint32_t __cdecl rotate_mech(uint32_t mech, uint32_t angles) noexcept {
    auto &s = *active;
    std::optional<pc::Pose> pose;
    try {
      const auto slot = read<uint32_t>(mech, 0xbc);
      if (s.game_manager && slot < pc::max_players &&
          slot != read<uint32_t>(s.game_manager, 0x3a04)) {
        const auto now = abi::invoke(s.clock) - read<uint32_t>(s.game_manager, 0x5c);
        std::lock_guard lock(s.mutex);
        if (s.enabled && !s.local && s.generations[slot])
          pose = s.history[slot].sample(now, 134);
      }
    } catch (...) {
      // Missing presentation data leaves the original controller untouched.
    }
    if (!pose)
      return abi::invoke(s.mech_rotation->original(), {mech, angles});
    const auto native_angle = [](float angle) {
      angle = std::fmod(angle, 360.f);
      return angle < 0 ? angle + 360.f : angle;
    };
    MechLook scope{read<std::array<float, 3>>(angles), native_angle(pose->yaw), mech_look_scope};
    // Pitch only drives native upper-body look. Keep yaw raw until its body
    // and footwork decisions finish, then let the native quaternion code use
    // the presented yaw. Never overwrite the received mech+c0/c8 state.
    scope.angles[0] = native_angle(pose->pitch);
    mech_look_scope = &scope;
    struct Restore {
      MechLook *previous;
      ~Restore() { mech_look_scope = previous; }
    } restore{scope.previous};
    return abi::invoke(s.mech_rotation->original(), {mech, ptr(scope.angles.data())});
  }

  static uint32_t __cdecl capture(uint32_t manager) noexcept {
    auto &s = *active;
    s.game_manager = manager;
    try {
      const auto slot = read<uint32_t>(manager, 0x3a04);
      const bool local = abi::invoke_bool(s.is_local, {manager});
      // Wi-Fi remains native until its own peer capability negotiation admits
      // the shared snapshot format. Online admission comes only from the grant.
      const auto generation = slot < pc::max_players && !local
                                  ? NativeSnapshotPresentation::generation(
                                        static_cast<uint8_t>(slot), false)
                                  : 0;
      if (!generation)
        return abi::invoke(s.client->original(), {manager});
      const auto players = read<uint32_t>(manager, 0x3bdc);
      // Precise snapshots replace native position messages, including their
      // connectivity accounting. Transfer only fresh accepted samples on the
      // game thread; rendering a held pose must never keep an outage alive.
      {
        std::lock_guard lock(s.mutex);
        for (uint32_t i = 0; i < pc::max_players; ++i) {
          const auto count = std::exchange(s.received[i], 0);
          if (!count || i == slot || !players || !s.generations[i])
            continue;
          const auto remote = read<uint32_t>(players, i * 4);
          if (!remote)
            continue;
          auto &counter = *reinterpret_cast<uint32_t *>(remote + 0x140);
          counter = static_cast<uint32_t>(std::min<uint64_t>(
              uint64_t(counter) + count, 0x7fffffff));
        }
      }
      const auto player = players ? read<uint32_t>(players, slot * 4) : 0;
      const auto object = player ? read<uint32_t>(player) : 0;
      if (!object)
        return 0;
      const auto mech = abi::invoke(s.component, {object, 0x34});
      if (read<uint32_t>(player, 0x144) || (mech && read<uint8_t>(mech, 0xd)))
        return abi::invoke(s.client->original(), {manager});
      if (read<uint8_t>(player, 0x18))
        return 0;
      const auto begin = read<uint32_t>(player, 0x114);
      const auto end = read<uint32_t>(player, 0x118);
      if (!begin || end < begin || end - begin < 16 || (end - begin) % 16)
        return 0;
      const auto yaw = pc::encode_angle(read<float>(object, 0x68));
      const auto pitch = pc::encode_angle(read<float>(object, 0x60));
      if (!yaw || !pitch)
        return 0;
      pc::Snapshot snapshot;
      snapshot.slot = static_cast<uint8_t>(slot);
      snapshot.generation = generation;
      snapshot.time = abi::invoke(s.clock) - read<uint32_t>(manager, 0x5c);
      std::memcpy(snapshot.position.data(), reinterpret_cast<void *>(end - 16),
                  12);
      snapshot.yaw = *yaw;
      snapshot.pitch = *pitch;
      if (!snapshot.valid())
        return 0;
      const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
      {
        std::lock_guard lock(s.mutex);
        if (!s.enabled || s.local || s.generations[slot] != generation)
          return 0;
        if (s.sent_generation != generation) {
          s.sent_generation = generation;
          s.sequence = 0;
          s.cadence = {};
        }
        if (!s.cadence.due(static_cast<uint64_t>(now)))
          return 0;
        snapshot.sequence = ++s.sequence;
      }
      s.send(
          snapshot); // Loss/EAGAIN drops this sample; never queue stale poses.
    } catch (...) {
      // Do not leak host exceptions or fall back to coarse native on-foot data.
    }
    return 0;
  }

  static uint32_t present(uint32_t out, uint32_t manager, uint32_t slot,
                          uint32_t changed, uint32_t current,
                          uint32_t positions, uint32_t rendered) noexcept {
    auto &s = *active;
    const auto original = [&] {
      return abi::invoke(s.render->original(), {out, manager, slot, changed,
                                                current, positions, rendered});
    };
    try {
      std::optional<pc::Pose> pose;
      bool admitted;
      const bool local = abi::invoke_bool(s.is_local, {manager});
      const auto now = abi::invoke(s.clock) - read<uint32_t>(manager, 0x5c);
      {
        std::lock_guard lock(s.mutex);
        admitted = s.enabled && s.local == local && slot < pc::max_players &&
                   s.generations[slot] != 0;
        if (admitted)
          pose = s.history[slot].sample(now);
      }
      if (!admitted)
        return original();
      const auto players = read<uint32_t>(manager, 0x3bdc);
      const auto player = players ? read<uint32_t>(players, slot * 4) : 0;
      const auto object = player ? read<uint32_t>(player) : 0;
      const auto remote = object ? abi::invoke(s.component, {object, 0x2c}) : 0;
      if (!remote || read<uint32_t>(remote, 0x1c) != slot ||
          read<uint32_t>(remote, 0x90))
        return original();
      if (read<uint32_t>(remote, 0x94))
        return original();
      if (!pose) {
        std::memcpy(reinterpret_cast<void *>(out),
                    reinterpret_cast<const void *>(current), 12);
        *reinterpret_cast<uint8_t *>(changed) = 0;
        return out;
      }
      // The original receiver preserves object X/Y and writes yaw to Z. Pitch
      // belongs to the remote skeletal component, not the object's X rotation.
      const std::array<float, 3> angles{read<float>(object, 0x60),
                                        read<float>(object, 0x64), pose->yaw};
      abi::invoke(s.rotation, {object, ptr(angles.data())});
      std::memcpy(reinterpret_cast<void *>(remote + 0x84), &pose->pitch,
                  sizeof(float));
      std::memcpy(reinterpret_cast<void *>(out), pose->position.data(), 12);
      *reinterpret_cast<uint8_t *>(changed) = 1;
      return out;
    } catch (...) {
      // Never throw through the original game. Holding the current presentation
      // is preferable to feeding stale native interpolation after admission.
      std::memcpy(reinterpret_cast<void *>(out),
                  reinterpret_cast<const void *>(current), 12);
      *reinterpret_cast<uint8_t *>(changed) = 0;
      return out;
    }
  }
};
NativeSnapshotPresentation::Impl *NativeSnapshotPresentation::Impl::active =
    nullptr;
NativeSnapshotPresentation::NativeSnapshotPresentation(const ElfImage &image,
                                                       Send send)
    : impl_(std::make_unique<Impl>(image, send)) {}
NativeSnapshotPresentation::~NativeSnapshotPresentation() = default;
void NativeSnapshotPresentation::begin(bool local) {
  if (auto *s = Impl::active) {
    std::lock_guard lock(s->mutex);
    s->enabled = true;
    s->local = local;
    s->generations.fill(0);
    s->sent_generation = s->sequence = 0;
    s->received.fill(0);
    s->have_received_sequence.fill(false);
    s->cadence = {};
    for (uint8_t slot = 0; slot < pc::max_players; ++slot)
      s->history[slot].reset(slot, 0);
  }
}
void NativeSnapshotPresentation::clear() {
  if (auto *s = Impl::active) {
    std::lock_guard lock(s->mutex);
    s->enabled = false;
    s->generations.fill(0);
    s->received.fill(0);
    s->have_received_sequence.fill(false);
    for (uint8_t slot = 0; slot < pc::max_players; ++slot)
      s->history[slot].reset(slot, 0);
  }
}
bool NativeSnapshotPresentation::spawn(uint8_t slot, uint32_t generation) {
  if (auto *s = Impl::active; s && slot < pc::max_players && generation) {
    std::lock_guard lock(s->mutex);
    if (!s->enabled ||
        (s->generations[slot] && !pc::newer(generation, s->generations[slot])))
      return false;
    s->generations[slot] = generation;
    s->received[slot] = 0;
    s->have_received_sequence[slot] = false;
    s->history[slot].reset(slot, generation);
    return true;
  }
  return false;
}
uint32_t NativeSnapshotPresentation::generation(uint8_t slot, bool local) {
  if (auto *s = Impl::active; s && slot < pc::max_players) {
    std::lock_guard lock(s->mutex);
    if (s->enabled && s->local == local)
      return s->generations[slot];
  }
  return 0;
}
void NativeSnapshotPresentation::receive(
    std::span<const pc::Snapshot> records) {
  if (auto *s = Impl::active) {
    std::lock_guard lock(s->mutex);
    if (s->enabled)
      for (const auto &record : records) {
        const auto slot = record.slot;
        if (slot >= pc::max_players || !record.valid() ||
            record.generation != s->generations[slot] ||
            (s->have_received_sequence[slot] &&
             !pc::newer(record.sequence, s->received_sequence[slot])))
          continue;
        s->received_sequence[slot] = record.sequence;
        s->have_received_sequence[slot] = true;
        // Fresh authenticated server traffic proves connectivity even when
        // its actor sample is held (dead, waiting, mounted or stationary).
        // Replayed sequences and rendering a cached pose never count.
        s->received[slot] = std::min<uint32_t>(s->received[slot] + 1, 0x7fffffff);
        s->history[slot].push(record);
      }
  }
}
} // namespace nova::net
