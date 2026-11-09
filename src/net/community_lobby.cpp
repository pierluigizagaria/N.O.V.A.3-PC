#include "net/community_lobby.h"
#include "net/community_match.h"
#include "net/tls_stream.h"
#include "net/social_invitations.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <ws2tcpip.h>

namespace nova::net {
struct CommunityLobby::Entry {
  std::mutex guard;
  std::weak_ptr<bionic::Descriptor> descriptor;
  std::unique_ptr<TlsStream> stream;
  std::array<uint8_t, 73> preface{};
  bool sent = false, failed = false;
  bool lobby = true;
  bool classified = true;
  std::array<uint8_t, 4098> frame{};
  size_t received = 0, expected = 2, consumed = 0;
  bool frame_ready = false;
  bool received_social = false;
  Entry(const std::shared_ptr<bionic::Descriptor> &d,
        const CommunityLobby &owner, bool native_lobby, bool shared)
      : descriptor(d), lobby(native_lobby), classified(!shared) {
    try {
      if (lobby && classified) {
        const auto login = owner.credential_();
        if (!login || login->token.size() != 64) {
          failed = true;
          return;
        }
        std::memcpy(preface.data(), "NV3L\x01", 5);
        std::memcpy(preface.data() + 5, login->token.data(), 64);
        preface.back() = 7; // Snapshot-v1, social invitations and stable invitation events.
      }
      stream = std::make_unique<TlsStream>(d->socket, owner.config_.address,
                                           owner.engine_);
    } catch (...) {
      failed = true;
    }
  }
  void classify(const CommunityLobby &owner, uint8_t first) {
    if (classified)
      return;
    // The native lobby starts with the BE length of its 12-byte establish
    // frame (00 0a). HTTP methods start with an ASCII letter. Delay credential
    // admission until the first application write, including fragmented writes.
    classified = true;
    lobby = first == 0;
    if (!lobby)
      return;
    const auto login = owner.credential_();
    if (!login || login->token.size() != 64) {
      failed = true;
      return;
    }
    std::memcpy(preface.data(), "NV3L\x01", 5);
    std::memcpy(preface.data() + 5, login->token.data(), 64);
    preface.back() = 7; // Match the invitation event support on the shared port.
  }
  ~Entry() {
    if (received_social) social::clear();
    SecureZeroMemory(preface.data(), preface.size());
    SecureZeroMemory(frame.data(), frame.size());
  }
  void clear_frame() {
    SecureZeroMemory(frame.data(), received);
    received = consumed = 0;
    expected = 2;
    frame_ready = false;
  }
  int fill(bool polling) {
    // Control records are consumed before native readers can see key material.
    // Keep one bounded frame, preserving native partial-read behavior.
    for (unsigned controls = 0; controls < 16;) {
      if (frame_ready)
        return 1;
      const int count = polling
          ? stream->read_available(frame.data() + received, static_cast<int>(expected - received))
          : stream->read(frame.data() + received, static_cast<int>(expected - received));
      if (count < 0)
        return error();
      if (!count) {
        if (!received)
          return 0;
        failed = true;
        return error();
      }
      received += static_cast<size_t>(count);
      if (received != expected)
        continue;
      if (expected == 2) {
        expected = size_t(pc::read16(frame.data())) + 2;
        if (expected < 8 || expected > frame.size()) {
          failed = true;
          return error();
        }
        continue;
      }
      if (frame[2] != 1 || frame[3] != 2) {
        failed = true;
        return error();
      }
      if (pc::read32(frame.data() + 4) == match_control_opcode) {
        const auto control =
            decode_match_control({frame.data() + 8, received - 8});
        if (!control || !CommunityMatch::control(descriptor.lock(), *control)) {
          failed = true;
          clear_frame();
          return error();
        }
        clear_frame();
        ++controls;
      } else if (pc::read32(frame.data() + 4) == social::control_opcode) {
        if (!social::receive({frame.data() + 8, received - 8})) {
          failed = true;
          clear_frame();
          return error();
        }
	    received_social = true;
        clear_frame();
        ++controls;
      } else
        frame_ready = true;
    }
    return bionic::fail(11);
  }
  int read(void *data, uint32_t size) {
    if (!size)
      return 0;
    if (!lobby) {
      const int count = stream->read(data, static_cast<int>(size));
      return count < 0 ? error() : count;
    }
    const auto result = fill(false);
    if (result <= 0)
      return result;
    const auto count = std::min<size_t>(size, received - consumed);
    std::memcpy(data, frame.data() + consumed, count);
    consumed += count;
    if (consumed == received)
      clear_frame();
    return static_cast<int>(count);
  }
  int ready() {
    if (failed || !stream)
      return -1;
    if (!classified)
      return -1;
    if (sent)
      return 1;
    if (stream->connect() < 0)
      return -1;
    if (!lobby) {
      sent = true;
      return 1;
    }
    if (!sent) {
      if (stream->write(preface.data(), static_cast<int>(preface.size())) < 0)
        return -1;
      sent = true;
      SecureZeroMemory(preface.data(), preface.size());
    }
    return 1;
  }
  int error() const {
    if (!failed && stream &&
        (stream->error() == TlsStream::Error::want_read ||
         stream->error() == TlsStream::Error::want_write))
      return bionic::fail(
          11);                // EAGAIN; readiness drives the same native retry.
    return bionic::fail(104); // ECONNRESET, never plaintext fallback.
  }
};
CommunityLobby *CommunityLobby::active_ = nullptr;
CommunityLobby::CommunityLobby(const ServerConfig &config,
                               Credential credential, void *engine,
                               uint16_t port, uint16_t service_port)
    : config_(config), credential_(std::move(credential)), engine_(engine),
      port_(port), service_port_(service_port) {
  if (active_ || !credential_ || !port_ || !service_port_ ||
      InetPtonA(AF_INET, config.resolved_address.c_str(), &address_) != 1)
    throw std::runtime_error("Invalid community lobby transport");
  active_ = this;
}
CommunityLobby::~CommunityLobby() {
  social::clear();
  if (active_ == this)
    active_ = nullptr;
  entries_.clear();
}
void CommunityLobby::attach(const std::shared_ptr<bionic::Descriptor> &d,
                            sockaddr *address, int length) {
  if (!active_ || d->type != SOCK_STREAM || length != sizeof(sockaddr_in) ||
      address->sa_family != AF_INET)
    return;
  auto *peer = reinterpret_cast<sockaddr_in *>(address);
  const auto port = ntohs(peer->sin_port);
  const bool lobby = port == active_->port_;
  const bool shared = lobby && port == active_->service_port_;
  // Some original HTTP callers reuse the advertised HTTPS host:port without
  // selecting CSSLSocket. Protect those streams too. Original SSL callers
  // relinquish this wrapper in use_native_tls before their own handshake.
  if (peer->sin_addr.s_addr != active_->address_ ||
      (!lobby && port != 80 && port != 20001 && port != 443 &&
       port != active_->service_port_))
    return;
  std::lock_guard lock(active_->guard_);
  for (auto it = active_->entries_.begin(); it != active_->entries_.end();) {
    if (it->second->descriptor.expired())
      it = active_->entries_.erase(it);
    else
      ++it;
  }
  if (!active_->entries_.contains(d->serial))
    active_->entries_.emplace(d->serial,
                              std::make_shared<Entry>(d, *active_, lobby, shared));
  if (!lobby)
    peer->sin_port = htons(active_->service_port_);
}
std::shared_ptr<CommunityLobby::Entry>
CommunityLobby::find(const std::shared_ptr<bionic::Descriptor> &d) {
  std::lock_guard lock(guard_);
  const auto it = entries_.find(d->serial);
  if (it == entries_.end() || it->second->descriptor.lock() != d)
    return {};
  return it->second;
}
void CommunityLobby::use_native_tls(
    const std::shared_ptr<bionic::Descriptor> &d) {
  if (!active_)
    return;
  std::lock_guard lock(active_->guard_);
  const auto found = active_->entries_.find(d->serial);
  // A service URL can already use an original SSL socket on any service port.
  // Keep destination redirection but give SSL sole TLS/poll
  // ownership; never put two independent record layers on one connection.
  if (found != active_->entries_.end() &&
      (!found->second->classified || !found->second->lobby))
    active_->entries_.erase(found);
}
std::optional<int>
CommunityLobby::transfer(const std::shared_ptr<bionic::Descriptor> &d,
                         void *data, uint32_t size, int flags, bool writing) {
  if (!active_)
    return {};
  const auto entry = active_->find(d);
  if (!entry)
    return {};
  std::lock_guard lock(entry->guard);
  std::lock_guard socket_lock(d->guard);
  if (flags & ~0x4000)
    return bionic::fail(95); // Only native ordinary/NOSIGNAL IO.
  if (size > INT32_MAX || (!data && size))
    return bionic::fail(!data ? 14 : 22);
  try {
    if (!entry->classified) {
      if (!writing || !size)
        return bionic::fail(11);
      entry->classify(*active_, *static_cast<const uint8_t *>(data));
    }
    if (entry->ready() < 0)
      return entry->error();
    if (!writing)
      return entry->read(data, size);
    const auto count = entry->stream->write(data, static_cast<int>(size));
    return count < 0 ? entry->error() : count;
  } catch (...) {
    entry->failed = true;
    return entry->error();
  }
}
std::optional<int>
CommunityLobby::read(const std::shared_ptr<bionic::Descriptor> &d, void *p,
                     uint32_t n, int f) {
  return transfer(d, p, n, f, false);
}
std::optional<int>
CommunityLobby::write(const std::shared_ptr<bionic::Descriptor> &d,
                      const void *p, uint32_t n, int f) {
  return transfer(d, const_cast<void *>(p), n, f, true);
}
std::optional<int>
CommunityLobby::readable(const std::shared_ptr<bionic::Descriptor> &d) {
  if (!active_)
    return {};
  const auto entry = active_->find(d);
  if (!entry)
    return {};
  std::lock_guard lock(entry->guard);
  if (!entry->classified || !entry->lobby || !entry->sent)
    return {};
  std::lock_guard socket_lock(d->guard);
  const auto saved = bionic::guest_errno;
  int ready = -1;
  try {
    const auto result = entry->fill(true);
    // EOF is readable; a partial frame or a control-only gap is not.
    ready = result >= 0 ? 1 : bionic::guest_errno == 11 ? 0 : -1;
  } catch (...) {
    entry->failed = true;
  }
  bionic::guest_errno = saved;
  return ready;
}
std::optional<CommunityLobby::PollPlan>
CommunityLobby::poll(const std::shared_ptr<bionic::Descriptor> &d,
                     short events) {
  if (!active_)
    return {};
  const auto entry = active_->find(d);
  if (!entry)
    return {};
  std::lock_guard lock(entry->guard);
  PollPlan plan{events, 0, 0};
  if (entry->failed || !entry->stream ||
      entry->stream->error() == TlsStream::Error::failed) {
    plan.immediate = POLLERR;
    return plan;
  }
  if (entry->sent) {
    if ((events & POLLRDNORM) &&
        (entry->frame_ready || entry->stream->read_ready()))
      plan.immediate = POLLRDNORM;
  } else if (events & (POLLRDNORM | POLLWRNORM)) {
    plan.retry = entry->stream->error() == TlsStream::Error::want_read
                     ? POLLRDNORM
                     : POLLWRNORM;
    // HTTP and lobby are client-first protocols. Only an application write
    // advances admission; handshake input is never readable application data.
    // In particular Eve checks reads before sends and treats a premature read
    // error as a failed request. A read-only poll must not wake on writable TCP.
    plan.events = (events & POLLWRNORM) ? plan.retry : 0;
  }
  return plan;
}
} // namespace nova::net
