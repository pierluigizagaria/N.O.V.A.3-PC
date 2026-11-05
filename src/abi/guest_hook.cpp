#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <cstring>
#include <stdexcept>
#include <windows.h>

namespace nova::abi {
uint32_t require_function(const ElfImage &image, const std::string &name) {
    auto address = image.symbol(name);
    if (!address || !image.executable(address))
        throw std::runtime_error("Missing executable guest symbol: " + name);
    return address;
}
uint32_t invoke(uint32_t function, std::initializer_list<uint32_t> args) {
    return call_guest(function, args.begin(), static_cast<uint32_t>(args.size()));
}
bool invoke_bool(uint32_t function, std::initializer_list<uint32_t> args) {
    // SysV i386 C++ bool returns define AL only, not the upper bytes of EAX.
    return (invoke(function,args) & 0xff) != 0;
}
namespace {
void jump(uint8_t *at, uint32_t destination) {
    at[0] = 0xe9;
    uint32_t relative = destination - (reinterpret_cast<uint32_t>(at) + 5);
    std::memcpy(at + 1, &relative, 4);
}
bool safe_prologue(const std::vector<uint8_t> &code) {
    size_t n = 0;
    while (n < code.size()) {
        auto b = code[n];
        if (b >= 0x50 && b <= 0x57) { ++n; continue; }
        if (b >= 0xb8 && b <= 0xbf && n + 5 <= code.size()) { n += 5; continue; }
        if (b == 0x90) { ++n; continue; }
        // GCC's i386 realigning entry retains the caller's argument address.
        // This exact stack-relative LEA is position independent; no other LEA
        // form is accepted. Its following AND is checked independently below.
        if (n + 4 <= code.size() && b == 0x8d && code[n+1] == 0x4c &&
            code[n+2] == 0x24 && code[n+3] == 0x04) { n += 4; continue; }
        if (n + 2 <= code.size() &&
            ((b == 0x89 && code[n+1] == 0xe5) ||
             (b == 0x8b && code[n+1] == 0xec) ||
             (b == 0x31 && (code[n+1] == 0xc0 || code[n+1] == 0xd2)))) { n += 2; continue; }
        // Exact first-argument load after GCC's checked frame setup. No other
        // MOV, displacement or addressing mode is relocatable by this decoder.
        if (n == 3 && code[0] == 0x55 && code[1] == 0x89 && code[2] == 0xe5 &&
            n + 3 <= code.size() && b == 0x8b &&
            ((code[n+1] == 0x45 && code[n+2] == 0x08) ||
             (code[n+1] == 0x55 && code[n+2] == 0x0c))) {
            n += 3; continue;
        }
        if (n + 3 <= code.size() && b == 0x83 &&
            (code[n+1] == 0xe4 || code[n+1] == 0xec)) { n += 3; continue; }
        if (n + 6 <= code.size() && b == 0x81 && code[n+1] == 0xec) {
            n += 6; continue;
        }
        return false;
    }
    return n >= 5;
}
void write_code(uint32_t address, const void *bytes, size_t size) {
    auto p = reinterpret_cast<void *>(address);
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(p, size, PAGE_EXECUTE_READWRITE, &old))
        throw std::runtime_error("Guest hook VirtualProtect failed");
    std::memcpy(p, bytes, size);
    if (!VirtualProtect(p, size, old, &ignored) ||
        !FlushInstructionCache(GetCurrentProcess(), p, size))
        throw std::runtime_error("Guest hook protection/cache restore failed");
}
}
GuestHook::GuestHook(const ElfImage &image, const std::string &name,
                     std::initializer_list<uint8_t> expected, uint32_t replacement)
    : saved_(expected) {
    auto target = require_function(image, name);
    if (!replacement || !safe_prologue(saved_) ||
        !image.executable(target + static_cast<uint32_t>(saved_.size()) - 1) ||
        std::memcmp(reinterpret_cast<void *>(target), saved_.data(), saved_.size()))
        throw std::runtime_error("Guest hook prologue mismatch/unsupported: " + name);
    auto buffer = static_cast<uint8_t *>(VirtualAlloc(nullptr, saved_.size() + 5,
                                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!buffer) throw std::runtime_error("Guest hook trampoline allocation failed");
    trampoline_ = reinterpret_cast<uint32_t>(buffer);
    try {
        std::memcpy(buffer, saved_.data(), saved_.size());
        jump(buffer + saved_.size(), target + static_cast<uint32_t>(saved_.size()));
        DWORD old = 0;
        if (!VirtualProtect(buffer, saved_.size() + 5, PAGE_EXECUTE_READ, &old) ||
            !FlushInstructionCache(GetCurrentProcess(), buffer, saved_.size() + 5))
            throw std::runtime_error("Guest hook trampoline protection failed");
        auto patch = saved_;
        std::memset(patch.data(), 0x90, patch.size());
        patch[0] = 0xe9; // The displacement is relative to the guest target address.
        uint32_t relative = replacement - (target + 5);
        std::memcpy(patch.data() + 1, &relative, 4);
        write_code(target, patch.data(), patch.size());
        target_ = target;
    } catch (...) {
        VirtualFree(buffer, 0, MEM_RELEASE);
        trampoline_ = 0;
        throw;
    }
}
GuestHook::~GuestHook() {
    if (target_) {
        try { write_code(target_, saved_.data(), saved_.size()); }
        catch (...) { std::terminate(); }
    }
    if (trampoline_) VirtualFree(reinterpret_cast<void *>(trampoline_), 0, MEM_RELEASE);
}
GuestCall::GuestCall(const ElfImage &image, const std::string &owner, uint32_t offset,
                     const std::string &callee, uint32_t replacement) {
    const auto function = require_function(image,owner);
    original_ = require_function(image,callee);
    if (!replacement || function > UINT32_MAX-4 || offset > UINT32_MAX-function-4)
        throw std::runtime_error("Invalid guest call-site offset: " + owner);
    const auto site = function+offset;
    if (!image.executable(site) || !image.executable(site+4))
        throw std::runtime_error("Guest call-site is not executable: " + owner);
    std::memcpy(saved_,reinterpret_cast<void *>(site),sizeof(saved_));
    uint32_t displacement;
    std::memcpy(&displacement,saved_+1,4);
    if (saved_[0] != 0xe8 || site+5+displacement != original_)
        throw std::runtime_error("Guest direct call mismatch: " + owner);
    uint8_t patch[5]{0xe8};
    displacement = replacement-(site+5);
    std::memcpy(patch+1,&displacement,4);
    write_code(site,patch,sizeof(patch));
    target_ = site;
}
GuestCall::~GuestCall() {
    if (target_) {
        try { write_code(target_,saved_,sizeof(saved_)); }
        catch (...) { std::terminate(); }
    }
}
}
