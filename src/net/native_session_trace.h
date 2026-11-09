#pragma once
#include "abi/guest_hook.h"
#include "net/native_save_trace.h"
#include <memory>

namespace nova::net {
// Forwarding diagnostics around original calls; enabled by --session-log or --net-log.
class NativeSessionTrace {
  public:
    NativeSessionTrace(const ElfImage &, bool enabled);
    ~NativeSessionTrace();
    NativeSessionTrace(const NativeSessionTrace &) = delete;
    NativeSessionTrace &operator=(const NativeSessionTrace &) = delete;

  private:
    static uint32_t __cdecl panel_update(uint32_t receiver) noexcept;
    static uint32_t __cdecl storage_response(uint32_t receiver, uint32_t dispatcher,
                                             uint32_t event) noexcept;
    static uint32_t __cdecl send_get(uint32_t receiver, uint32_t operation, uint32_t url,
                                     uint32_t parameters, uint32_t headers) noexcept;
    uint32_t panel_original_ = 0, response_original_ = 0, event_operation_ = 0, event_error_ = 0;
    uint32_t last_legacy_ = ~0u, last_federation_ = ~0u;
    uint32_t manager_cell_ = 0, federation_cell_ = 0, last_xp_ = ~0u;
    uint32_t send_get_original_ = 0;
    uint32_t application_instance_ = 0, coins_getter_ = 0, player_instance_ = 0;
    uint32_t last_coins_ = ~0u, last_kills_ = ~0u, last_deaths_ = ~0u;
    std::unique_ptr<abi::GuestHook> panel_, response_, send_get_;
    std::unique_ptr<NativeSaveTrace> saves_;
    static NativeSessionTrace *active_;
};
} // namespace nova::net
