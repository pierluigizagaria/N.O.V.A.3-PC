#pragma once
#include "net/server_config.h"
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>
namespace nova::net {
struct LocalShopConfig {
  uint16_t port = 0;
  unsigned price_rows = 0;
  std::string request_url, version, token;
  bool native_request = false;
  std::string address = "127.0.0.1";
  bool original_response = false;
  bool secure = false;
};
// Compatibility with the supported 1.0.7 resources; not server configuration.
inline constexpr unsigned native_price_rows = 54;
LocalShopConfig parse_local_shop_config(const std::string &text);
bool valid_shop_prices(const std::vector<uint8_t> &body, unsigned rows);
// Explicit native server IPv4 only. No proxy, redirect, DNS or fallback.
std::optional<std::vector<uint8_t>>
fetch_shop_prices(const LocalShopConfig &config);
// Called once before guest threads start. An explicit offline file disables the
// legacy environment bridge; an absent file preserves development invocations.
void configure_server_shop(const ServerConfig &, uint16_t service_port = 18443);
struct ConsumableInventory {
  bool accepted = false;
  uint32_t coins = 0;
  std::array<uint32_t,9> counts{};
};
// Uses the selected authenticated server. A purchase always carries a durable
// nonce; retrying that nonce reconciles an uncertain response without rebuying.
std::optional<ConsumableInventory> server_consumables(int kind = -1,
                                                     const std::string &nonce = {});
// Disabled when NOVA_TEST_SHOP_CONFIG is unset. Errors return Java null.
std::optional<std::vector<uint8_t>>
local_shop_file(const std::string &request_url) noexcept;
} // namespace nova::net
