// Test-only import observers. Baseline calls the registered production adapters.
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "loader/diagnostics.h"
#include "loader/elf_image.h"
#include "net/network.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using U = uint32_t;
using AI = nova::net::AddressInfo;
using TV = nova::net::Timeval;
struct Address { uint16_t family, port; U ip; unsigned char zero[8]; };
static_assert(sizeof(void *) == 4 && sizeof(Address) == 16);
nova::ImportRegistry production;
bool server = false, injected = false;
int fault = 0, primary = -1, client_binds = 0, sends = 0, observations = 0;
std::map<int, uint64_t> live;
std::set<uint64_t> retired;
Address observed_peer{};
bool have_peer = false;

[[noreturn]] void reject(const char *tag) {
    std::fprintf(stderr, "FAIL HOST %s\n", tag);
    std::fflush(nullptr);
    std::exit(86); // Process owns all descriptors; never unwind through ELF frames.
}
void need(bool ok, const char *tag) { if (!ok) reject(tag); }
void note(const char *op, int rc, int extra = 0) {
    need(++observations <= 256, "metadata-budget");
    std::printf("OP %s rc=%d errno=%d extra=%d\n", op, rc,
                rc < 0 ? nova::bionic::guest_errno : 0, extra);
}
template<class T> T actual(const char *name) {
    const auto *b = production.find(name);
    need(b && b->kind == nova::ImportKind::function && !b->placeholder, "actual-import");
    return reinterpret_cast<T>(b->address);
}
bool loopback(const void *p, U size) {
    if (!p || size != 16) return false;
    Address a{}; std::memcpy(&a, p, 16);
    return a.family == 2 && a.ip == 0x0100007fu;
}
void unbound(int fd) {
    auto d = nova::bionic::descriptor(fd);
    need(d && d->is_socket(), "unbound-descriptor");
    sockaddr_in a{}; int size = sizeof a;
    const int rc = ::getsockname(d->socket, reinterpret_cast<sockaddr *>(&a), &size);
    // Winsock can return WSAEINVAL for a genuinely unbound UDP socket. This is
    // an observer of OS ownership, not a guest ABI expectation or adapter fix.
    need((rc == SOCKET_ERROR && WSAGetLastError() == WSAEINVAL) ||
         (rc == 0 && a.sin_port == 0), "client-unbound-os");
}
int __cdecl socket_observer(int domain, int type, int protocol) {
    need(domain == 2 && type == 2 && protocol == 17, "udp-only");
    int fd = actual<int(__cdecl *)(int, int, int)>("socket")(domain, type, protocol);
    if (fd >= 0) {
        auto d = nova::bionic::descriptor(fd);
        need(d && !live.contains(fd) && !retired.contains(d->serial), "fresh-descriptor-serial");
        live.emplace(fd, d->serial);
        if (primary == -1) primary = fd;
    }
    note("socket", fd); return fd;
}
int __cdecl bind_observer(int fd, const void *p, U size) {
    if (!server) { ++client_binds; reject("client-unbound"); }
    need(loopback(p, size), "loopback-bind-only");
    const auto &a = *static_cast<const Address *>(p);
    need(ntohs(a.port) == 7891, "server-port7891");
    int rc = actual<int(__cdecl *)(int, const void *, U)>("bind")(fd, p, size);
    if (fault == 7 && fd != primary && rc == -1 && nova::bionic::guest_errno == 98) {
        nova::bionic::guest_errno = WSAEADDRINUSE; injected = true;
        std::puts("INJECT errno98");
    }
    note("bind", rc, fd); return rc;
}
int __cdecl resolve_observer(const char *node, const char *service, const AI *hints, AI **out) {
    need(!node && service && std::strcmp(service, server ? "7891" : "7890") == 0 &&
         hints && hints->flags == 1 && hints->family == 2 && hints->type == 2 && hints->protocol == 17,
         "passive-numeric-service-only");
    int rc = actual<int(__cdecl *)(const char *, const char *, const AI *, AI **)>("getaddrinfo")
        (node, service, hints, out);
    note("resolve", rc, server ? 7891 : 7890); return rc;
}
int __cdecl option_observer(int fd, int level, int name, const void *value, U size) {
    // Preserve the observed SO_BROADCAST option, without any broadcast send.
    need(!server && level == 1 && name == 6 && value && size == 4 &&
         *static_cast<const int *>(value) == 1, "only-observed-option");
    int rc = actual<int(__cdecl *)(int, int, int, const void *, U)>("setsockopt")
        (fd, level, name, value, size);
    note("setsockopt", rc); return rc;
}
int __cdecl select_observer(int n, U *read, U *write, U *except, TV *time) {
    need(read && !write && !except && time && time->seconds >= 0 && time->seconds <= 2 &&
         time->micros == 0, "bounded-read-select");
    const bool pre_send = !server && sends == 0 && live.contains(primary);
    if (pre_send) unbound(primary);
    int rc = actual<int(__cdecl *)(int, U *, U *, U *, TV *)>("select")(n, read, write, except, time);
    if (pre_send) unbound(primary);
    note("select", rc); return rc;
}
int __cdecl send_observer(int fd, const void *bytes, U size, int flags, const void *p, U length) {
    need(flags == 0, "send-flags");
    // Invalid/stale calls are intentionally delegated for real errno checks.
    const bool valid = live.contains(fd) && (bytes || !size) && size <= 0x7fffffffu;
    if (valid) {
        need(loopback(p, length), "loopback-send-only");
        const auto &to = *static_cast<const Address *>(p);
        if (server) {
            need(have_peer && std::memcmp(&to, &observed_peer, 16) == 0, "reply-endpoint16");
        } else {
            need(client_binds == 0 && ntohs(to.port) == 7891, "client-unbound-target");
            if (!sends) { unbound(fd); std::puts("PRE_SEND unbound=1 binds=0"); }
        }
        if (fault == 3 && server && !injected) {
            injected = true; std::puts("INJECT omit-reply");
            return static_cast<int>(size); // deliberate omission, never production behavior
        }
    }
    int rc = actual<int(__cdecl *)(int, const void *, U, int, const void *, U)>("sendto")
        (fd, bytes, size, flags, p, length);
    if (valid && rc >= 0) ++sends;
    note("sendto", rc); return rc;
}
int __cdecl receive_observer(int fd, void *bytes, U size, int flags, void *p, U *length) {
    need(flags == 0 || flags == 2, "receive-flags");
    const U capacity = length ? *length : 0;
    int rc = actual<int(__cdecl *)(int, void *, U, int, void *, U *)>("recvfrom")
        (fd, bytes, size, flags, p, length);
    if (rc >= 0 && p && length && capacity >= 16 && *length == 16) {
        need(loopback(p, *length), "received-loopback-only");
        if (server) { std::memcpy(&observed_peer, p, 16); have_peer = true; }
        if (server && !injected && flags == 0) {
            auto &a = *static_cast<Address *>(p);
            if (fault == 4) { a.zero[7] = 0x5a; injected = true; std::puts("INJECT dirty-address"); }
            if (fault == 5) { *length = 8; injected = true; std::puts("INJECT short-address"); }
            if (fault == 6) { a.port = 0; injected = true; std::puts("INJECT lost-source-port"); }
        }
    }
    note("recvfrom", rc, static_cast<int>(capacity)); return rc;
}
int __cdecl close_observer(int fd) {
    if (fault == 8 && !injected && live.contains(fd)) {
        injected = true; std::puts("INJECT omit-close"); return 0;
    }
    SOCKET previous = INVALID_SOCKET;
    if (live.contains(fd)) {
        auto d = nova::bionic::descriptor(fd);
        need(d && d->is_socket(), "close-live-socket");
        previous = d->socket; // Release this shared_ptr before the actual close.
    }
    int rc = actual<int(__cdecl *)(int)>("close")(fd);
    if (rc == 0) {
        auto it = live.find(fd); need(it != live.end(), "close-owned-descriptor");
        retired.insert(it->second); live.erase(it);
        int type = 0, size = sizeof type;
        need(::getsockopt(previous, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &size) ==
                 SOCKET_ERROR && WSAGetLastError() == WSAENOTSOCK, "os-socket-closed");
    }
    note("close", rc); return rc;
}
void __cdecl event(const char *tag, int a, int b) {
    std::printf("EVENT %s %d %d\n", tag, a, b);
}
void __cdecl failure(const char *tag, int line, int error) {
    std::fprintf(stderr, "FAIL GUEST %s line=%d errno=%d\n", tag, line, error);
}
int parse_fault(const std::string &name) {
    const char *names[] = {"none", "client-bind", "reply-endpoint", "omit-reply", "dirty-address",
                          "short-address", "lost-source-port", "errno98", "omit-close"};
    for (int i = 0; i < 9; ++i) if (name == names[i]) return i;
    throw std::runtime_error("unknown mutation");
}
} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (argc != 4) return 2;
    try {
        const std::string role = argv[2];
        if (role != "server" && role != "client") return 2;
        server = role == "server"; fault = parse_fault(argv[3]);
        if ((fault == 1 && server) || (fault > 1 && !server)) return 2;
        // No profile configuration, preferences, identity or payload logs.
        nova::bionic::register_boot(production);
        nova::ImportRegistry imports;
        for (const auto *name : {"__errno", "freeaddrinfo", "getsockname", "memcpy", "memset", "memcmp"}) {
            auto address = actual<void(__cdecl *)()>(name);
            imports.function(name, reinterpret_cast<const void *>(address));
        }
#define OBS(name, fn) imports.function(name, reinterpret_cast<const void *>(&fn))
        OBS("socket", socket_observer); OBS("bind", bind_observer);
        OBS("getaddrinfo", resolve_observer); OBS("setsockopt", option_observer);
        OBS("select", select_observer); OBS("sendto", send_observer);
        OBS("recvfrom", receive_observer); OBS("close", close_observer);
        OBS("lan_event", event); OBS("lan_failure", failure);
#undef OBS
        nova::ElfImage image(argv[1], imports);
        nova::Diagnostics diagnostics(image);
        need(image.initializer_count() == 0, "no-initializers");
        for (const auto &entry : image.imports()) need(!entry.trapped && !entry.placeholder, "bound-imports");
        const U args[] = {server ? 1u : 0u, static_cast<U>(fault)};
        U rc = nova::abi::call_guest(image.lookup("lan_start"), args, 2);
        if (rc == 77) return 77;
        if (rc) return 1;
        if (server) std::puts("READY 7891 loopback=127.0.0.1");
        rc = nova::abi::call0(image.lookup("lan_exchange"));
        if (rc) return 1;
        need(live.empty(), "all-descriptors-closed");
        need(server || client_binds == 0, "client-unbound");
        need(fault == 0 || injected, "mutation-reached");
        std::printf("PASS %s imports=production fault=%d binds=%d\n", role.c_str(), fault, client_binds);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL HOST exception=%s\n", e.what()); return 1;
    }
}
