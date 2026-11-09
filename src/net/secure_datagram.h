#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nova::net {
// Secret material delivered by the authenticated lobby, never native fields,
// disk configuration or log output. One ticket belongs to one match/player.
struct MatchTicket {
  std::array<uint8_t, 16> id{};
  std::array<uint8_t, 32> client_key{}, server_key{};
  ~MatchTicket();
};

// AES-256-GCM compatibility with server/internal/protocol/secure. Socket and
// gameplay ownership stay with their existing adapters. Each direction has
// its own key and counter; receive state changes only after authentication.
class SecureDatagram {
public:
  static constexpr size_t header_size = 28, tag_size = 16, max_payload = 2056;
  explicit SecureDatagram(const MatchTicket &, bool server = false);
  ~SecureDatagram();
  SecureDatagram(const SecureDatagram &) = delete;
  SecureDatagram &operator=(const SecureDatagram &) = delete;
  std::optional<std::vector<uint8_t>> seal(std::span<const uint8_t>);
  std::optional<std::vector<uint8_t>> open(std::span<const uint8_t>);

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace nova::net
