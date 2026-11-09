#pragma once
#include "bionic/descriptors.h"
#include "net/login_credential.h"
#include "net/server_config.h"
#include <functional>
#include <optional>
#include <unordered_map>

namespace nova::net {
// Wrap the selected lobby and bootstrap HTTP streams in TLS. Original frame
// producers/readers and local Wi-Fi ownership stay in place.
class CommunityLobby {
public:
  using Credential = std::function<std::optional<LoginCredential>()>;
  CommunityLobby(const ServerConfig &, Credential, void *chain_engine = nullptr,
                 uint16_t port = 18443, uint16_t service_port = 18443);
  ~CommunityLobby();
  CommunityLobby(const CommunityLobby &) = delete;
  CommunityLobby &operator=(const CommunityLobby &) = delete;
  static void attach(const std::shared_ptr<bionic::Descriptor> &, sockaddr *,
                     int);
  static void use_native_tls(const std::shared_ptr<bionic::Descriptor> &);
  static std::optional<int> read(const std::shared_ptr<bionic::Descriptor> &,
                                 void *, uint32_t, int);
  static std::optional<int> write(const std::shared_ptr<bionic::Descriptor> &,
                                  const void *, uint32_t, int);
  static std::optional<int> readable(const std::shared_ptr<bionic::Descriptor> &);
  struct PollPlan {
    short events = 0, immediate = 0, retry = 0;
  };
  static std::optional<PollPlan>
  poll(const std::shared_ptr<bionic::Descriptor> &, short events);

private:
  struct Entry;
  std::shared_ptr<Entry> find(const std::shared_ptr<bionic::Descriptor> &);
  static std::optional<int>
  transfer(const std::shared_ptr<bionic::Descriptor> &, void *, uint32_t, int,
           bool);
  ServerConfig config_;
  Credential credential_;
  void *engine_;
  uint32_t address_ = 0;
  uint16_t port_, service_port_;
  std::mutex guard_;
  std::unordered_map<uint64_t, std::shared_ptr<Entry>> entries_;
  static CommunityLobby *active_;
};
} // namespace nova::net
