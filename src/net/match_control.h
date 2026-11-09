#pragma once
#include "net/pc_snapshot.h"
#include "net/secure_datagram.h"

namespace nova::net {
inline constexpr uint32_t match_control_opcode = 0x4e335043;
struct MatchControl {
  bool grant = false;
  std::array<uint8_t, 4> address{};
  uint16_t port = 0;
  MatchTicket ticket;
};
inline std::optional<MatchControl>
decode_match_control(std::span<const uint8_t> body) {
  if (body.size() < 2 || body[0] != 1)
    return {};
  MatchControl value;
  if (body[1] == 2 && body.size() == 18) {
    std::copy_n(body.begin() + 2, 16, value.ticket.id.begin());
    return value;
  }
  if (body[1] != 1 || body.size() != 92 ||
      pc::read32(body.data() + 8) != pc::snapshot_v1)
    return {};
  value.grant = true;
  std::copy_n(body.begin() + 2, 4, value.address.begin());
  value.port = pc::read16(body.data() + 6);
  const auto address = pc::read32(value.address.data());
  if (!value.port || address == 0 || address == 0xffffffff ||
      (value.address[0] & 0xf0) == 0xe0)
    return {};
  std::copy_n(body.begin() + 12, 16, value.ticket.id.begin());
  std::copy_n(body.begin() + 28, 32, value.ticket.client_key.begin());
  std::copy_n(body.begin() + 60, 32, value.ticket.server_key.begin());
  return value;
}
} // namespace nova::net
