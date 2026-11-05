#include "net/interfaces_windows.h"
#include <cstring>
#include <utility>

namespace nova::net {
namespace {
int provider_error(ULONG error) {
    switch (error) {
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: return 12;
    case ERROR_ACCESS_DENIED: return 13;
    case ERROR_NOT_SUPPORTED: return 95;
    case ERROR_INVALID_PARAMETER: return 22;
    case ERROR_NOT_FOUND: return 11; // snapshot raced removal/change
    default: return 5;
    }
}
uint16_t linux_flags(const MIB_IF_ROW2 &row, ULONG adapter_flags) {
    uint16_t flags = 0;
    if (row.AdminStatus == NET_IF_ADMIN_STATUS_UP) flags |= 0x0001; // IFF_UP: admin, not carrier
    if (row.AccessType == NET_IF_ACCESS_BROADCAST) flags |= 0x0002;
    if (row.AccessType == NET_IF_ACCESS_LOOPBACK) flags |= 0x0008;
    if (row.AccessType == NET_IF_ACCESS_POINT_TO_POINT) flags |= 0x0010;
    // Provisional faithful subset: operational UP supplies IFF_RUNNING, while
    // UNKNOWN/DORMANT never fabricate readiness. Admin/operational stay distinct.
    if (row.AdminStatus == NET_IF_ADMIN_STATUS_UP && row.OperStatus == IfOperStatusUp)
        flags |= 0x0040;
    if (!(adapter_flags & IP_ADAPTER_NO_MULTICAST)) flags |= 0x1000;
    // PROMISC/ALLMULTI/NOARP and Linux-private flags lack equivalent evidence.
    // Windows DHCP/DDNS/receive-only flags are NOT Linux IFF bits.
    return flags;
}
} // namespace
int read_windows_interfaces(InterfaceSnapshot &output, const WindowsInterfaceApi &api) {
    if (!api.addresses || !api.entry) return 5;
    ULONG bytes = 15 * 1024;
    // Alignment8 also satisfies IP_ADAPTER_ADDRESSES_LH's leading ULONGLONG.
    std::vector<uint64_t> storage;
    IP_ADAPTER_ADDRESSES *head = nullptr;
    bool ready = false;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (bytes > 16 * 1024 * 1024) return 105;
        storage.assign((bytes + 7u) / 8u, 0);
        head = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data());
        constexpr ULONG query_flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_INCLUDE_ALL_INTERFACES;
        const ULONG rc = api.addresses(AF_INET, query_flags, nullptr, head, &bytes);
        if (rc == ERROR_NO_DATA) { output.entries.clear(); return 0; }
        if (rc == ERROR_BUFFER_OVERFLOW) continue;
        if (rc != NO_ERROR) return provider_error(rc);
        ready = true; break;
    }
    if (!ready) return 11; // bounded retry exhaustion, never partial/fake success
    InterfaceSnapshot next;
    size_t address_count = 0;
    for (auto *a = head; a; a = a->Next) {
        if (next.entries.size() == 4096) return 105;
        if (a->Length < offsetof(IP_ADAPTER_ADDRESSES, Luid) + sizeof a->Luid || !a->Luid.Value)
            return 5;
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = a->Luid;
        const auto rc = api.entry(&row);
        if (rc != NO_ERROR) return provider_error(rc);
        if (row.InterfaceLuid.Value != a->Luid.Value) return 5;
        InterfaceEntry entry;
        entry.id = a->Luid.Value;
        entry.flags = linux_flags(row, a->Flags);
        size_t walked = 0;
        for (auto *u = a->FirstUnicastAddress; u; u = u->Next) {
            if (++walked > 65536) return 105;
            if (!u->Address.lpSockaddr || u->Address.iSockaddrLength < static_cast<int>(sizeof(sockaddr)))
                return 5;
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            if (u->Address.iSockaddrLength < static_cast<int>(sizeof(sockaddr_in))) return 5;
            if (++address_count > 65536) return 105;
            const auto *address = reinterpret_cast<const sockaddr_in *>(u->Address.lpSockaddr);
            std::array<uint8_t, 4> ip{};
            std::memcpy(ip.data(), &address->sin_addr, ip.size());
            entry.ipv4.push_back(ip);
        }
        // Keep provider order and every IPv4 address, including down/loopback.
        // Neither route metrics nor a guessed preferred endpoint reorder it.
        next.entries.push_back(std::move(entry));
    }
    output = std::move(next);
    return 0;
}
int windows_interface_snapshot(InterfaceSnapshot &output) {
    return read_windows_interfaces(output, {&GetAdaptersAddresses, &GetIfEntry2});
}
} // namespace nova::net
