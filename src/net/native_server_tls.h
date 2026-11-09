#pragma once
#include "abi/guest_hook.h"
#include "net/login_credential.h"
#include "net/server_config.h"
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace nova::net {
class CommunityLobby;
class CommunityMatch;
class NativeSnapshotPresentation;
class NativeFriendsRefresh;
// Destination-scoped Windows TLS adapter at the existing OpenSSL boundary.
// A hostname always uses Windows chain/name validation, even after DNS
// resolution.
class NativeServerTls {
public:
  // chain_engine is an optional isolated trust engine for tests; normal clients
  // use nullptr (Windows trust). No certificate path is accepted by this
  // policy.
  NativeServerTls(const ElfImage &, const ServerConfig &,
                  void *chain_engine = nullptr, uint16_t lobby_port = 18443,
                  uint16_t service_port = 18443);
  ~NativeServerTls();
  NativeServerTls(const NativeServerTls &) = delete;
  NativeServerTls &operator=(const NativeServerTls &) = delete;
  // In-memory credential from the existing verified password-login response.
  // Used only by the selected community server's lobby/match adapter.
  static std::optional<LoginCredential> login_credential();

private:
  bool selected(uint32_t ssl) const;
  struct Entry;
  std::shared_ptr<Entry> entry(uint32_t ssl, bool create = false);
  uint32_t handshake(uint32_t ssl, uint32_t original);
  uint32_t transfer(uint32_t ssl, uint32_t data, uint32_t size, bool writing);
  static uint32_t __cdecl connect(uint32_t ssl) noexcept;
  static uint32_t __cdecl do_handshake(uint32_t ssl) noexcept;
  static uint32_t __cdecl verify_result(uint32_t ssl) noexcept;
  static uint32_t __cdecl read(uint32_t ssl, uint32_t data,
                               uint32_t size) noexcept;
  static uint32_t __cdecl write(uint32_t ssl, uint32_t data,
                                uint32_t size) noexcept;
  static uint32_t __cdecl get_error(uint32_t ssl, uint32_t result) noexcept;
  static uint32_t __cdecl pending(uint32_t ssl) noexcept;
  static uint32_t __cdecl shutdown(uint32_t ssl) noexcept;
  static uint32_t __cdecl free(uint32_t ssl) noexcept;
  static uint32_t __cdecl copy_ipv4(uint32_t address) noexcept;
  ServerConfig config_;
  void *chain_engine_ = nullptr;
  bool literal_ = false;
  uint32_t peer_address_ = 0;
  uint32_t get_fd_ = 0, set_verify_ = 0;
  uint32_t allocate_array_ = 0;
  std::unique_ptr<abi::GuestCall> dns_copy_;
  std::unique_ptr<abi::GuestHook> connect_, handshake_, result_;
  std::unique_ptr<abi::GuestHook> read_, write_, error_, pending_, shutdown_,
      free_;
  std::mutex guard_;
  std::unordered_map<uint32_t, std::shared_ptr<Entry>> entries_;
  std::optional<LoginCredential> login_;
  std::unique_ptr<CommunityLobby> lobby_;
  std::unique_ptr<CommunityMatch> match_;
  std::unique_ptr<NativeSnapshotPresentation> presentation_;
  std::unique_ptr<NativeFriendsRefresh> friends_;
  static NativeServerTls *active_;
};
} // namespace nova::net
