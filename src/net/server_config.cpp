#include "net/server_config.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace nova::net {
namespace {
std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos
             ? ""
             : value.substr(first,
                            value.find_last_not_of(" \t\r\n") - first + 1);
}
unsigned number(const std::string &value, unsigned maximum) {
  unsigned out = 0;
  auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
  if (value.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size() || out > maximum)
    throw std::runtime_error("Invalid server.conf number: " + value);
  return out;
}
std::array<unsigned, 4> octets(const std::string &address) {
  std::array<unsigned, 4> out{};
  size_t start = 0;
  for (size_t i = 0; i < out.size(); ++i) {
    const auto end = address.find('.', start);
    if ((i == 3) != (end == std::string::npos))
      throw std::runtime_error("Invalid IPv4");
    auto part =
        address.substr(start, end == std::string::npos ? end : end - start);
    if (part.size() > 1 && part[0] == '0')
      throw std::runtime_error("Invalid IPv4");
    out[i] = number(part, 255);
    start = end + 1;
  }
  return out;
}
} // namespace
bool server_ipv4(const std::string &address) {
  try {
    const auto parts = octets(address);
    return parts[0] > 0 && parts[0] < 224 &&
           parts != std::array<unsigned, 4>{255, 255, 255, 255};
  } catch (...) {
    return false;
  }
}
bool server_hostname(const std::string &value) {
  if (value.empty() || value.size() > 253 ||
      value.find_first_not_of("0123456789.") == std::string::npos)
    return false;
  if (value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") !=
      std::string::npos)
    return false;
  size_t start = 0;
  while (start < value.size()) {
    const auto end = value.find('.', start);
    const auto count = (end == std::string::npos ? value.size() : end) - start;
    if (!count || count > 63 || value[start] == '-' ||
        value[start + count - 1] == '-')
      return false;
    if (end == std::string::npos)
      return true;
    start = end + 1;
  }
  return false;
}
ServerConfig read_server_config(const std::filesystem::path &file) {
  ServerConfig out;
  if (!std::filesystem::exists(file))
    return out;
  if (std::filesystem::file_size(file) > 16384)
    throw std::runtime_error("server.conf exceeds 16 KiB");
  std::ifstream input(file, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot read server.conf");
  out.present = true;
  const auto base = std::filesystem::absolute(file).parent_path();
  auto path = [&](const std::string &s) {
    auto p = std::filesystem::path(std::u8string(s.begin(), s.end()));
    return p.empty() ? p : (p.is_absolute() ? p : base / p).lexically_normal();
  };
  std::set<std::string> seen;
  std::string line;
  while (std::getline(input, line)) {
    if (seen.empty() && line.starts_with("\xef\xbb\xbf"))
      line.erase(0, 3);
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    if (line.find('\0') != std::string::npos)
      throw std::runtime_error("Invalid server.conf characters");
    const auto eq = line.find('=');
    if (eq == std::string::npos)
      throw std::runtime_error("Expected key = value in server.conf");
    const auto key = trim(line.substr(0, eq)),
               value = trim(line.substr(eq + 1));
    if (!seen.insert(key).second)
      throw std::runtime_error("Duplicate server.conf key: " + key);
    if (key == "mode") {
      if (value == "offline")
        out.mode = ServerMode::offline;
      else if (value == "online")
        out.mode = ServerMode::online;
      else
        throw std::runtime_error("server.conf mode must be offline or online");
    } else if (key == "address")
      out.address = value;
    else if (key == "profile_root")
      out.profile_root = path(value);
    else
      throw std::runtime_error("Unknown server.conf key: " + key);
  }
  if (!seen.contains("mode"))
    throw std::runtime_error("server.conf requires mode");
  if (!out.profile_root.empty() &&
      !std::filesystem::is_directory(out.profile_root))
    throw std::runtime_error("The configured profile_root is not prepared");
  // Offline keeps dormant connection settings so switching mode is one edit.
  if (out.mode == ServerMode::offline)
    return out;
  std::transform(out.address.begin(), out.address.end(), out.address.begin(),
                 [](unsigned char c) { return static_cast<char>(tolower(c)); });
  if (!server_ipv4(out.address) && !server_hostname(out.address))
    throw std::runtime_error("address must be an IPv4 address or DNS hostname, "
                             "without a URL or port");
  if (out.address == "gameloft.com" || out.address.ends_with(".gameloft.com"))
    throw std::runtime_error(
        "Original service hosts remain blocked; select your dedicated server");
  if (server_ipv4(out.address)) {
    out.resolved_address = out.address;
  }
  return out;
}
void resolve_server_address(ServerConfig &server) {
  if (!server.present || server.mode == ServerMode::offline ||
      !server.resolved_address.empty())
    return;
  WSADATA startup{};
  if (WSAStartup(MAKEWORD(2, 2), &startup))
    throw std::runtime_error("Cannot initialize dedicated server resolution");
  struct Cleanup {
    ~Cleanup() { WSACleanup(); }
  } cleanup;
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *result = nullptr;
  if (getaddrinfo(server.address.c_str(), nullptr, &hints, &result))
    throw std::runtime_error(
        "Cannot resolve the configured dedicated server hostname");
  struct Free {
    addrinfo *p;
    ~Free() { freeaddrinfo(p); }
  } free{result};
  for (auto *entry = result; entry; entry = entry->ai_next) {
    if (entry->ai_family != AF_INET || entry->ai_addrlen < sizeof(sockaddr_in))
      continue;
    char buffer[INET_ADDRSTRLEN]{};
    const auto *address = reinterpret_cast<sockaddr_in *>(entry->ai_addr);
    if (!InetNtopA(AF_INET, &address->sin_addr, buffer, sizeof(buffer)) ||
        !server_ipv4(buffer))
      continue;
    server.resolved_address = buffer;
    return;
  }
  throw std::runtime_error("The server has no usable IPv4 address");
}
void apply_server_hosts(const ServerConfig &server,
                        std::map<std::string, std::string> &hosts) {
  if (!server.present)
    return;
  // Explicit offline overrides legacy mappings; no DNS fallback is introduced.
  for (const auto *name : {"gllive.gameloft.com", "vgold.gameloft.com",
                           "confirmation.gameloft.com"})
    hosts[name] =
        server.mode == ServerMode::offline ? "block" : server.resolved_address;
  if (server.mode == ServerMode::online) {
    if (!server_ipv4(server.resolved_address))
      throw std::runtime_error(
          "Resolve the selected server before applying routes");
    hosts[server.address] = server.resolved_address;
  }
}
} // namespace nova::net
