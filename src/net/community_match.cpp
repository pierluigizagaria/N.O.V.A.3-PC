#include "net/community_match.h"
#include "net/native_snapshot_presentation.h"
#include "net/network.h"
#include <cstring>
#include <stdexcept>
#include <ws2tcpip.h>

namespace nova::net {
struct CommunityMatch::Grant {
  std::mutex guard;
  std::weak_ptr<bionic::Descriptor> owner;
  std::weak_ptr<bionic::Descriptor> udp;
  std::array<uint8_t, 16> id;
  sockaddr_in endpoint{};
  std::unique_ptr<SecureDatagram> channel;
  std::vector<uint8_t> native;
  sockaddr_storage native_address{};
  int native_length = 0;
  Grant(const std::shared_ptr<bionic::Descriptor> &d, const MatchControl &c)
      : owner(d), id(c.ticket.id),
        channel(std::make_unique<SecureDatagram>(c.ticket)) {
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(c.port);
    std::memcpy(&endpoint.sin_addr, c.address.data(), 4);
  }
  bool matches(const sockaddr *address, int length) const {
    if (!address || length != sizeof(sockaddr_in) ||
        address->sa_family != AF_INET)
      return false;
    const auto &peer = *reinterpret_cast<const sockaddr_in *>(address);
    return peer.sin_addr.s_addr == endpoint.sin_addr.s_addr &&
           peer.sin_port == endpoint.sin_port;
  }
  bool valid() {
    if (owner.expired() && channel) {
      channel.reset();
      NativeSnapshotPresentation::clear();
    }
    return channel != nullptr;
  }
  void revoke() {
    std::lock_guard lock(guard);
    channel.reset();
    native.clear();
  }
};
CommunityMatch *CommunityMatch::active_ = nullptr;
CommunityMatch::CommunityMatch(const ServerConfig &config) {
  if (active_ ||
      InetPtonA(AF_INET, config.resolved_address.c_str(), &address_) != 1)
    throw std::runtime_error("Invalid community match transport");
  active_ = this;
}
CommunityMatch::~CommunityMatch() {
  NativeSnapshotPresentation::clear();
  if (active_ == this)
    active_ = nullptr;
}
bool CommunityMatch::control(const std::shared_ptr<bionic::Descriptor> &owner,
                             const MatchControl &control) {
  if (!active_ || !owner)
    return false;
  std::lock_guard lock(active_->guard_);
  auto &pending = active_->pending_;
  if (!control.grant) {
    if (pending && pending->id == control.ticket.id &&
        pending->owner.lock() == owner) {
      pending->revoke();
      NativeSnapshotPresentation::clear();
    }
    return true;
  }
  uint32_t address = 0;
  std::memcpy(&address, control.address.data(), 4);
  if (address != active_->address_ ||
      (pending && pending->id == control.ticket.id))
    return false; // Never reset counters under an already installed key.
  auto replacement = std::make_shared<Grant>(owner, control);
  if (pending)
    pending->revoke();
  pending = std::move(replacement);
  NativeSnapshotPresentation::begin(false);
  return true;
}
std::shared_ptr<CommunityMatch::Grant>
CommunityMatch::find(const std::shared_ptr<bionic::Descriptor> &d,
                     const sockaddr *address, int length) {
  if (d->type != SOCK_DGRAM)
    return {};
  std::lock_guard lock(guard_);
  for (auto it = bindings_.begin(); it != bindings_.end();) {
    if (it->second.descriptor.expired())
      it = bindings_.erase(it);
    else
      ++it;
  }
  const auto it = bindings_.find(d->serial);
  if (it != bindings_.end()) {
    // A new grant may reuse the original Comms descriptor. Swap only when its
    // first send addresses the new endpoint, and never on an incoming packet.
    if (pending_ && pending_ != it->second.grant &&
        !pending_->owner.expired() && pending_->matches(address, length))
      it->second.grant = pending_;
    if (it->second.grant->matches(address, length)) {
      std::lock_guard lock(it->second.grant->guard);
      it->second.grant->udp = d;
    }
    return it->second.grant;
  }
  if (!pending_ || pending_->owner.expired() ||
      !pending_->matches(address, length))
    return {};
  bindings_.emplace(d->serial, Binding{d, pending_});
  {
    std::lock_guard lock(pending_->guard);
    pending_->udp = d;
  }
  return pending_;
}
bool CommunityMatch::publish(const pc::Snapshot &snapshot) try {
  if (!active_)
    return false;
  std::shared_ptr<Grant> grant;
  {
    std::lock_guard lock(active_->guard_);
    grant = active_->pending_;
  }
  if (!grant)
    return false;
  std::lock_guard lock(grant->guard);
  const auto descriptor = grant->udp.lock();
  if (!grant->valid() || !descriptor ||
      NativeSnapshotPresentation::generation(snapshot.slot, false) !=
          snapshot.generation)
    return false;
  const auto body = pc::encode_snapshots(std::span(&snapshot, 1));
  if (!body)
    return false;
  const auto envelope = pc::encode(pc::snapshots, *body);
  const auto encrypted = grant->channel->seal(*envelope);
  if (!encrypted)
    return false;
  const auto count = ::sendto(
      descriptor->socket, reinterpret_cast<const char *>(encrypted->data()),
      static_cast<int>(encrypted->size()), 0,
      reinterpret_cast<const sockaddr *>(&grant->endpoint),
      sizeof(grant->endpoint));
  return count == static_cast<int>(encrypted->size());
} catch (...) {
  return false;
}
std::optional<int>
CommunityMatch::send(const std::shared_ptr<bionic::Descriptor> &d,
                     const void *data, uint32_t size, int flags,
                     const sockaddr *address, int length) try {
  if (!active_)
    return {};
  const auto grant = active_->find(d, address, length);
  if (!grant)
    return {};
  std::lock_guard lock(grant->guard);
  if (!grant->valid() || !grant->matches(address, length))
    return bionic::fail(104);
  if (flags & ~0x4000)
    return bionic::fail(95);
  if ((!data && size) || size > 2048)
    return bionic::fail(!data ? 14 : 90);
  const auto envelope =
      pc::encode(pc::native, {static_cast<const uint8_t *>(data), size});
  if (!envelope)
    return bionic::fail(90);
  const auto encrypted = grant->channel->seal(*envelope);
  if (!encrypted)
    return bionic::fail(104);
  const auto n =
      ::sendto(d->socket, reinterpret_cast<const char *>(encrypted->data()),
               static_cast<int>(encrypted->size()), 0, address, length);
  if (n == SOCKET_ERROR)
    return bionic::fail(socket_error(WSAGetLastError()));
  return n == static_cast<int>(encrypted->size()) ? static_cast<int>(size)
                                                  : bionic::fail(5);
} catch (...) {
  return bionic::fail(12);
}
std::optional<int>
CommunityMatch::receive(const std::shared_ptr<bionic::Descriptor> &d,
                        void *data, uint32_t size, int flags,
                        sockaddr_storage &address, int &length) {
  return receive_impl(d, data, size, flags, address, length, false);
}
std::optional<int> CommunityMatch::readable(const std::shared_ptr<bionic::Descriptor> &d) {
  sockaddr_storage address{};
  int length = sizeof(address);
  const auto saved = bionic::guest_errno;
  const auto result = receive_impl(d, nullptr, 0, 0, address, length, true);
  bionic::guest_errno = saved;
  return result;
}
std::optional<int>
CommunityMatch::receive_impl(const std::shared_ptr<bionic::Descriptor> &d,
                        void *data, uint32_t size, int flags,
                        sockaddr_storage &address, int &length, bool polling) try {
  if (!active_)
    return {};
  const auto grant = active_->find(d);
  if (!grant)
    return {};
  std::lock_guard lock(grant->guard);
  if (!grant->valid()) {
    grant->native.clear();
    // -2 marks a revoked binding: poll must stop watching its raw UDP socket.
    return polling ? -2 : bionic::fail(104);
  }
  if (flags & ~0x4000)
    return bionic::fail(95);
  if (!data && size)
    return bionic::fail(14);
  if (!grant->native.empty()) {
    if (polling) return 1;
    const auto copied = std::min<size_t>(size, grant->native.size());
    if (copied) std::memcpy(data, grant->native.data(), copied);
    address = grant->native_address;
    length = grant->native_length;
    grant->native.clear();
    return static_cast<int>(copied);
  }
  // Read the complete datagram before validating its smaller protocol limit;
  // a hostile oversize packet must never surface WSAEMSGSIZE as native data.
  std::array<uint8_t, 65535> encrypted;
  for (unsigned drained = 0; drained < 64; ++drained) {
  if (polling || drained) {
    // Consumed PC control/snapshot records must not end the native receive
    // loop while another datagram is already queued. Otherwise two 30 Hz
    // records per tick accumulate behind a client's lower render rate.
    // Poll only after the first receive so blocking descriptor semantics stay
    // intact, and never block while draining internal records.
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(d->socket, &ready);
    timeval timeout{};
    const auto available = ::select(0, &ready, nullptr, nullptr, &timeout);
    if (available == SOCKET_ERROR)
      return bionic::fail(socket_error(WSAGetLastError()));
    if (!available)
      return polling ? 0 : bionic::fail(11);
  }
  length = sizeof(address);
  const auto n =
      ::recvfrom(d->socket, reinterpret_cast<char *>(encrypted.data()),
                 static_cast<int>(encrypted.size()), 0,
                 reinterpret_cast<sockaddr *>(&address), &length);
  if (n == SOCKET_ERROR)
    return bionic::fail(socket_error(WSAGetLastError()));
  if (!grant->matches(reinterpret_cast<sockaddr *>(&address), length))
    continue;
  const auto plain =
      grant->channel->open({encrypted.data(), static_cast<size_t>(n)});
  if (!plain)
    continue;
  const auto envelope = pc::decode(*plain);
  if (!envelope)
    continue;
  if (envelope->kind == pc::lives) {
    const auto lives = pc::decode_lives(envelope->payload);
    for (const auto &life : *lives)
      NativeSnapshotPresentation::spawn(life.slot, life.generation);
    continue;
  }
  if (envelope->kind == pc::snapshots) {
    NativeSnapshotPresentation::receive(
        *pc::decode_snapshots(envelope->payload));
    continue;
  }
  if (envelope->kind != pc::native)
    continue;
  if (polling) {
    grant->native.assign(envelope->payload.begin(), envelope->payload.end());
    grant->native_address = address;
    grant->native_length = length;
    return 1;
  }
  const auto copied = std::min<size_t>(size, envelope->payload.size());
  if (copied)
    std::memcpy(data, envelope->payload.data(), copied);
  return static_cast<int>(copied);
  }
  return polling ? 0 : bionic::fail(11);
} catch (...) {
  return bionic::fail(12);
}
} // namespace nova::net
