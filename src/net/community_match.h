#pragma once
#include "bionic/descriptors.h"
#include "net/match_control.h"
#include "net/server_config.h"
#include <unordered_map>

namespace nova::net {
// Bind the existing UDP descriptor only after an authenticated lobby grant.
// Unbound descriptors (including local Wi-Fi) retain their native behavior.
class CommunityMatch {
public:
  explicit CommunityMatch(const ServerConfig &);
  ~CommunityMatch();
  CommunityMatch(const CommunityMatch &) = delete;
  CommunityMatch &operator=(const CommunityMatch &) = delete;
  static bool control(const std::shared_ptr<bionic::Descriptor> &owner,
                      const MatchControl &);
  static bool publish(const pc::Snapshot &);
  // Read readiness refers to native datagrams, not internal snapshot traffic.
  static std::optional<int> readable(const std::shared_ptr<bionic::Descriptor> &);
  static std::optional<int> send(const std::shared_ptr<bionic::Descriptor> &,
                                 const void *, uint32_t, int, const sockaddr *,
                                 int);
  static std::optional<int> receive(const std::shared_ptr<bionic::Descriptor> &,
                                    void *, uint32_t, int, sockaddr_storage &,
                                    int &);

private:
  static std::optional<int> receive_impl(const std::shared_ptr<bionic::Descriptor> &,
      void *, uint32_t, int, sockaddr_storage &, int &, bool);
  struct Grant;
  std::shared_ptr<Grant> find(const std::shared_ptr<bionic::Descriptor> &,
                              const sockaddr * = nullptr, int = 0);
  static CommunityMatch *active_;
  uint32_t address_ = 0;
  std::mutex guard_;
  std::shared_ptr<Grant> pending_;
  struct Binding {
    std::weak_ptr<bionic::Descriptor> descriptor;
    std::shared_ptr<Grant> grant;
  };
  std::unordered_map<uint64_t, Binding> bindings_;
};
} // namespace nova::net
