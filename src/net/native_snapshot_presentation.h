#pragma once
#include "loader/elf_image.h"
#include "net/pc_snapshot.h"
#include <memory>

namespace nova::net {
// Captures precise on-foot proposals and supplies the existing remote-player
// position/animation path with one pose timeline. Admission and life generations
// come from the match transport;
// received snapshots cannot create a new generation themselves.
class NativeSnapshotPresentation {
public:
  using Send = bool (*)(const pc::Snapshot &);
  // The optional sink isolates native capture tests; production uses the
  // admitted CommunityMatch descriptor. It is not a player/operator setting.
  explicit NativeSnapshotPresentation(const ElfImage &, Send = nullptr);
  ~NativeSnapshotPresentation();
  NativeSnapshotPresentation(const NativeSnapshotPresentation &) = delete;
  NativeSnapshotPresentation &
  operator=(const NativeSnapshotPresentation &) = delete;
  static void begin(bool local);
  static void clear();
  static bool spawn(uint8_t slot, uint32_t generation);
  static uint32_t generation(uint8_t slot, bool local);
  static void receive(std::span<const pc::Snapshot>);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace nova::net
