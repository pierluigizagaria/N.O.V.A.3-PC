#define _WINSOCK_DEPRECATED_NO_WARNINGS // The guest imports the legacy numeric inet_addr contract.
#include "net/network.h"
#include "platform/profile.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>
namespace nova::net {
namespace {
thread_local int host_error = 0;
std::string normalize(const char *name) {
    std::string out = name ? name : "";
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });
    if (out.ends_with('.'))
        out.pop_back();
    return out;
}
bool numeric(const std::string &text, int family, Address &address, uint32_t &size) {
    sockaddr_in v4{};
    if ((!family || family == 2) && InetPtonA(AF_INET, text.c_str(), &v4.sin_addr) == 1) {
        v4.sin_family = 2;
        size = sizeof v4;
        std::memcpy(&address, &v4, size);
        return true;
    }
    sockaddr_in6 v6{};
    if ((!family || family == 10) && InetPtonA(AF_INET6, text.c_str(), &v6.sin6_addr) == 1) {
        v6.sin6_family = 10;
        size = sizeof v6;
        std::memcpy(&address, &v6, size);
        return true;
    }
    return false;
}
// The only name lookup choke point. Deliberately never calls OS DNS.
bool resolve(const char *input, int family, bool numeric_only, Address &address, uint32_t &size) {
    initialize();
    auto name = normalize(input);
    if (numeric(name, family, address, size))
        return true;
    if (!numeric_only) {
        auto it = profile::config().hosts.find(name);
        if (it != profile::config().hosts.end()) {
            bool ok = it->second != "block" && numeric(it->second, family, address, size);
            std::fprintf(stderr, "resolver %s name=%s target=%s\n", ok ? "redirect" : "blocked",
                         name.c_str(), it->second.c_str());
            if (ok)
                return true;
        } else if (name == "localhost")
            return numeric(family == 10 ? "::1" : "127.0.0.1", family, address, size);
    }
    std::fprintf(stderr, "resolver blocked name=%s (no DNS fallback)\n", name.c_str());
    host_error = 1;
    return false;
}
int *__cdecl h_error_pointer() { return &host_error; }
struct HostStorage {
    HostEntry entry{};
    std::string name;
    char address[16]{};
    char *aliases[1]{};
    char *addresses[2]{address, nullptr};
};
thread_local HostStorage host;
HostEntry *host_result(const std::string &name, const Address &address) {
    host.name = name;
    host.entry.name = host.name.data();
    host.entry.aliases = host.aliases;
    host.entry.family = address.family;
    host.entry.length = address.family == 2 ? 4 : 16;
    std::memcpy(host.address, reinterpret_cast<const char *>(&address) + (address.family == 2 ? 4 : 8),
                host.entry.length);
    host.entry.addresses = host.addresses;
    host_error = 0;
    return &host.entry;
}
HostEntry *__cdecl host_by_name(const char *name) {
    Address address{};
    uint32_t size = 0;
    if (!name || !resolve(name, 2, false, address, size)) {
        host_error = 1;
        return nullptr;
    }
    return host_result(normalize(name), address);
}
HostEntry *__cdecl host_by_address(const void *bytes, uint32_t length, int family) {
    if (!bytes || (family != 2 && family != 10) || length != (family == 2 ? 4u : 16u)) {
        host_error = 1;
        return nullptr;
    }
    Address address{};
    address.family = static_cast<uint16_t>(family);
    std::memcpy(reinterpret_cast<char *>(&address) + (family == 2 ? 4 : 8), bytes, length);
    char text[INET6_ADDRSTRLEN]{};
    InetNtopA(family == 2 ? AF_INET : AF_INET6, const_cast<void *>(bytes), text, sizeof text);
    return host_result(text, address);
}
void __cdecl free_addresses(AddressInfo *p) {
    while (p) {
        auto next = p->next;
        delete[] p->canonical;
        delete p->address;
        delete p;
        p = next;
    }
}
int __cdecl addresses(const char *node, const char *service, const AddressInfo *hints, AddressInfo **out) {
    if (!out)
        return 4;
    *out = nullptr;
    int flags = hints ? hints->flags : 0, family = hints ? hints->family : 0,
        type = hints ? hints->type : 0, protocol = hints ? hints->protocol : 0;
    if (flags & ~15)
        return 3; // BSD AI_PASSIVE/CANONNAME/NUMERICHOST/NUMERICSERV.
    if (family != 0 && family != 2 && family != 10)
        return 5;
    if (type && type != 1 && type != 2)
        return 10;
    if (protocol && protocol != 6 && protocol != 17)
        return 13;
    if ((type == 1 && protocol == 17) || (type == 2 && protocol == 6))
        return 12;
    if (!node && !service)
        return 8;
    unsigned port = 0;
    if (service && *service) {
        auto end = service + std::strlen(service);
        auto parsed = std::from_chars(service, end, port);
        if (parsed.ec != std::errc{} || parsed.ptr != end) {
            if (flags & 8)
                return 8;
            std::string s(service);
            if (s == "http")
                port = 80;
            else if (s == "https")
                port = 443;
            else if (s == "domain")
                port = 53;
            else
                return 9;
        }
        if (port > 65535)
            return 9;
    }
    Address address{};
    uint32_t length = 0;
    if (!node) {
        numeric(family == 10 ? (flags & 1 ? "::" : "::1") : (flags & 1 ? "0.0.0.0" : "127.0.0.1"), family,
                address, length);
    } else if (!resolve(node, family, (flags & 4) != 0, address, length))
        return 8;
    auto network_port = htons(static_cast<u_short>(port));
    std::memcpy(reinterpret_cast<char *>(&address) + 2, &network_port, 2);
    AddressInfo *head = nullptr, **tail = &head;
    try {
        for (int current : {1, 2}) {
            if (type && type != current)
                continue;
            int proto = current == 1 ? 6 : 17;
            if (protocol && protocol != proto)
                continue;
            auto p = new AddressInfo{};
            *tail = p;
            tail = &p->next;
            p->flags = flags;
            p->family = address.family;
            p->type = current;
            p->protocol = proto;
            p->length = length;
            p->address = new Address(address);
            if ((flags & 2) && p == head && node) {
                auto name = normalize(node);
                p->canonical = new char[name.size() + 1];
                std::memcpy(p->canonical, name.c_str(), name.size() + 1);
            }
        }
    } catch (const std::bad_alloc &) {
        free_addresses(head);
        return 6;
    }
    *out = head;
    return 0;
}
int __cdecl host_name(char *name, uint32_t length) {
    auto value = "nova3pc-" + profile::identifier("hostname");
    if (!name)
        return bionic::fail(14);
    if (length <= value.size()) {
        if (length)
            std::memcpy(name, value.data(), length);
        return bionic::fail(36);
    }
    std::memcpy(name, value.c_str(), value.size() + 1);
    return 0;
}
uint32_t __cdecl address_number(const char *text) {
    initialize();
    if (!text)
        return 0xffffffffu;
    // inet_addr is numeric conversion, never a resolver or DNS request.
    return ::inet_addr(text);
}
char *__cdecl address_text(uint32_t address) {
    thread_local char text[16];
    in_addr in{};
    in.s_addr = address;
    return InetNtopA(AF_INET, &in, text, sizeof text) ? text : nullptr;
}
int __cdecl address_pton(int family, const char *text, void *out) {
    if (family != 2 && family != 10)
        return bionic::fail(97);
    if (!text || !out)
        return bionic::fail(14);
    return InetPtonA(family == 2 ? AF_INET : AF_INET6, text, out);
}
const char *__cdecl address_ntop(int family, const void *bytes, char *out, uint32_t size) {
    if (family != 2 && family != 10) {
        bionic::fail(97);
        return nullptr;
    }
    if (!bytes || !out) {
        bionic::fail(14);
        return nullptr;
    }
    if (!InetNtopA(family == 2 ? AF_INET : AF_INET6, const_cast<void *>(bytes), out, size)) {
        bionic::fail(28);
        return nullptr;
    }
    return out;
}
const char *__cdecl gai_error(int e) {
    switch (e) {
    case 0:
        return "Success";
    case 3:
        return "Invalid flags";
    case 5:
        return "Address family not supported";
    case 6:
        return "Memory allocation failure";
    case 8:
        return "Name or service not known";
    case 9:
        return "Service not supported";
    case 10:
        return "Socket type not supported";
    default:
        return "Address lookup failed";
    }
}
} // namespace
void register_resolver(ImportRegistry &r) {
#define B(n, f) r.function(n, reinterpret_cast<const void *>(&f))
    B("gethostbyname", host_by_name);
    B("gethostbyaddr", host_by_address);
    B("getaddrinfo", addresses);
    B("freeaddrinfo", free_addresses);
    B("gethostname", host_name);
    B("inet_addr", address_number);
    B("inet_ntoa", address_text);
    B("inet_pton", address_pton);
    B("inet_ntop", address_ntop);
    B("__get_h_errno", h_error_pointer);
    B("__h_errno", h_error_pointer);
    B("gai_strerror", gai_error);
#undef B
}
} // namespace nova::net
