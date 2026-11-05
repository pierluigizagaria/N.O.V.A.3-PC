#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nova::net {
inline constexpr uint32_t siocgifconf = 0x8912;
inline constexpr uint32_t siocgifflags = 0x8913;
// Host-owned values only. No SDK structure or host pointer is copied to a guest.
struct InterfaceEntry {
    uint64_t id = 0; // Windows NET_LUID.Value, not transient IfIndex or a route rank.
    uint16_t flags = 0;
    std::vector<std::array<uint8_t, 4>> ipv4;
};
struct InterfaceSnapshot { std::vector<InterfaceEntry> entries; };
using InterfaceProvider = int (*)(InterfaceSnapshot &); // zero or positive Linux errno
int windows_interface_snapshot(InterfaceSnapshot &);
std::string interface_name(uint64_t id);
int interface_ioctl(uint32_t request, void *argument);

// Scoped per-thread dependency seam for synthetic tests. Default is the real
// read-only Windows provider; neither an environment switch nor a guest import.
class ScopedInterfaceProvider {
public:
    explicit ScopedInterfaceProvider(InterfaceProvider provider);
    ~ScopedInterfaceProvider();
    ScopedInterfaceProvider(const ScopedInterfaceProvider &) = delete;
    ScopedInterfaceProvider &operator=(const ScopedInterfaceProvider &) = delete;
private:
    InterfaceProvider previous_;
};
} // namespace nova::net
