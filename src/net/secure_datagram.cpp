#include "net/secure_datagram.h"
#include <Windows.h>
#include <algorithm>
#include <bcrypt.h>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace nova::net {
namespace {
uint64_t read64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value = (value << 8) | p[i];
  return value;
}
void write64(uint8_t *p, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = static_cast<uint8_t>(value >> ((7 - i) * 8));
}
struct Algorithm {
  BCRYPT_ALG_HANDLE handle = nullptr;
  Algorithm() {
    if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_AES_ALGORITHM, nullptr, 0) <
        0)
      throw std::runtime_error("Cannot initialize match encryption");
    if (BCryptSetProperty(handle, BCRYPT_CHAINING_MODE,
                          reinterpret_cast<PUCHAR>(
                              const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0) {
      BCryptCloseAlgorithmProvider(handle, 0);
      throw std::runtime_error(
          "Cannot initialize authenticated match encryption");
    }
  }
  ~Algorithm() { BCryptCloseAlgorithmProvider(handle, 0); }
};
struct Key {
  BCRYPT_KEY_HANDLE handle = nullptr;
  Key(BCRYPT_ALG_HANDLE algorithm, const std::array<uint8_t, 32> &secret) {
    if (BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0,
                                   const_cast<PUCHAR>(secret.data()),
                                   static_cast<ULONG>(secret.size()), 0) < 0)
      throw std::runtime_error("Cannot initialize match key");
  }
  ~Key() { BCryptDestroyKey(handle); }
};
} // namespace

MatchTicket::~MatchTicket() {
  SecureZeroMemory(client_key.data(), client_key.size());
  SecureZeroMemory(server_key.data(), server_key.size());
}
struct SecureDatagram::State {
  std::mutex guard;
  std::array<uint8_t, 16> id;
  Algorithm algorithm;
  Key send, receive;
  uint64_t next = 1, highest = 0, seen = 0;
  State(const MatchTicket &ticket, bool server)
      : id(ticket.id),
        send(algorithm.handle, server ? ticket.server_key : ticket.client_key),
        receive(algorithm.handle,
                server ? ticket.client_key : ticket.server_key) {}
};
SecureDatagram::SecureDatagram(const MatchTicket &ticket, bool server)
    : state_(std::make_unique<State>(ticket, server)) {}
SecureDatagram::~SecureDatagram() = default;

std::optional<std::vector<uint8_t>>
SecureDatagram::seal(std::span<const uint8_t> plain) {
  auto &s = *state_;
  std::lock_guard lock(s.guard);
  if (plain.empty() || plain.size() > max_payload ||
      s.next == std::numeric_limits<uint64_t>::max())
    return {};
  std::vector<uint8_t> packet(header_size + plain.size() + tag_size);
  std::memcpy(packet.data(), "NV3\x01", 4);
  std::copy(s.id.begin(), s.id.end(), packet.begin() + 4);
  std::array<uint8_t, 12> nonce{};
  write64(packet.data() + 20, s.next);
  write64(nonce.data() + 4, s.next++);
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = nonce.data();
  info.cbNonce = static_cast<ULONG>(nonce.size());
  info.pbAuthData = packet.data();
  info.cbAuthData = header_size;
  info.pbTag = packet.data() + header_size + plain.size();
  info.cbTag = tag_size;
  ULONG written = 0;
  const auto length = static_cast<ULONG>(plain.size());
  if (BCryptEncrypt(s.send.handle, const_cast<PUCHAR>(plain.data()), length,
                    &info, nullptr, 0, packet.data() + header_size, length,
                    &written, 0) < 0 ||
      written != length)
    return {};
  return packet;
}

std::optional<std::vector<uint8_t>>
SecureDatagram::open(std::span<const uint8_t> packet) {
  auto &s = *state_;
  std::lock_guard lock(s.guard);
  if (packet.size() < header_size + tag_size + 1 ||
      packet.size() > header_size + tag_size + max_payload ||
      std::memcmp(packet.data(), "NV3\x01", 4) != 0 ||
      !std::equal(s.id.begin(), s.id.end(), packet.begin() + 4))
    return {};
  const auto sequence = read64(packet.data() + 20);
  if (!sequence || (sequence <= s.highest &&
                    (s.highest - sequence >= 64 ||
                     (s.seen & (uint64_t{1} << (s.highest - sequence))))))
    return {};
  std::vector<uint8_t> plain(packet.size() - header_size - tag_size);
  std::array<uint8_t, 12> nonce{};
  write64(nonce.data() + 4, sequence);
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = nonce.data();
  info.cbNonce = static_cast<ULONG>(nonce.size());
  info.pbAuthData = const_cast<PUCHAR>(packet.data());
  info.cbAuthData = header_size;
  info.pbTag = const_cast<PUCHAR>(packet.data() + packet.size() - tag_size);
  info.cbTag = tag_size;
  ULONG written = 0;
  const auto length = static_cast<ULONG>(plain.size());
  if (BCryptDecrypt(s.receive.handle,
                    const_cast<PUCHAR>(packet.data() + header_size), length,
                    &info, nullptr, 0, plain.data(), length, &written, 0) < 0 ||
      written != length) {
    SecureZeroMemory(plain.data(), plain.size());
    return {};
  }
  if (sequence > s.highest) {
    const auto distance = sequence - s.highest;
    s.seen = distance >= 64 ? 0 : s.seen << distance;
    s.highest = sequence;
  }
  s.seen |= uint64_t{1} << (s.highest - sequence);
  return plain;
}
} // namespace nova::net
