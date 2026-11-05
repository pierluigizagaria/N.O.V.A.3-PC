#include "platform/native_device_profile.h"
#include "abi/guest_hook.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace nova::platform {
namespace {
std::mutex installation;
constexpr auto owner="_ZN13DeviceOptions11LoadProfileEv";
constexpr std::array<uint8_t,6> original{0x0f,0x84,0x37,0x03,0x00,0x00};
// Keep the conditional instruction but make its1920 destination equal to the
// fallthrough. Only the legacy resolution-specific CPU1/MEM1 detour is omitted.
constexpr std::array<uint8_t,6> independent{0x0f,0x84,0x00,0x00,0x00,0x00};
constexpr std::array<uint8_t,32> image_digest{
    0xa6,0x72,0xcf,0x14,0xe2,0x7f,0x1c,0x18,0x8e,0xdd,0x97,0xd8,0x5d,0x22,0x0b,0xec,
    0xd9,0x9e,0x64,0x1a,0xd9,0xcb,0x55,0x29,0x87,0x51,0x2d,0x5e,0xf9,0x0e,0x10,0x2e};
constexpr std::array<uint8_t,32> function_digest{
    0x5b,0x88,0xc3,0x45,0x66,0xe1,0x31,0x49,0x1d,0x0d,0x78,0x3a,0x26,0xf9,0x33,0x02,
    0xf9,0x36,0x4b,0x4b,0x9e,0x99,0xd9,0x69,0x6d,0xbd,0x3a,0xf4,0x99,0xa7,0xcd,0x86};
struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    std::vector<uint8_t> object;
    Sha256() {
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("Native profile SHA256 provider failed");
        ULONG bytes=0,size=0;
        if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof size,&bytes,0)<0) {
            BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr;
            throw std::runtime_error("Native profile SHA256 object failed");
        }
        try { object.resize(size); }
        catch(...) { BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr; throw; }
        if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0) {
            BCryptCloseAlgorithmProvider(algorithm,0); algorithm=nullptr;
            throw std::runtime_error("Native profile SHA256 hash failed");
        }
    }
    ~Sha256() {
        if(hash) BCryptDestroyHash(hash);
        if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    }
    void add(const uint8_t *bytes,ULONG count) {
        if(!count) return;
        if(BCryptHashData(hash,const_cast<PUCHAR>(bytes),count,0)<0)
            throw std::runtime_error("Native profile SHA256 input failed");
    }
    std::array<uint8_t,32> finish() {
        std::array<uint8_t,32> out{};
        if(BCryptFinishHash(hash,out.data(),static_cast<ULONG>(out.size()),0)<0)
            throw std::runtime_error("Native profile SHA256 finish failed");
        return out;
    }
};
void authenticate(const std::filesystem::path &path) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot authenticate native profile source image");
    Sha256 hash;
    std::array<uint8_t,65536> bytes{};
    while(input) {
        input.read(reinterpret_cast<char *>(bytes.data()),bytes.size());
        hash.add(bytes.data(),static_cast<ULONG>(input.gcount()));
    }
    if(!input.eof() || hash.finish()!=image_digest)
        throw std::runtime_error("Native profile source image identity mismatch");
}
void replace(uint32_t target,const std::array<uint8_t,6> &expected,
             const std::array<uint8_t,6> &desired) {
    auto *at=reinterpret_cast<uint8_t *>(target);
    if(std::memcmp(at,expected.data(),expected.size()))
        throw std::runtime_error("Native profile branch ownership mismatch");
    DWORD protection=0,ignored=0;
    if(!VirtualProtect(at,desired.size(),PAGE_EXECUTE_READWRITE,&protection))
        throw std::runtime_error("Native profile code protection failed");
    std::memcpy(at,desired.data(),desired.size());
    const bool protected_ok=VirtualProtect(at,desired.size(),protection,&ignored)!=FALSE;
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,desired.size())!=FALSE;
    if(protected_ok && flushed) return;
    // A failed install/restore cannot leave an unowned half-completed policy.
    if(!VirtualProtect(at,expected.size(),PAGE_EXECUTE_READWRITE,&ignored)) std::terminate();
    std::memcpy(at,expected.data(),expected.size());
    if(!VirtualProtect(at,expected.size(),protection,&ignored) ||
       !FlushInstructionCache(GetCurrentProcess(),at,expected.size())) std::terminate();
    throw std::runtime_error("Native profile protection/cache transaction failed");
}
}
NativeDeviceProfile::NativeDeviceProfile(const ElfImage &image,const std::filesystem::path &source) {
    std::lock_guard lock(installation);
    // Authenticate the source that actually produced this mapping. A separately
    // supplied accepted path cannot vouch for another ElfImage or a changed file.
    const auto bytes=image.source_bytes();
    Sha256 loaded;
    loaded.add(bytes.data(),static_cast<ULONG>(bytes.size())); // Loader caps input at512MiB.
    if(loaded.finish()!=image_digest)
        throw std::runtime_error("Native profile loaded image identity mismatch");
    // Retain the caller's source-path prerequisite, in addition to loaded identity.
    // Reopening this path is never sufficient to authenticate the mapped image.
    authenticate(source);
    const auto function=abi::require_function(image,owner);
    if(function!=image.address(0x6d2c20,925) || !image.executable(function+924))
        throw std::runtime_error("Native profile function mapping mismatch");
    Sha256 hash;
    hash.add(reinterpret_cast<const uint8_t *>(function),925);
    if(hash.finish()!=function_digest)
        throw std::runtime_error("Native profile925-byte function identity mismatch");
    replace(function+0x23,original,independent);
    target_=function+0x23;
}
NativeDeviceProfile::~NativeDeviceProfile() {
    std::lock_guard lock(installation);
    if(!target_) return;
    try { replace(target_,independent,original); }
    catch(...) { std::terminate(); }
}
} // namespace nova::platform
