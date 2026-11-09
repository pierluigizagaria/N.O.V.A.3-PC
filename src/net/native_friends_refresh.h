#pragma once
#include "loader/elf_image.h"
#include <memory>

namespace nova::net {
// Refresh the original online friends menu through its existing SDK request.
// Installed only for the selected community server; local Wi-Fi is unchanged.
class NativeFriendsRefresh {
public:
  explicit NativeFriendsRefresh(const ElfImage &);
  ~NativeFriendsRefresh();
  NativeFriendsRefresh(const NativeFriendsRefresh &) = delete;
  NativeFriendsRefresh &operator=(const NativeFriendsRefresh &) = delete;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace nova::net
