#include "abi/guest_hook.h"
#include "bionic/descriptors.h"
#include "bionic/runtime.h"
#include "loader/diagnostics.h"
#include "loader/sha256.h"
#include "net/native_server_tls.h"
#include "net/native_snapshot_presentation.h"
#include "net/community_match.h"
#include "net/native_trust.h"
#include "net/network.h"
#include "platform/profile.h"
#include <Windows.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <wincrypt.h>

namespace {
void check(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
uint32_t pointer(const void *value) {
  return reinterpret_cast<uint32_t>(value);
}
void credential_headers() {
  using Parser = nova::net::LoginCredentialHeader;
  const std::string fields = "X-Nova-Account: " + std::string(64, 'a') +
                             "\r\nx-nova-session: " + std::string(64, 'b') +
                             "\r\n";
  const std::string header = "HTTP/1.1 200 OK\r\n" + fields + "\r\n";
  for (size_t split = 0; split <= header.size(); ++split) {
    Parser parser;
    const auto first = parser.feed(header.data(), split);
    const auto second =
        parser.feed(header.data() + split, header.size() - split);
    check((first == Parser::Result::available) !=
              (second == Parser::Result::available),
          "one credential publication across every header split");
    check(parser.credential().account == std::string(64, 'a') &&
              parser.credential().token == std::string(64, 'b'),
          "credential header field identities");
    check(parser.feed(header.data(), header.size()) == Parser::Result::absent,
          "body cannot replace credentials");
  }
  for (const auto &value : std::vector<std::string>{
           "HTTP/1.1 200 OK\r\n" + fields + fields + "\r\n",
           "HTTP/1.1 200 OK\r\nX-Nova-Account: " + std::string(64, 'a') +
               "\r\n\r\n",
           "HTTP/1.1 200 OK\r\n" + fields + "X-Nova-Session: bad\r\n\r\n",
           "HTTP/1.1 200 OK\r\nX-Filler: " + std::string(8192, 'x') +
               "\r\n\r\n"}) {
    Parser parser;
    check(parser.feed(value.data(), value.size()) == Parser::Result::invalid,
          "invalid credential header rejected");
  }
  for (const auto &value : std::vector<std::string>{
           "HTTP/1.1 401 Unauthorized\r\n" + fields + "\r\n",
           "HTTP/1.1 200 OK\r\n\r\n" + fields, std::string(9000, 'x')}) {
    Parser parser;
    check(parser.feed(value.data(), value.size()) == Parser::Result::absent,
          "non-login data ignored");
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    credential_headers();
    const std::array<uint8_t, 3> abc{'a', 'b', 'c'};
    check(
        nova::sha256(abc) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA256 known vector");
    check(
        nova::sha256({}) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "SHA256 empty vector");
    if (argc == 1) {
      std::puts("PASS SHA256 identity controls");
      return 0;
    }
    check(argc >= 5 && argc <= 9, "usage: native-tls-tests ELF CA/ROOT/- PORT "
                                  "trusted|untrusted [selected-address "
                                  "[complete|truncated [lobby-port]]]");
    const bool truncated = argc >= 7 && std::string(argv[6]) == "truncated";
    check(argc < 7 || truncated || std::string(argv[6]) == "complete",
          "unknown TLS test mode");
    const auto lobby_port = argc >= 8 ? std::stoul(argv[7]) : 0;
    const auto game_port = argc == 9 ? std::stoul(argv[8]) : 0;
    check(game_port <= 65535 && (argc != 9 || game_port != 0),
          "invalid game port");
    check(lobby_port <= 65535 && (argc < 8 || lobby_port != 0),
          "invalid lobby test port");
    if (!std::getenv("NOVA_REAL_TESTS") ||
        std::string(std::getenv("NOVA_REAL_TESTS")) != "1" ||
        !std::filesystem::is_regular_file(argv[1])) {
      std::puts(
          "SKIP original TLS: opt in with NOVA_REAL_TESTS=1 and local inputs");
      return 77;
    }
    const bool trusted = std::string(argv[4]) == "trusted";
    check(trusted || std::string(argv[4]) == "untrusted",
          "expected trust case");
    const auto port = std::stoul(argv[3]);
    check(port > 0 && port <= 65535, "invalid loopback port");
    nova::bionic::configure_paths(
        {"native-tls-probe", "local/profiles/sv06-tls", {}});
    nova::profile::Config profile;
    profile.net_log =
        false; // Assertions cover transport; no routine socket dumps.
    nova::profile::configure(profile);
    nova::ImportRegistry imports;
    nova::bionic::register_boot(imports);
    nova::bionic::CxaRegistry cxa(imports);
    nova::ElfImage image(argv[1], imports);
    nova::Diagnostics diagnostics(image);
    image.run_initializers();
    std::unique_ptr<nova::net::NativeTrust> trust;
    std::unique_ptr<nova::net::NativeServerTls> server_tls;
    if (argc == 5)
      trust = std::make_unique<nova::net::NativeTrust>(image, argv[2]);
    else {
      HCERTCHAINENGINE engine = nullptr;
      if (std::string(argv[2]) != "-") {
        // Exclusive in-memory root store. Never modify OS trust stores.
        auto store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
        check(store != nullptr, "isolated root store");
        std::ifstream input(argv[2], std::ios::binary);
        const std::vector<uint8_t> der((std::istreambuf_iterator<char>(input)),
                                       {});
        check(CertAddEncodedCertificateToStore(
                  store, X509_ASN_ENCODING, der.data(),
                  static_cast<DWORD>(der.size()), CERT_STORE_ADD_ALWAYS,
                  nullptr) != FALSE,
              "test root DER");
        CERT_CHAIN_ENGINE_CONFIG engine_config{};
        engine_config.cbSize = sizeof(engine_config);
        engine_config.hExclusiveRoot = store;
        check(CertCreateCertificateChainEngine(&engine_config, &engine) !=
                  FALSE,
              "isolated trust engine");
        CertCloseStore(store, 0);
      }
      nova::net::ServerConfig config;
      config.present = true;
      config.mode = nova::net::ServerMode::online;
      config.address = argv[5];
      config.resolved_address =
          nova::net::server_ipv4(config.address) ? config.address : "127.0.0.1";
      server_tls = std::make_unique<nova::net::NativeServerTls>(
          image, config, engine,
          static_cast<uint16_t>(lobby_port ? lobby_port : 15200),
          static_cast<uint16_t>(port));
      // Probe process owns the engine until TerminateProcess below.
    }
    auto function = [&](const char *name) {
      return nova::abi::require_function(image, name);
    };
    {
      // GLXPlayerLogin passes the configured host:port to this constructor.
      // Its SSL flag comes from CGLLive; an explicit port is not rewritten to
      // 443. Discovery therefore must advertise the HTTPS listener itself.
      std::vector<uint32_t> http(0x305c / 4);
      const std::string endpoint = "127.0.0.1:" + std::to_string(port);
      const auto object = pointer(http.data());
      nova::abi::invoke(function("_ZN13GLXPlayerHttpC1EPcS0_S0_b"),
                        {object, pointer(endpoint.c_str()), pointer("4"),
                         pointer("/legacy"), 1});
      const auto *bytes = reinterpret_cast<const uint8_t *>(http.data());
      check(*reinterpret_cast<const uint16_t *>(bytes + 0x3038) == port &&
                bytes[0x303a] == 0 && bytes[0x304c] == 1,
            "original HTTPS login preserves advertised explicit port");
      const auto proxy = http[1];
      const auto ssl_socket = *reinterpret_cast<const uint32_t *>(proxy + 0xc);
      check(ssl_socket && *reinterpret_cast<const uint32_t *>(ssl_socket) ==
                              image.symbol("_ZTV10CSSLSocket") + 8,
            "original login selects CSSLSocket");
      nova::abi::invoke(function("_ZN13GLXPlayerHttpD1Ev"), {object});
    }
    std::vector<uint32_t> socket(0x874 / 4);
    const auto receiver = pointer(socket.data());
    // Exercise the corrected four-byte native DNS cache copy with zero octets.
    const char *host = "127.0.0.1";
    nova::abi::invoke(
        function("_ZN10CSSLSocketC1EPciP23GLXPlayerSocketObserver"),
        {receiver, pointer(host), static_cast<uint32_t>(port), 0});
    nova::abi::invoke(function("_ZN10CSSLSocket5StartEv"), {receiver});
    check(nova::abi::invoke_bool(function("_ZN10CSSLSocket12CreateSocketEv"),
                                 {receiver}),
          "original TLS socket creation");
    bool connected = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (!connected && std::chrono::steady_clock::now() < deadline) {
      connected = nova::abi::invoke_bool(
          function("_ZN10CSSLSocket13ConnectByNameEPKct"),
          {receiver, pointer(host), static_cast<uint32_t>(port)});
      if (!connected &&
          nova::abi::invoke_bool(function("_ZN10CSSLSocket15IsErrorOccurredEv"),
                                 {receiver})) {
        const auto child = socket[0x858 / 4];
        std::fprintf(
            stderr, "TLS native error=%u child=%08x fd=%u state=%u ssl=%08x\n",
            nova::abi::invoke(function("_ZN10CSSLSocket12GetLastErrorEv"),
                              {receiver}),
            child, *reinterpret_cast<uint32_t *>(child + 8),
            *reinterpret_cast<uint32_t *>(child + 12), socket[0x860 / 4]);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto child = socket[0x858 / 4];
    std::fprintf(stderr,
                 "TLS final child fd=%u state=%u connect=%u lookup=%u ssl=%08x "
                 "errno=%u\n",
                 *reinterpret_cast<uint32_t *>(child + 8),
                 *reinterpret_cast<uint32_t *>(child + 4),
                 *reinterpret_cast<uint32_t *>(child + 0x848),
                 *reinterpret_cast<uint32_t *>(child + 0x84c),
                 socket[0x860 / 4],
                 nova::abi::invoke(function("_ZN10CSSLSocket12GetLastErrorEv"),
                                   {receiver}));
    check(connected == trusted, "original TLS trust outcome");
    if (trusted) {
      const auto ssl = socket[0x860 / 4];
      check(ssl && nova::abi::invoke(function("SSL_get_verify_result"),
                                     {ssl}) == 0,
            "original certificate verification must return zero");
      if (argc >= 6) {
        const auto fd = nova::bionic::descriptor(
            static_cast<int>(nova::abi::invoke(function("SSL_get_fd"), {ssl})));
        check(fd && fd->is_socket(), "TLS test descriptor");
        u_long nonblocking = 1;
        int small_buffer = 1024;
        check(ioctlsocket(fd->socket, FIONBIO, &nonblocking) == 0 &&
                  setsockopt(fd->socket, SOL_SOCKET, SO_SNDBUF,
                             reinterpret_cast<const char *>(&small_buffer),
                             sizeof(small_buffer)) == 0,
              "TLS nonblocking fragmented-write setup");
        std::vector<uint8_t> payload(131071);
        for (size_t i = 0; i < payload.size(); ++i)
          payload[i] = static_cast<uint8_t>(i % 251);
        const std::string response_header =
            "HTTP/1.1 200 OK\r\nX-Nova-Account: " + std::string(64, 'a') +
            "\r\nX-Nova-Session: " + std::string(64, 'b') + "\r\n\r\n";
        std::vector<uint8_t> expected(response_header.begin(),
                                      response_header.end());
        expected.insert(expected.end(), payload.begin(), payload.end());
        std::vector<uint8_t> response(expected.size());
        const auto transfer_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto retry = [&](uint32_t result) {
          const auto error =
              nova::abi::invoke(function("SSL_get_error"), {ssl, result});
          check(error == 2 || error == 3, "TLS transfer error mapping");
          check(std::chrono::steady_clock::now() < transfer_deadline,
                "TLS transfer deadline");
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        for (;;) {
          const auto written = nova::abi::invoke(
              function("SSL_write"), {ssl, pointer(payload.data()),
                                      static_cast<uint32_t>(payload.size())});
          if (static_cast<int32_t>(written) > 0) {
            check(written == payload.size(), "complete TLS write");
            break;
          }
          retry(written);
        }
        size_t received = 0;
        while (received < response.size()) {
          // Small reads exercise retained plaintext independently of encrypted
          // socket readiness and the server's uneven TLS record boundaries.
          const auto capacity = static_cast<uint32_t>(
              std::min<size_t>(97, response.size() - received));
          const auto count = nova::abi::invoke(
              function("SSL_read"),
              {ssl, pointer(response.data() + received), capacity});
          if (static_cast<int32_t>(count) > 0) {
            check(count <= capacity, "TLS read bounds");
            received += count;
          } else
            retry(count);
        }
        check(response == expected,
              "TLS record fragmentation/coalescing round trip");
        const auto credential = nova::net::NativeServerTls::login_credential();
        check(credential && credential->account == std::string(64, 'a') &&
                  credential->token == std::string(64, 'b'),
              "verified TLS login credential publication");
        uint8_t byte = 0;
        for (;;) {
          const auto count =
              nova::abi::invoke(function("SSL_read"), {ssl, pointer(&byte), 1});
          if (truncated && static_cast<int32_t>(count) < 0 &&
              nova::abi::invoke(function("SSL_get_error"), {ssl, count}) == 1)
            break;
          if (count == 0) {
            check(!truncated && nova::abi::invoke(function("SSL_get_error"),
                                                  {ssl, 0}) == 6,
                  "TLS close notify");
            break;
          }
          check(static_cast<int32_t>(count) < 0, "unexpected TLS trailer");
          retry(count);
        }
      }
    } else {
      const auto ssl = socket[0x860 / 4];
      check(ssl && nova::abi::invoke(function("SSL_get_verify_result"),
                                     {ssl}) != 0,
            "negative must reach actual certificate verification");
    }
    nova::abi::invoke(function("_ZN10CSSLSocket11CloseSocketEv"), {receiver});
    nova::abi::invoke(function("_ZN10CSSLSocketD1Ev"), {receiver});
    if (lobby_port) {
      auto imported = [&](const char *name) {
        const auto *binding = imports.find(name);
        check(binding != nullptr, "network import");
        return binding->address;
      };
      auto fd = nova::abi::invoke(imported("socket"), {2, 1 | 0x800, 0});
      check(static_cast<int32_t>(fd) >= 0, "native lobby descriptor");
      sockaddr_in peer{};
      peer.sin_family = AF_INET;
      peer.sin_port = htons(static_cast<uint16_t>(lobby_port));
      check(InetPtonA(AF_INET, "127.0.0.1", &peer.sin_addr) == 1,
            "lobby loopback endpoint");
      const auto connected_result = nova::abi::invoke(
          imported("connect"), {fd, pointer(&peer), sizeof(peer)});
      check(connected_result == 0 || nova::bionic::guest_errno == 115,
            "native nonblocking lobby connect");
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(10);
      auto ready = [&](int16_t events) {
        for (;;) {
          check(std::chrono::steady_clock::now() < deadline,
                "lobby transport deadline");
          nova::net::PollFd poll{static_cast<int32_t>(fd), events, 0};
          const auto count =
              nova::abi::invoke(imported("poll"), {pointer(&poll), 1, 100});
          check(static_cast<int32_t>(count) >= 0 && !(poll.returned & (8 | 32)),
                "lobby poll failure");
          if (count && (poll.returned & events))
            return;
        }
      };
      const std::array<uint8_t, 12> frame{0, 10, 1, 2, 0, 0, 0, 1, 0, 0, 0, 0};
      for (;;) {
        ready(4);
        const auto count = nova::abi::invoke(
            imported("send"), {fd, pointer(frame.data()),
                               static_cast<uint32_t>(frame.size()), 0});
        if (static_cast<int32_t>(count) >= 0) {
          check(count == frame.size(), "native lobby frame write");
          break;
        }
        check(nova::bionic::guest_errno == 11, "TLS lobby write retry");
      }
      std::array<uint8_t, 12> reply{};
      size_t received = 0;
      while (received < reply.size()) {
        ready(1);
        const auto count = nova::abi::invoke(
            imported("recv"), {fd, pointer(reply.data() + received), 3, 0});
        if (static_cast<int32_t>(count) > 0) {
          check(count <= 3, "lobby read bounds");
          received += count;
        } else
          check(static_cast<int32_t>(count) < 0 &&
                    nova::bionic::guest_errno == 11,
                "TLS lobby read retry");
      }
      check(reply == frame, "native lobby bytes changed by TLS adapter");
      if (game_port) {
        const auto udp =
            nova::abi::invoke(imported("socket"), {2, 2 | 0x800, 0});
        check(static_cast<int32_t>(udp) >= 0, "native UDP descriptor");
        auto game_peer = peer;
        game_peer.sin_port = htons(static_cast<uint16_t>(game_port));
        auto send_packet = [&] {
          return nova::abi::invoke(imported("sendto"),
                                   {udp, pointer(frame.data()),
                                    static_cast<uint32_t>(frame.size()), 0,
                                    pointer(&game_peer), sizeof(game_peer)});
        };
        check(send_packet() == frame.size(),
              "native UDP authentication and payload length");
        std::array<uint8_t, 12> packet{};
        sockaddr_in source{};
        uint32_t source_length = sizeof(source);
        for (;;) {
          check(std::chrono::steady_clock::now() < deadline,
                "UDP adapter deadline");
          nova::net::PollFd poll{static_cast<int32_t>(udp), 1, 0};
          const auto polled =
              nova::abi::invoke(imported("poll"), {pointer(&poll), 1, 100});
          check(static_cast<int32_t>(polled) >= 0, "native UDP poll");
          if (!polled)
            continue;
          nova::net::PollFd repeated{static_cast<int32_t>(udp), 1, 0};
          check(nova::abi::invoke(imported("poll"), {pointer(&repeated), 1, 0}) == 1 && (repeated.returned & 1),
                "native datagram remains readable after internal records drain");
          const auto count =
              nova::abi::invoke(imported("recvfrom"),
                                {udp, pointer(packet.data()),
                                 static_cast<uint32_t>(packet.size()), 0,
                                 pointer(&source), pointer(&source_length)});
          check(static_cast<int32_t>(count) >= 0,
                "readiness must not expose internal/forged traffic as an empty native read");
          check(count == packet.size() && packet == frame &&
                    source.sin_port == game_peer.sin_port &&
                    source.sin_addr.s_addr == game_peer.sin_addr.s_addr,
                "native UDP bytes/source unchanged after authentication");
          break;
        }
        check(nova::net::NativeSnapshotPresentation::generation(0,false)==2 &&
                  nova::net::NativeSnapshotPresentation::generation(0,true)==0,
              "authenticated life declarations reach presentation without native parser exposure");
        nova::net::pc::Snapshot proposal{0,2,1,100,{1.25f,-2.5f,3.75f},
            *nova::net::pc::encode_angle(359.875),*nova::net::pc::encode_angle(-0.125)};
        check(nova::net::CommunityMatch::publish(proposal),"precise proposal uses the admitted native UDP descriptor");
        check(nova::abi::invoke(imported("fcntl"), {fd, 4, 2}) == 0,
              "restore native blocking lobby descriptor");
        while (nova::net::NativeSnapshotPresentation::generation(0,false) != 0) {
          check(std::chrono::steady_clock::now() < deadline, "control-only revoke deadline");
          nova::net::PollFd control_only{static_cast<int32_t>(fd), 1, 0};
          check(nova::abi::invoke(imported("poll"), {pointer(&control_only), 1, 100}) == 0 && !control_only.returned,
                "internal revoke must not expose a blocking native lobby read");
        }
        check(nova::abi::invoke(imported("send"),
                                {fd, pointer(frame.data()), static_cast<uint32_t>(frame.size()), 0}) == frame.size(),
              "control-only revoke drained without native read");
        received = 0;
        reply.fill(0);
        while (received < reply.size()) {
          ready(1);
          const auto count = nova::abi::invoke(
              imported("recv"), {fd, pointer(reply.data() + received), 3, 0});
          if (static_cast<int32_t>(count) > 0)
            received += count;
          else
            check(static_cast<int32_t>(count) < 0 &&
                      nova::bionic::guest_errno == 11,
                  "revoke read retry");
        }
        check(reply == frame,
              "secret grant/revoke never reaches native reader");
        check(nova::net::NativeSnapshotPresentation::generation(0,false)==0,
              "revocation clears presentation generation");
        check(!nova::net::CommunityMatch::publish(proposal),"revocation blocks precise proposals");
        nova::net::PollFd revoked{static_cast<int32_t>(udp), 1, 0};
        check(nova::abi::invoke(imported("poll"), {pointer(&revoked), 1, 0}) == 0 && !revoked.returned,
              "revoked match cannot signal readable gameplay during menu teardown");
        check(static_cast<int32_t>(send_packet()) < 0 &&
                  nova::bionic::guest_errno == 104,
              "revoked UDP cannot fall back to plaintext");
        check(nova::abi::invoke(imported("send"),
                                {fd, pointer(frame.data()),
                                 static_cast<uint32_t>(frame.size()), 0}) ==
                  frame.size(),
              "post-revocation lobby acknowledgement");
        check(nova::abi::invoke(imported("close"), {udp}) == 0,
              "native UDP close");
      }
      check(nova::abi::invoke(imported("close"), {fd}) == 0,
            "native lobby close");
      for (const auto service_port : {uint16_t{80}, uint16_t{20001},
                                      static_cast<uint16_t>(port), uint16_t{443}}) {
        fd = nova::abi::invoke(imported("socket"), {2, 1 | 0x800, 0});
        check(static_cast<int32_t>(fd) >= 0, "bootstrap descriptor");
        peer.sin_port = htons(service_port);
        const auto bootstrap_connect = nova::abi::invoke(
            imported("connect"), {fd, pointer(&peer), sizeof(peer)});
        check(bootstrap_connect == 0 || nova::bionic::guest_errno == 115,
              "bootstrap redirects to HTTPS service");
        const std::string request =
            "GET /config/fixture HTTP/1.1\r\nHost: original.invalid\r\n\r\n";
        for (;;) {
          ready(4);
          const auto count = nova::abi::invoke(
              imported("send"), {fd, pointer(request.data()),
                                 static_cast<uint32_t>(request.size()), 0});
          if (count == request.size())
            break;
          check(static_cast<int32_t>(count) < 0 &&
                    nova::bionic::guest_errno == 11,
                "bootstrap TLS retry");
        }
        const std::string expected =
            "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nbootstrap";
        std::string actual;
        while (actual.size() < expected.size()) {
          ready(1);
          std::array<char, 7> chunk{};
          const auto count = nova::abi::invoke(
              imported("recv"), {fd, pointer(chunk.data()),
                                 static_cast<uint32_t>(chunk.size()), 0});
          if (static_cast<int32_t>(count) > 0)
            actual.append(chunk.data(), count);
          else
            check(static_cast<int32_t>(count) < 0 &&
                      nova::bionic::guest_errno == 11,
                  "bootstrap read retry");
        }
        check(actual == expected, "bootstrap HTTP response unchanged inside TLS");
        check(nova::abi::invoke(imported("close"), {fd}) == 0, "bootstrap close");
      }
      // Eve uses GLXTcpConnect rather than the legacy CSSLSocket tested above.
      // Exercise its real nonblocking retry/error path, not just import calls.
      std::vector<uint32_t> eve(0x29050 / 4);
      const auto eve_object = pointer(eve.data());
      nova::abi::invoke(function("_ZN9GLBaseLib13GLXTcpConnectC1EPci"),
                        {eve_object, pointer("127.0.0.1"), 20001});
      check(nova::abi::invoke_bool(function("_ZN9GLBaseLib13GLXTcpConnect12CreateSocketEv"), {eve_object}),
            "Eve original socket creation");
      const auto eve_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!nova::abi::invoke_bool(function("_ZN9GLBaseLib13GLXTcpConnect7ConnectEv"), {eve_object})) {
        check(std::chrono::steady_clock::now() < eve_deadline, "Eve original connect deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      const std::string eve_request = "GET /config/fixture HTTP/1.1\r\nHost: original.invalid\r\n\r\n";
      const auto eve_socket = eve[0xc / 4];
      check(nova::abi::invoke(function("_ZN9GLBaseLib17GLXSockAndroidImp6SelectENS_10SelectFlagE"),
                              {eve_socket, 0}) == 0,
            "Eve read readiness must not expose an unfinished TLS handshake");
      auto *eve_bytes = reinterpret_cast<uint8_t *>(eve.data());
      *reinterpret_cast<uint16_t *>(eve_bytes + 0x19012) = static_cast<uint16_t>(eve_request.size());
      *reinterpret_cast<uint16_t *>(eve_bytes + 0x19014) = static_cast<uint16_t>(eve_request.size());
      std::memcpy(eve_bytes + 0x19016, eve_request.data(), eve_request.size());
      while (*reinterpret_cast<uint16_t *>(eve_bytes + 0x19014)) {
        const auto sent = static_cast<int32_t>(nova::abi::invoke(function("_ZN9GLBaseLib13GLXTcpConnect8SendDataEv"), {eve_object}));
        check(sent > 0 || sent == -2, "Eve send must preserve retryable TLS handshake");
        if (sent == -2)
          check(nova::abi::invoke(function("_ZN9GLBaseLib17GLXSockAndroidImp6SelectENS_10SelectFlagE"),
                                  {eve_socket, 0}) == 0,
                "Eve handshake retry is not readable application data");
        check(std::chrono::steady_clock::now() < eve_deadline, "Eve send deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      const std::string eve_expected = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nbootstrap";
      std::string eve_response;
      while (eve_response.size() < eve_expected.size()) {
        uint32_t data = 0;
        const auto received = static_cast<int32_t>(nova::abi::invoke(function("_ZN9GLBaseLib13GLXTcpConnect8RecvDataEPPc"), {eve_object, pointer(&data)}));
        if (received > 0) eve_response.append(reinterpret_cast<const char *>(data), received);
        check(std::chrono::steady_clock::now() < eve_deadline, "Eve receive deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      check(eve_response == eve_expected, "Eve original transport response");
      nova::abi::invoke(function("_ZN9GLBaseLib13GLXTcpConnectD1Ev"), {eve_object});
    }
    std::printf(
        "PASS original loopback TLS trusted=%d connected=%d; socket closed\n",
        trusted, connected);
    std::fflush(nullptr);
    // Original OpenSSL/global destructors share the single-image process
    // lifetime.
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), 1);
    return 1;
  }
}
