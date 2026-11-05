// Synthetic provider only: no sockets, live adapter query, profile or game input.
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "loader/diagnostics.h"
#include "loader/elf_image.h"
#include "net/interfaces_windows.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
int mode = 0, calls = 0, fault = 0;
bool injected = false;
nova::ImportRegistry production;
[[noreturn]] void reject(const char *tag) {
    std::fprintf(stderr, "FAIL HOST %s\n", tag); std::fflush(nullptr); std::exit(86);
}
void need(bool condition, const char *tag) { if (!condition) reject(tag); }
template<class T> T actual(const char *name) {
    const auto *b = production.find(name);
    need(b && b->kind == nova::ImportKind::function && !b->placeholder, "production-import");
    return reinterpret_cast<T>(b->address);
}
void inject(const char *name) {
    if (!injected) { injected = true; std::printf("INJECT %s\n", name); }
}
struct FakeData {
    IP_ADAPTER_ADDRESSES adapters[5];
    IP_ADAPTER_UNICAST_ADDRESS unicast[5];
    sockaddr_in addresses[5];
};
ULONG WINAPI fake_addresses(ULONG family, ULONG flags, void *reserved,
                            IP_ADAPTER_ADDRESSES *output, ULONG *bytes) {
    need(family == AF_INET && reserved == nullptr && output && bytes, "adapter-query-contract");
    constexpr ULONG expected = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
        GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_INCLUDE_ALL_INTERFACES;
    need(flags == expected, "adapter-query-flags");
    ++calls;
    if (mode == 1) return ERROR_NO_DATA;
    if (mode == 2) return ERROR_NOT_ENOUGH_MEMORY;
    if (mode == 3) return ERROR_ACCESS_DENIED;
    if (mode == 4) return ERROR_NOT_SUPPORTED;
    if (mode == 5) return ERROR_GEN_FAILURE;
    if (mode == 14) return ERROR_INVALID_PARAMETER;
    if (mode == 7 || (mode == 6 && calls == 1)) {
        *bytes += 1024; return ERROR_BUFFER_OVERFLOW;
    }
    if (mode == 12) { *bytes = 16 * 1024 * 1024 + 1; return ERROR_BUFFER_OVERFLOW; }
    if (*bytes < sizeof(FakeData)) { *bytes = sizeof(FakeData); return ERROR_BUFFER_OVERFLOW; }
    need(reinterpret_cast<uintptr_t>(output) % alignof(FakeData) == 0, "adapter-buffer-alignment");
    auto *data = reinterpret_cast<FakeData *>(output);
    std::memset(data, 0, sizeof *data);
    static constexpr uint8_t ips[5][4] = {{10,20,30,1},{10,20,30,2},{192,0,2,4},{127,0,0,1},{198,51,100,9}};
    for (unsigned i = 0; i < 5; ++i) {
        auto &a = data->adapters[i]; a.Length = sizeof a; a.Luid.Value = i + 1;
        a.Next = i == 4 ? nullptr : &data->adapters[i + 1];
        // Deliberately unrelated host bits: must not become IFF_DEBUG/LOOPBACK.
        a.Flags = IP_ADAPTER_DDNS_ENABLED | IP_ADAPTER_DHCP_ENABLED;
        if (i == 1 || i == 2 || i == 4) a.Flags |= IP_ADAPTER_NO_MULTICAST;
        auto &u = data->unicast[i]; u.Length = sizeof u;
        u.Address.lpSockaddr = reinterpret_cast<sockaddr *>(&data->addresses[i]);
        u.Address.iSockaddrLength = sizeof(sockaddr_in);
        data->addresses[i].sin_family = AF_INET;
        data->addresses[i].sin_port = 0x3412; // Host port/padding must never escape.
        std::memset(data->addresses[i].sin_zero, 0x5a, sizeof data->addresses[i].sin_zero);
        std::memcpy(&data->addresses[i].sin_addr, ips[i], 4);
    }
    data->adapters[0].FirstUnicastAddress = &data->unicast[0];
    data->unicast[0].Next = &data->unicast[1];
    data->adapters[1].FirstUnicastAddress = &data->unicast[2];
    data->adapters[2].FirstUnicastAddress = &data->unicast[3];
    data->adapters[4].FirstUnicastAddress = &data->unicast[4];
    if (mode == 13) data->adapters[4].Luid.Value = (std::numeric_limits<uint64_t>::max)();
    *bytes = sizeof *data;
    return NO_ERROR;
}
DWORD WINAPI fake_entry(MIB_IF_ROW2 *row) {
    need(row != nullptr && row->InterfaceIndex == 0, "adapter-row-contract");
    if (mode == 8) return ERROR_NOT_FOUND;
    const auto id = row->InterfaceLuid.Value;
    need((id >= 1 && id <= 5) || (mode == 13 && id == (std::numeric_limits<uint64_t>::max)()),
         "adapter-row-identity");
    row->AdminStatus = id == 2 || ((mode == 9 || mode == 15) && id == 1) ?
        NET_IF_ADMIN_STATUS_DOWN : NET_IF_ADMIN_STATUS_UP;
    row->OperStatus = id == 2 || id == 4 || (mode == 9 && id == 1) ? IfOperStatusDown : IfOperStatusUp;
    row->AccessType = id == 2 ? NET_IF_ACCESS_POINT_TO_POINT :
                      id == 3 ? NET_IF_ACCESS_LOOPBACK : NET_IF_ACCESS_BROADCAST;
    return NO_ERROR;
}
int synthetic_provider(nova::net::InterfaceSnapshot &out) {
    if (mode == 11) throw std::bad_alloc();
    const int rc = nova::net::read_windows_interfaces(out, {&fake_addresses, &fake_entry});
    if (!rc && mode == 10 && out.entries.size() >= 2) out.entries[1].id = out.entries[0].id;
    if (!rc && fault == 3) {
        for (auto &entry : out.entries) if (entry.id == 4) {
            entry.flags &= static_cast<uint16_t>(~1u); inject("oper-up");
        }
    }
    return rc;
}
void __cdecl select_provider(int selected) {
    if (mode == 7) need(calls == 4, "bounded-provider-retries");
    if (mode == 6) need(calls == 2, "provider-resized-once");
    mode = selected; calls = 0;
}
void __cdecl failure(const char *tag, int line, int error) {
    std::fprintf(stderr, "FAIL GUEST %s line=%d errno=%d\n", tag, line, error);
}
int __cdecl fault_ioctl(int fd, uint32_t request, ...) {
    if (fault == 1) { inject("omit-dispatch"); return nova::bionic::fail(25); }
    // This negative lane is reached at the first, valid count-only call.
    need(fault == 2 && request == 0x8912, "fault-call-shape");
    va_list args; va_start(args, request); void *p = va_arg(args, void *); va_end(args);
    int rc = actual<int(__cdecl *)(int, uint32_t, ...)>("ioctl")(fd, request, p);
    if (!rc) { *static_cast<int32_t *>(p) = 200; inject("host-layout"); }
    return rc;
}
int fake_descriptor(bool socket) {
    auto d = std::make_shared<nova::bionic::Descriptor>();
    d->owned = false; // A token only; no real HANDLE/SOCKET exists or is closed.
    if (socket) { d->socket = static_cast<SOCKET>(0x1234); d->domain = 2; d->type = 2; }
    return nova::bionic::adopt_descriptor(std::move(d));
}
} // namespace
int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    try {
        const std::string choice = argv[2];
        if (choice == "baseline") fault = 0;
        else if (choice == "omit-dispatch") fault = 1;
        else if (choice == "host-layout") fault = 2;
        else if (choice == "oper-up") fault = 3;
        else return 2;
        nova::bionic::register_boot(production);
        nova::ImportRegistry imports;
        for (const auto *name : {"__errno", "close", "memcpy", "memset", "memmove"})
            imports.function(name, reinterpret_cast<const void *>(actual<void(__cdecl *)()>(name)));
        const void *ioctl_address = fault == 1 || fault == 2 ? reinterpret_cast<const void *>(&fault_ioctl) :
            reinterpret_cast<const void *>(actual<void(__cdecl *)()>("ioctl"));
        imports.function("ioctl", ioctl_address);
        imports.function("fixture_provider", reinterpret_cast<const void *>(&select_provider));
        imports.function("ioctl_failure", reinterpret_cast<const void *>(&failure));
        nova::net::ScopedInterfaceProvider provider(&synthetic_provider);
        need(nova::net::interface_name((std::numeric_limits<uint64_t>::max)()) == "n3w5e11264sgsf",
             "full-luid-name-bound");
        nova::ElfImage image(argv[1], imports);
        nova::Diagnostics diagnostics(image);
        need(image.initializer_count() == 0, "no-initializers");
        for (const auto &entry : image.imports()) need(!entry.trapped && !entry.placeholder, "bound-imports");
        const int fd = fake_descriptor(true), regular = fake_descriptor(false), stale = fake_descriptor(true);
        need(fd >= 3 && regular >= 3 && stale >= 3, "descriptor-adoption");
        need(actual<int(__cdecl *)(int)>("close")(stale) == 0, "stale-descriptor-close");
        const uint32_t args[] = {static_cast<uint32_t>(fd), static_cast<uint32_t>(regular), static_cast<uint32_t>(stale)};
        const auto rc = nova::abi::call_guest(image.lookup("interface_ioctl_test"), args, 3);
        if (rc) return 1;
        need(fault == 0, "negative-must-fail");
        std::puts("PASS interface-ioctl imports=production provider=synthetic sdk-adapter=production");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL HOST exception=%s\n", e.what()); return 2;
    }
}
