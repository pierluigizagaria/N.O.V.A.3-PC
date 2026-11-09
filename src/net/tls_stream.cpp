#define SECURITY_WIN32
#include "net/tls_stream.h"
#include "net/server_certificate.h"
#include "net/server_config.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <schannel.h>
#include <security.h>
#include <stdexcept>
#include <vector>

namespace nova::net {
struct TlsStream::State {
  SOCKET socket;
  std::string address;
  HCERTCHAINENGINE engine;
  CredHandle credentials{};
  CtxtHandle context{};
  bool have_credentials = false, have_context = false;
  bool authenticated = false, closing = false, peer_closed = false,
       failed = false;
  Error error = Error::none;
  SecPkgContext_StreamSizes sizes{};
  std::vector<uint8_t> incoming, outgoing, plaintext, pending_write;
  size_t outgoing_offset = 0, plaintext_offset = 0;
  static constexpr size_t input_limit = 256 * 1024;

  State(SOCKET s, const std::string &name, void *trust)
      : socket(s), address(name), engine(trust) {
    if (s == INVALID_SOCKET || (!server_hostname(name) && !server_ipv4(name)))
      throw std::invalid_argument("TLS requires a selected server address");
    SCHANNEL_CRED settings{};
    settings.dwVersion = SCHANNEL_CRED_VERSION;
    settings.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    settings.dwFlags = SCH_CRED_NO_DEFAULT_CREDS |
                       SCH_CRED_MANUAL_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO;
    TimeStamp expiry{};
    const auto status = AcquireCredentialsHandleA(
        nullptr, const_cast<char *>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND,
        nullptr, &settings, nullptr, nullptr, &credentials, &expiry);
    if (status != SEC_E_OK)
      throw std::runtime_error("Windows TLS credentials unavailable");
    have_credentials = true;
  }
  ~State() {
    if (have_context)
      DeleteSecurityContext(&context);
    if (have_credentials)
      FreeCredentialsHandle(&credentials);
    if (!plaintext.empty())
      SecureZeroMemory(plaintext.data(), plaintext.size());
    if (!pending_write.empty())
      SecureZeroMemory(pending_write.data(), pending_write.size());
  }
  int fail() {
    failed = true;
    error = Error::failed;
    return -1;
  }
  int flush() {
    while (outgoing_offset < outgoing.size()) {
      const auto count = ::send(
          socket,
          reinterpret_cast<const char *>(outgoing.data() + outgoing_offset),
          static_cast<int>(outgoing.size() - outgoing_offset), 0);
      if (count == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
          error = Error::want_write;
          return -1;
        }
        return fail();
      }
      if (!count)
        return fail();
      outgoing_offset += static_cast<size_t>(count);
    }
    outgoing.clear();
    outgoing_offset = 0;
    return 1;
  }
  int receive(bool available_only = false) {
    if (incoming.size() >= input_limit)
      return fail();
    if (available_only) {
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(socket, &readable);
      timeval timeout{};
      const auto ready = ::select(0, &readable, nullptr, nullptr, &timeout);
      if (ready == SOCKET_ERROR)
        return fail();
      if (!ready) {
        error = Error::want_read;
        return -1;
      }
    }
    std::array<char, 16384> buffer{};
    const auto count =
        ::recv(socket, buffer.data(),
               static_cast<int>(
                   std::min(buffer.size(), input_limit - incoming.size())),
               0);
    if (count == SOCKET_ERROR) {
      if (WSAGetLastError() == WSAEWOULDBLOCK) {
        error = Error::want_read;
        return -1;
      }
      return fail();
    }
    // A bare EOF without close_notify is a truncated TLS stream, not an
    // authenticated end-of-response or successful lobby disconnect.
    if (!count)
      return fail();
    incoming.insert(incoming.end(), buffer.data(), buffer.data() + count);
    return count;
  }
  void retain_extra(const SecBuffer *buffers, size_t count) {
    size_t extra = 0;
    for (size_t i = 0; i < count; ++i)
      if (buffers[i].BufferType == SECBUFFER_EXTRA)
        extra = buffers[i].cbBuffer;
    if (extra > incoming.size()) {
      fail();
      return;
    }
    incoming.erase(incoming.begin(), incoming.end() - extra);
  }
  bool authenticate() {
    PCCERT_CONTEXT certificate = nullptr;
    if (QueryContextAttributesA(&context, SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                &certificate) != SEC_E_OK ||
        !certificate)
      return false;
    struct Release {
      PCCERT_CONTEXT p;
      ~Release() { CertFreeCertificateContext(p); }
    } release{certificate};
    // Preserve the existing explicit-IP policy. A resolved hostname never
    // acquires that exception: its original selected name is validated.
    if (!server_ipv4(address) &&
        !verify_server_certificate(certificate, certificate->hCertStore,
                                   address, engine))
      return false;
    return QueryContextAttributesA(&context, SECPKG_ATTR_STREAM_SIZES,
                                   &sizes) == SEC_E_OK &&
           sizes.cbMaximumMessage > 0;
  }
  int handshake() {
    if (failed || closing)
      return fail();
    error = Error::none;
    if (flush() < 0)
      return -1;
    if (authenticated)
      return 1;
    for (;;) {
      const bool initial = !have_context;
      if (!initial && incoming.empty() && receive() < 0)
        return -1;
      SecBuffer inputs[2]{{static_cast<unsigned long>(incoming.size()),
                           SECBUFFER_TOKEN, incoming.data()},
                          {0, SECBUFFER_EMPTY, nullptr}};
      SecBufferDesc input{SECBUFFER_VERSION, 2, inputs};
      SecBuffer token{0, SECBUFFER_TOKEN, nullptr};
      SecBufferDesc output{SECBUFFER_VERSION, 1, &token};
      unsigned long attributes = 0;
      TimeStamp expiry{};
      constexpr unsigned long requests =
          ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
          ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
          ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
      const auto status = InitializeSecurityContextA(
          &credentials, initial ? nullptr : &context, address.data(), requests,
          0, SECURITY_NATIVE_DREP, initial ? nullptr : &input, 0, &context,
          &output, &attributes, &expiry);
      if (token.pvBuffer) {
        if (token.cbBuffer) {
          auto p = static_cast<uint8_t *>(token.pvBuffer);
          outgoing.insert(outgoing.end(), p, p + token.cbBuffer);
        }
        FreeContextBuffer(token.pvBuffer);
      }
      if (status == SEC_E_INCOMPLETE_MESSAGE) {
        if (receive() < 0)
          return -1;
        continue;
      }
      if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED)
        return fail();
      have_context = true;
      if (!initial)
        retain_extra(inputs, 2);
      if (failed)
        return -1;
      if (status == SEC_E_OK) {
        if ((attributes & ISC_RET_CONFIDENTIALITY) == 0 || !authenticate())
          return fail();
        authenticated = true;
      }
      if (flush() < 0)
        return -1;
      if (authenticated)
        return 1;
    }
  }
  int decrypt_buffered() {
    // Drain complete records already read from Winsock. Leaving a complete
    // second record hidden behind SSL_pending()==0 can stall native readers
    // that wait on socket readiness before their next read.
    while (!incoming.empty() && !peer_closed) {
      SecBuffer buffers[4]{{static_cast<unsigned long>(incoming.size()),
                            SECBUFFER_DATA, incoming.data()},
                           {0, SECBUFFER_EMPTY, nullptr},
                           {0, SECBUFFER_EMPTY, nullptr},
                           {0, SECBUFFER_EMPTY, nullptr}};
      SecBufferDesc message{SECBUFFER_VERSION, 4, buffers};
      const auto status = DecryptMessage(&context, &message, 0, nullptr);
      if (status == SEC_E_INCOMPLETE_MESSAGE)
        return 0;
      if (status == SEC_I_CONTEXT_EXPIRED) {
        peer_closed = true;
        incoming.clear();
        return 0;
      }
      // TLS 1.2 renegotiation is not required by the community protocol.
      // Do not fall back to the guest TLS implementation on any failure.
      if (status != SEC_E_OK)
        return fail();
      for (const auto &b : buffers)
        if (b.BufferType == SECBUFFER_DATA && b.cbBuffer) {
          auto p = static_cast<const uint8_t *>(b.pvBuffer);
          plaintext.insert(plaintext.end(), p, p + b.cbBuffer);
        }
      retain_extra(buffers, 4);
      if (failed)
        return -1;
    }
    return 0;
  }
  int decrypt(void *output, int capacity, bool available_only = false) {
    if (failed)
      return fail();
    error = Error::none;
    if (capacity < 0 || (capacity && !output))
      return fail();
    if (!capacity)
      return 0;
    if (!authenticated && available_only) {
      error = Error::want_read;
      return -1;
    }
    if (!authenticated && handshake() < 0)
      return -1;
    while (plaintext_offset == plaintext.size()) {
      if (!plaintext.empty())
        SecureZeroMemory(plaintext.data(), plaintext.size());
      plaintext.clear();
      plaintext_offset = 0;
      if (peer_closed) {
        error = Error::closed;
        return 0;
      }
      if (incoming.empty() && receive(available_only) < 0)
        return -1;
      if (decrypt_buffered() < 0)
        return -1;
      if (plaintext.empty() && !peer_closed && receive(available_only) < 0)
        return -1;
    }
    const auto count = std::min(static_cast<size_t>(capacity),
                                plaintext.size() - plaintext_offset);
    std::memcpy(output, plaintext.data() + plaintext_offset, count);
    plaintext_offset += count;
    return static_cast<int>(count);
  }
  int encrypt(const void *input, int count) {
    if (failed || closing || peer_closed)
      return fail();
    error = Error::none;
    if (count < 0 || (count && !input))
      return fail();
    if (!authenticated && handshake() < 0)
      return -1;
    if (!pending_write.empty()) {
      // OpenSSL-compatible retry: the caller must retry the same bytes.
      if (pending_write.size() != static_cast<size_t>(count) ||
          std::memcmp(pending_write.data(), input, pending_write.size()) != 0)
        return fail();
      if (flush() < 0)
        return -1;
      SecureZeroMemory(pending_write.data(), pending_write.size());
      pending_write.clear();
      return count;
    }
    if (!count)
      return 0;
    // Bound one outstanding call, not the stream. Native HTTP/lobby writes
    // are small; arbitrary writes up to 1 MiB can span multiple TLS records.
    if (count > 1024 * 1024)
      return fail();
    const auto *source = static_cast<const uint8_t *>(input);
    for (int offset = 0; offset < count;) {
      const auto length = std::min(static_cast<unsigned long>(count - offset),
                                   sizes.cbMaximumMessage);
      std::vector<uint8_t> record(static_cast<size_t>(sizes.cbHeader) + length +
                                  sizes.cbTrailer);
      std::memcpy(record.data() + sizes.cbHeader, source + offset, length);
      SecBuffer buffers[4]{
          {sizes.cbHeader, SECBUFFER_STREAM_HEADER, record.data()},
          {length, SECBUFFER_DATA, record.data() + sizes.cbHeader},
          {sizes.cbTrailer, SECBUFFER_STREAM_TRAILER,
           record.data() + sizes.cbHeader + length},
          {0, SECBUFFER_EMPTY, nullptr}};
      SecBufferDesc message{SECBUFFER_VERSION, 4, buffers};
      if (EncryptMessage(&context, 0, &message, 0) != SEC_E_OK)
        return fail();
      for (size_t i = 0; i < 3; ++i) {
        auto p = static_cast<const uint8_t *>(buffers[i].pvBuffer);
        outgoing.insert(outgoing.end(), p, p + buffers[i].cbBuffer);
      }
      offset += static_cast<int>(length);
    }
    pending_write.assign(source, source + count);
    if (flush() < 0)
      return -1;
    SecureZeroMemory(pending_write.data(), pending_write.size());
    pending_write.clear();
    return count;
  }
  int close() {
    if (failed)
      return fail();
    error = Error::none;
    if (!authenticated)
      return fail();
    if (!closing) {
      if (!pending_write.empty())
        return fail();
      unsigned long value = SCHANNEL_SHUTDOWN;
      SecBuffer control{sizeof(value), SECBUFFER_TOKEN, &value};
      SecBufferDesc input{SECBUFFER_VERSION, 1, &control};
      if (ApplyControlToken(&context, &input) != SEC_E_OK)
        return fail();
      SecBuffer token{0, SECBUFFER_TOKEN, nullptr};
      SecBufferDesc output{SECBUFFER_VERSION, 1, &token};
      unsigned long attributes = 0;
      TimeStamp expiry{};
      const auto status = InitializeSecurityContextA(
          &credentials, &context, address.data(),
          ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM, 0, SECURITY_NATIVE_DREP,
          nullptr, 0, &context, &output, &attributes, &expiry);
      if (token.pvBuffer) {
        auto p = static_cast<const uint8_t *>(token.pvBuffer);
        outgoing.insert(outgoing.end(), p, p + token.cbBuffer);
        FreeContextBuffer(token.pvBuffer);
      }
      if (status != SEC_E_OK && status != SEC_I_CONTEXT_EXPIRED)
        return fail();
      closing = true;
    }
    return flush();
  }
};

TlsStream::TlsStream(SOCKET s, const std::string &name, void *engine)
    : state_(std::make_unique<State>(s, name, engine)) {}
TlsStream::~TlsStream() = default;
int TlsStream::connect() { return state_->handshake(); }
int TlsStream::read(void *p, int n) { return state_->decrypt(p, n); }
int TlsStream::read_available(void *p, int n) { return state_->decrypt(p, n, true); }
int TlsStream::write(const void *p, int n) { return state_->encrypt(p, n); }
int TlsStream::shutdown() { return state_->close(); }
size_t TlsStream::pending() const {
  if (state_->authenticated && !state_->failed)
    state_->decrypt_buffered();
  return state_->plaintext.size() - state_->plaintext_offset;
}
TlsStream::Error TlsStream::error() const { return state_->error; }
bool TlsStream::read_ready() const {
  return pending() != 0 || state_->peer_closed || state_->failed;
}
bool TlsStream::verified() const {
  return state_->authenticated && !state_->failed;
}
} // namespace nova::net
