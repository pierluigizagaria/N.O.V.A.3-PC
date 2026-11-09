#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <winsock2.h>

namespace nova::net {
// Modern TLS over an already connected socket. Socket lifetime, blocking mode
// and readiness polling remain with the existing descriptor owner. No worker,
// localhost proxy, new certificate store or user configuration is introduced.
class TlsStream {
public:
  enum class Error { none, want_read, want_write, closed, failed };
  TlsStream(SOCKET, const std::string &selected_address,
            void *chain_engine = nullptr);
  ~TlsStream();
  TlsStream(const TlsStream &) = delete;
  TlsStream &operator=(const TlsStream &) = delete;
  int connect();
  int read(void *output, int capacity);
  // Filter framed transport controls without waiting on a blocking descriptor.
  int read_available(void *output, int capacity);
  int write(const void *input, int size);
  int shutdown();
  std::size_t pending() const;
  bool read_ready() const;
  Error error() const;
  bool verified() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace nova::net
