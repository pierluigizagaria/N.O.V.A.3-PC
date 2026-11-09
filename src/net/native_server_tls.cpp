#include "net/native_server_tls.h"
#include "bionic/descriptors.h"
#include "loader/sha256.h"
#include "net/community_lobby.h"
#include "net/community_match.h"
#include "net/native_snapshot_presentation.h"
#include "net/native_friends_refresh.h"
#include "net/tls_stream.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <ws2tcpip.h>

namespace nova::net {
namespace {
template <class T> uint32_t ptr(T *p) { return reinterpret_cast<uint32_t>(p); }
constexpr uint32_t failure = 0xffffffff;
} // namespace
struct NativeServerTls::Entry {
  std::mutex guard;
  std::weak_ptr<bionic::Descriptor> descriptor;
  TlsStream stream;
  LoginCredentialHeader header;
  bool invalid_header = false;
  Entry(const std::shared_ptr<bionic::Descriptor> &d, const ServerConfig &c,
        void *engine)
      : descriptor(d), stream(d->socket, c.address, engine) {}
};
NativeServerTls *NativeServerTls::active_ = nullptr;
NativeServerTls::NativeServerTls(const ElfImage &image,
                                 const ServerConfig &config, void *engine,
                                 uint16_t lobby_port, uint16_t service_port)
    : config_(config), chain_engine_(engine),
      literal_(server_ipv4(config.address)) {
  if (!config.present || config.mode != ServerMode::online)
    return;
  if (active_)
    throw std::runtime_error("Native server TLS already configured");
  if (sha256(image.source_bytes()) !=
      "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e")
    throw std::runtime_error(
        "Native server TLS requires the supported Android library");
  if (InetPtonA(AF_INET, config.resolved_address.c_str(), &peer_address_) !=
          1 ||
      (!literal_ && !server_hostname(config.address)))
    throw std::runtime_error(
        "Native server TLS requires a resolved server selection");
  get_fd_ = abi::require_function(image, "SSL_get_fd");
  set_verify_ = abi::require_function(image, "SSL_set_verify");
  allocate_array_ = abi::require_function(image, "_Znaj");
  active_ = this;
  try {
    // This one original cache call copies a binary IPv4 address as a string.
    // Preserve its allocator/ownership, replacing only that copy with 4 bytes.
    dns_copy_ = std::make_unique<abi::GuestCall>(
        image, "_Z26threadGetHostByNameProcessPv", 0x10e, "XP_API_STRNEW",
        ptr(&copy_ipv4));
    connect_ = std::make_unique<abi::GuestHook>(
        image, "SSL_connect",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x83, 0xec, 0x18},
        ptr(&connect));
    handshake_ = std::make_unique<abi::GuestHook>(
        image, "SSL_do_handshake",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x83, 0xec, 0x28},
        ptr(&do_handshake));
    result_ = std::make_unique<abi::GuestHook>(
        image, "SSL_get_verify_result",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08},
        ptr(&verify_result));
    read_ = std::make_unique<abi::GuestHook>(
        image, "SSL_read",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x53, 0x83, 0xec,
                                       0x24},
        ptr(&read));
    write_ = std::make_unique<abi::GuestHook>(
        image, "SSL_write",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x53, 0x83, 0xec,
                                       0x24},
        ptr(&write));
    error_ = std::make_unique<abi::GuestHook>(
        image, "SSL_get_error",
        std::initializer_list<uint8_t>{0x55, 0x31, 0xc0, 0x89, 0xe5},
        ptr(&get_error));
    pending_ = std::make_unique<abi::GuestHook>(
        image, "SSL_pending",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x83, 0xec, 0x18},
        ptr(&pending));
    shutdown_ = std::make_unique<abi::GuestHook>(
        image, "SSL_shutdown",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x53, 0x83, 0xec,
                                       0x24},
        ptr(&shutdown));
    free_ = std::make_unique<abi::GuestHook>(
        image, "SSL_free",
        std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x56, 0x53},
        ptr(&free));
    presentation_ = std::make_unique<NativeSnapshotPresentation>(image);
    friends_ = std::make_unique<NativeFriendsRefresh>(image);
    match_ = std::make_unique<CommunityMatch>(config);
    lobby_ = std::make_unique<CommunityLobby>(
        config, &NativeServerTls::login_credential, engine, lobby_port,
        service_port);
  } catch (...) {
    active_ = nullptr;
    throw;
  }
  std::fprintf(stderr, "native server TLS: Windows TLS 1.2 for %s, policy=%s\n",
               config.address.c_str(),
               literal_ ? "explicit IP"
                        : "Windows certificate and hostname validation");
}
NativeServerTls::~NativeServerTls() {
  friends_.reset();
  lobby_.reset();
  match_.reset();
  presentation_.reset();
  dns_copy_.reset();
  // Hooks are installed/removed only while guest execution is quiescent.
  free_.reset();
  shutdown_.reset();
  pending_.reset();
  error_.reset();
  write_.reset();
  read_.reset();
  result_.reset();
  handshake_.reset();
  connect_.reset();
  entries_.clear();
  if (active_ == this)
    active_ = nullptr;
}
uint32_t __cdecl NativeServerTls::copy_ipv4(uint32_t address) noexcept {
  try {
    if (!active_ || !address)
      return 0;
    const auto copy = abi::invoke(active_->allocate_array_, {4});
    if (copy)
      std::memcpy(reinterpret_cast<void *>(copy),
                  reinterpret_cast<const void *>(address), 4);
    return copy;
  } catch (...) {
    return 0;
  }
}
bool NativeServerTls::selected(uint32_t ssl) const {
  const auto d =
      bionic::descriptor(static_cast<int>(abi::invoke(get_fd_, {ssl})));
  if (!d)
    return false;
  std::lock_guard lock(d->guard);
  sockaddr_in peer{};
  int size = sizeof(peer);
  return d->is_socket() &&
         getpeername(d->socket, reinterpret_cast<sockaddr *>(&peer), &size) ==
             0 &&
         peer.sin_family == AF_INET && peer.sin_addr.s_addr == peer_address_;
}
std::shared_ptr<NativeServerTls::Entry> NativeServerTls::entry(uint32_t ssl,
                                                               bool create) {
  {
    std::lock_guard lock(guard_);
    auto it = entries_.find(ssl);
    if (it != entries_.end()) {
      if (!it->second)
        throw std::runtime_error("TLS initialization previously failed");
      return it->second;
    }
  }
  if (!create || !selected(ssl))
    return {};
  const auto d =
      bionic::descriptor(static_cast<int>(abi::invoke(get_fd_, {ssl})));
  if (!d)
    throw std::runtime_error("TLS descriptor expired");
  CommunityLobby::use_native_tls(d);
  std::lock_guard lock(guard_);
  auto &value = entries_[ssl];
  if (!value)
    value = std::make_shared<Entry>(d, config_, chain_engine_);
  return value;
}
uint32_t NativeServerTls::handshake(uint32_t ssl, uint32_t original) {
  if (auto e = entry(ssl, true)) {
    std::lock_guard lock(e->guard);
    auto d = e->descriptor.lock();
    if (!d)
      return failure;
    std::lock_guard socket_lock(d->guard);
    return static_cast<uint32_t>(e->stream.connect());
  }
  abi::invoke(set_verify_, {ssl, 1, 0});
  return abi::invoke(original, {ssl});
}
uint32_t NativeServerTls::transfer(uint32_t ssl, uint32_t data, uint32_t size,
                                   bool writing) {
  if (auto e = entry(ssl)) {
    std::lock_guard lock(e->guard);
    auto d = e->descriptor.lock();
    if (!d || e->invalid_header)
      return failure;
    std::lock_guard socket_lock(d->guard);
    if (size > std::numeric_limits<int>::max())
      return failure;
    const auto result = static_cast<uint32_t>(
        writing ? e->stream.write(reinterpret_cast<const void *>(data),
                                  static_cast<int>(size))
                : e->stream.read(reinterpret_cast<void *>(data),
                                 static_cast<int>(size)));
    if (!writing && static_cast<int32_t>(result) > 0) {
      const auto observed =
          e->header.feed(reinterpret_cast<const void *>(data), result);
      if (observed == LoginCredentialHeader::Result::invalid) {
        e->invalid_header = true;
        return failure;
      }
      if (observed == LoginCredentialHeader::Result::available) {
        std::lock_guard cache_lock(guard_);
        login_ = e->header.credential();
      }
    }
    return result;
  }
  return abi::invoke(writing ? write_->original() : read_->original(),
                     {ssl, data, size});
}
uint32_t __cdecl NativeServerTls::connect(uint32_t ssl) noexcept {
  try {
    return active_ ? active_->handshake(ssl, active_->connect_->original())
                   : failure;
  } catch (...) {
    return failure;
  }
}
uint32_t __cdecl NativeServerTls::do_handshake(uint32_t ssl) noexcept {
  try {
    return active_ ? active_->handshake(ssl, active_->handshake_->original())
                   : failure;
  } catch (...) {
    return failure;
  }
}
uint32_t __cdecl NativeServerTls::read(uint32_t ssl, uint32_t data,
                                       uint32_t size) noexcept {
  try {
    return active_ ? active_->transfer(ssl, data, size, false) : failure;
  } catch (...) {
    return failure;
  }
}
uint32_t __cdecl NativeServerTls::write(uint32_t ssl, uint32_t data,
                                        uint32_t size) noexcept {
  try {
    return active_ ? active_->transfer(ssl, data, size, true) : failure;
  } catch (...) {
    return failure;
  }
}
uint32_t __cdecl NativeServerTls::get_error(uint32_t ssl,
                                            uint32_t result) noexcept {
  try {
    if (!active_)
      return 1;
    if (auto e = active_->entry(ssl)) {
      std::lock_guard lock(e->guard);
      if (static_cast<int32_t>(result) > 0)
        return 0;
      if (e->descriptor.expired() || e->invalid_header)
        return 1;
      switch (e->stream.error()) {
      case TlsStream::Error::want_read:
        return 2;
      case TlsStream::Error::want_write:
        return 3;
      case TlsStream::Error::closed:
        return 6;
      default:
        return 1;
      }
    }
    return abi::invoke(active_->error_->original(), {ssl, result});
  } catch (...) {
    return 1;
  }
}
uint32_t __cdecl NativeServerTls::verify_result(uint32_t ssl) noexcept {
  try {
    if (!active_)
      return 50;
    if (auto e = active_->entry(ssl)) {
      std::lock_guard lock(e->guard);
      return e->stream.verified() && !e->invalid_header ? 0u : 50u;
    }
    if (active_->selected(ssl))
      return 50;
    return abi::invoke(active_->result_->original(), {ssl});
  } catch (...) {
    return 50;
  }
}
uint32_t __cdecl NativeServerTls::pending(uint32_t ssl) noexcept {
  try {
    if (!active_)
      return 0;
    if (auto e = active_->entry(ssl)) {
      std::lock_guard lock(e->guard);
      return static_cast<uint32_t>(e->stream.pending());
    }
    return abi::invoke(active_->pending_->original(), {ssl});
  } catch (...) {
    return 0;
  }
}
uint32_t __cdecl NativeServerTls::shutdown(uint32_t ssl) noexcept {
  try {
    if (!active_)
      return failure;
    if (auto e = active_->entry(ssl)) {
      std::lock_guard lock(e->guard);
      auto d = e->descriptor.lock();
      if (!d)
        return failure;
      std::lock_guard socket_lock(d->guard);
      return static_cast<uint32_t>(e->stream.shutdown());
    }
    return abi::invoke(active_->shutdown_->original(), {ssl});
  } catch (...) {
    return failure;
  }
}
uint32_t __cdecl NativeServerTls::free(uint32_t ssl) noexcept {
  try {
    if (!active_)
      return 0;
    {
      std::lock_guard lock(active_->guard_);
      active_->entries_.erase(ssl);
    }
    return abi::invoke(active_->free_->original(), {ssl});
  } catch (...) {
    return 0;
  }
}
std::optional<LoginCredential> NativeServerTls::login_credential() {
  if (!active_)
    return {};
  std::lock_guard lock(active_->guard_);
  return active_->login_;
}
} // namespace nova::net
