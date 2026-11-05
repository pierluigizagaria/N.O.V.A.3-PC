#include "net/interfaces.h"
#include "bionic/runtime.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>

namespace nova::net {
namespace {
#pragma pack(push, 4)
struct GuestIfconf { int32_t length; uint32_t buffer; };
struct GuestIfreq { char name[16]; uint8_t value[16]; };
#pragma pack(pop)
static_assert(sizeof(void *) == 4 && sizeof(GuestIfconf) == 8 &&
              offsetof(GuestIfconf, buffer) == 4 && sizeof(GuestIfreq) == 32 &&
              offsetof(GuestIfreq, value) == 16);
thread_local InterfaceProvider current_provider = windows_interface_snapshot;

bool accessible(const void *address, size_t size, bool write) {
    if (!size) return true;
    uintptr_t p = reinterpret_cast<uintptr_t>(address);
    if (!p || size > std::numeric_limits<uintptr_t>::max() - p) return false;
    const uintptr_t end = p + size;
    while (p < end) {
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(reinterpret_cast<const void *>(p), &page, sizeof page) ||
            page.State != MEM_COMMIT || (page.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = page.Protect & 0xff;
        const bool writable = access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
                              access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
        const bool readable = writable || access == PAGE_READONLY || access == PAGE_EXECUTE_READ;
        if (write ? !writable : !readable) return false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(page.BaseAddress);
        if (page.RegionSize > std::numeric_limits<uintptr_t>::max() - base) return false;
        const uintptr_t next = base + page.RegionSize;
        if (next <= p) return false;
        p = (std::min)(next, end);
    }
    return true;
}
bool copy_in(void *to, const void *from, size_t size) {
    SIZE_T copied = 0;
    return accessible(from, size, false) &&
           ReadProcessMemory(GetCurrentProcess(), from, to, size, &copied) && copied == size;
}
bool copy_out(void *to, const void *from, size_t size) {
    if (!size) return true;
    SIZE_T copied = 0;
    // WriteProcessMemory alone can have debugger-like page permissions. Check
    // ordinary guest writability first; protection races still return EFAULT.
    return accessible(to, size, true) &&
           WriteProcessMemory(GetCurrentProcess(), to, from, size, &copied) && copied == size;
}
int capture(InterfaceSnapshot &snapshot) {
    const int error = current_provider(snapshot);
    if (error) return error > 0 ? error : 5;
    std::set<uint64_t> ids;
    size_t count = 0;
    if (snapshot.entries.size() > 4096) return 105;
    for (const auto &entry : snapshot.entries) {
        if (!entry.id || !ids.insert(entry.id).second) return 5;
        if (entry.ipv4.size() > 65536 - count) return 105;
        count += entry.ipv4.size();
    }
    return 0;
}
int enumerate(void *argument) {
    GuestIfconf conf{};
    if (!copy_in(&conf, argument, sizeof conf) || !accessible(argument, sizeof conf, true))
        return bionic::fail(14);
    // Like count-only SIOCGIFCONF, a null buffer ignores its input length.
    if (conf.buffer && conf.length < 0) return bionic::fail(22);
    InterfaceSnapshot snapshot;
    int error = capture(snapshot);
    if (error) return bionic::fail(error);
    size_t count = 0;
    for (const auto &entry : snapshot.entries) count += entry.ipv4.size();
    const size_t records = conf.buffer ? (std::min)(count, static_cast<size_t>(conf.length) / 32) : count;
    std::vector<GuestIfreq> output;
    if (conf.buffer) {
        output.reserve(records);
        for (const auto &entry : snapshot.entries) {
            const auto name = interface_name(entry.id);
            for (const auto &ip : entry.ipv4) {
                if (output.size() == records) break;
                GuestIfreq record{};
                std::memcpy(record.name, name.data(), name.size());
                record.value[0] = 2; // little-endian AF_INET, port and padding stay zero
                std::memcpy(record.value + 4, ip.data(), ip.size());
                output.push_back(record);
            }
            if (output.size() == records) break;
        }
        if (!copy_out(reinterpret_cast<void *>(conf.buffer), output.data(), output.size() * 32))
            return bionic::fail(14);
    }
    conf.length = static_cast<int32_t>(records * 32);
    return copy_out(argument, &conf, sizeof conf) ? 0 : bionic::fail(14);
}
int flags(void *argument) {
    GuestIfreq request{};
    if (!copy_in(&request, argument, sizeof request) || !accessible(argument, sizeof request, true))
        return bionic::fail(14);
    // Linux's ioctl name is bounded to IFNAMSIZ-1, including for alias lookup.
    request.name[15] = '\0';
    std::string name(request.name);
    name.resize(name.find(':') == std::string::npos ? name.size() : name.find(':'));
    InterfaceSnapshot snapshot;
    int error = capture(snapshot);
    if (error) return bionic::fail(error);
    for (const auto &entry : snapshot.entries) {
        if (interface_name(entry.id) != name) continue;
        request.value[0] = static_cast<uint8_t>(entry.flags);
        request.value[1] = static_cast<uint8_t>(entry.flags >> 8);
        // ifr_flags is a short; preserve the other fourteen union bytes.
        return copy_out(argument, &request, sizeof request) ? 0 : bionic::fail(14);
    }
    return bionic::fail(19);
}
} // namespace

std::string interface_name(uint64_t id) {
    // Full 64-bit LUID encoded without collision: at most 13 base36 digits plus
    // 'n' and NUL fits IFNAMSIZ16. All addresses of one interface use its base
    // name; no guessed Android eth/wlan label or preferred endpoint is invented.
    static constexpr char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string digits;
    do { digits.push_back(alphabet[id % 36]); id /= 36; } while (id);
    std::reverse(digits.begin(), digits.end());
    return "n" + digits;
}
ScopedInterfaceProvider::ScopedInterfaceProvider(InterfaceProvider provider) : previous_(current_provider) {
    if (!provider) throw std::invalid_argument("null interface provider");
    current_provider = provider;
}
ScopedInterfaceProvider::~ScopedInterfaceProvider() { current_provider = previous_; }
int interface_ioctl(uint32_t request, void *argument) {
    if (request != siocgifconf && request != siocgifflags) return bionic::fail(25);
    if (!argument) return bionic::fail(14);
    try { return request == siocgifconf ? enumerate(argument) : flags(argument); }
    catch (const std::bad_alloc &) { return bionic::fail(12); }
    catch (...) { return bionic::fail(5); } // Never unwind through the guest's ELF frames.
}
} // namespace nova::net
