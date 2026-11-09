#pragma once
#include "abi/guest_hook.h"
#include <memory>

namespace nova::net {
// Diagnostic forwarding only. The caller admits the supported image identity.
// No plaintext, credentials, file body or key material is logged.
class NativeSaveTrace {
  public:
    explicit NativeSaveTrace(const ElfImage &);
    ~NativeSaveTrace();
    NativeSaveTrace(const NativeSaveTrace &) = delete;
    NativeSaveTrace &operator=(const NativeSaveTrace &) = delete;

  private:
    static uint32_t __cdecl save(uint32_t receiver, uint32_t name, uint32_t version,
                                  uint32_t stream) noexcept;
    static uint32_t __cdecl load(uint32_t receiver, uint32_t name, uint32_t version,
                                  uint32_t stream) noexcept;
    uint32_t save_original_ = 0, load_original_ = 0;
    std::unique_ptr<abi::GuestHook> save_, load_;
    static NativeSaveTrace *active_;
};
} // namespace nova::net
