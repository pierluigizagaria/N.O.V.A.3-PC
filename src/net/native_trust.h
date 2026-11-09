#pragma once
#include "abi/guest_hook.h"
#include <filesystem>
#include <memory>
#include <string>

namespace nova::net {
// Adds one explicit development CA to the original OpenSSL stores. Native TLS,
// requests, parsers and asynchronous callbacks remain owned by the game.
class NativeTrust {
  public:
    NativeTrust(const ElfImage &, const std::filesystem::path &certificate);
    ~NativeTrust();
    NativeTrust(const NativeTrust &) = delete;
    NativeTrust &operator=(const NativeTrust &) = delete;

  private:
    static uint32_t __cdecl create_store() noexcept;
    static uint32_t __cdecl attach_socket(uint32_t ssl, uint32_t descriptor) noexcept;
    std::string certificate_;
    uint32_t original_ = 0;
    uint32_t bio_new_ = 0, bio_free_ = 0, pem_read_ = 0, add_cert_ = 0, free_cert_ = 0;
    uint32_t attach_original_ = 0, verify_ = 0;
    std::unique_ptr<abi::GuestHook> hook_;
    std::unique_ptr<abi::GuestHook> attach_hook_;
    static NativeTrust *active_;
};
} // namespace nova::net
