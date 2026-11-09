#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace nova::net {
enum class ServerMode { offline, online };
struct ServerConfig {
  bool present = false;
  ServerMode mode = ServerMode::offline;
  std::string address = "127.1.1.1";
  // Runtime resolution is separate from parsing and never changes whether
  // the user explicitly selected an IP (the scoped TLS exception).
  std::string resolved_address;
  std::filesystem::path profile_root;
};
bool server_ipv4(const std::string &address);
bool server_hostname(const std::string &address);
ServerConfig read_server_config(const std::filesystem::path &file);
void resolve_server_address(ServerConfig &);
void apply_server_hosts(const ServerConfig &,
                        std::map<std::string, std::string> &hosts);
} // namespace nova::net
