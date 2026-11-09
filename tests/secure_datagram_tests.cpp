#include "net/match_control.h"
#include "net/secure_datagram.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {
void check(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
std::vector<uint8_t> hex(std::string_view text) {
  std::vector<uint8_t> out;
  auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (size_t i = 0; i < text.size(); i += 2)
    out.push_back(
        static_cast<uint8_t>((nibble(text[i]) << 4) | nibble(text[i + 1])));
  return out;
}
} // namespace
int main() {
  try {
    using nova::net::MatchTicket;
    using nova::net::SecureDatagram;
    MatchTicket ticket;
    for (size_t i = 0; i < ticket.id.size(); ++i)
      ticket.id[i] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < ticket.client_key.size(); ++i) {
      ticket.client_key[i] = static_cast<uint8_t>(i);
      ticket.server_key[i] = static_cast<uint8_t>(i + 32);
    }
    SecureDatagram client(ticket), server(ticket, true);
    const auto grant_wire =
        hex("0101c0a801023b6100000001000102030405060708090a0b0c0d0e0f0001020304"
            "05060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425"
            "262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f");
    const auto grant = nova::net::decode_match_control(grant_wire);
    check(grant && grant->grant &&
              grant->address == std::array<uint8_t, 4>{192, 168, 1, 2} &&
              grant->port == 15201 && grant->ticket.id == ticket.id &&
              grant->ticket.client_key == ticket.client_key &&
              grant->ticket.server_key == ticket.server_key,
          "Go lobby grant golden");
    const auto revoke = nova::net::decode_match_control(
        hex("0102000102030405060708090a0b0c0d0e0f"));
    check(revoke && !revoke->grant && revoke->ticket.id == ticket.id,
          "Go lobby revoke golden");
    for (size_t length = 0; length < grant_wire.size(); ++length)
      check(
          !nova::net::decode_match_control(std::span(grant_wire).first(length)),
          "truncated grant");
    for (const size_t index : {size_t{0}, size_t{1}, size_t{8}}) {
      auto invalid = grant_wire;
      invalid[index] ^= 0x80;
      check(!nova::net::decode_match_control(invalid),
            "unknown mandatory control fields");
    }
    const auto plain = hex("4e3350430101000401020304");
    const auto client_vector =
        hex("4e563301000102030405060708090a0b0c0d0e0f00000000000000015be5efbf45"
            "f5301a0f2c523df075dafc2f5cd27409096a114ee3a75b");
    const auto server_vector =
        hex("4e563301000102030405060708090a0b0c0d0e0f000000000000000145d838fc78"
            "db4fa241c496252913762e89f9fe019d6d085b71fe82fc");
    check(client.seal(plain) == client_vector,
          "client ciphertext matches independent AES-GCM and Go vector");
    check(server.seal(plain) == server_vector,
          "server ciphertext matches independent AES-GCM and Go vector");
    // A forged high counter, identifier, body or tag cannot consume sequence 1.
    for (const size_t offset : {size_t{0}, size_t{4}, size_t{20}, size_t{28},
                                client_vector.size() - 1}) {
      auto forged = client_vector;
      forged[offset] ^= 0x80;
      check(!server.open(forged), "forged packet rejected");
    }
    check(!client.open(client_vector), "reflected direction rejected");
    check(server.open(client_vector) == plain &&
              client.open(server_vector) == plain,
          "both directions decode");
    check(!server.open(client_vector) && !client.open(server_vector),
          "replay rejected");
    std::vector<std::vector<uint8_t>> packets;
    for (unsigned i = 0; i < 130; ++i) {
      const std::vector<uint8_t> data{static_cast<uint8_t>(i), 1, 2};
      auto encrypted = client.seal(data);
      check(encrypted.has_value(), "bounded encryption");
      packets.push_back(std::move(*encrypted));
    }
    for (const size_t index : {size_t{2}, size_t{0}, size_t{1}, size_t{129},
                               size_t{128}, size_t{66}}) {
      check(server.open(packets[index]) ==
                std::vector<uint8_t>{static_cast<uint8_t>(index), 1, 2},
            "reordered window and large jump");
      check(!server.open(packets[index]), "reordered replay");
    }
    check(!server.open(packets[65]), "outside 64-packet window");
    const std::vector<uint8_t> maximum(SecureDatagram::max_payload, 0xa5);
    const auto maximum_packet = client.seal(maximum);
    check(maximum_packet && server.open(*maximum_packet) == maximum,
          "maximum payload");
    check(!client.seal({}) && !client.seal(std::vector<uint8_t>(
                                  SecureDatagram::max_payload + 1)),
          "payload bounds");
    for (size_t size = 0;
         size < SecureDatagram::header_size + SecureDatagram::tag_size + 1;
         ++size)
      check(!server.open(std::span(client_vector).first(size)),
            "truncated packet");
    auto oversized = *maximum_packet;
    oversized.push_back(0);
    check(!server.open(oversized), "oversized packet");
    MatchTicket other = ticket;
    other.id[0] ^= 1;
    SecureDatagram other_server(other, true);
    check(!other_server.open(client_vector), "cross-ticket rejection");
    std::puts("PASS Windows/Go authenticated datagram vectors, directions, "
              "bounds and replay window");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "FAIL %s\n", e.what());
    return 1;
  }
}
