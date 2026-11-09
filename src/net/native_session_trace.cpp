#include "net/native_session_trace.h"
#include "loader/sha256.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace nova::net {
namespace {
uint32_t word(uint32_t receiver, uint32_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void *>(receiver + offset), sizeof(value));
    return value;
}
} // namespace
NativeSessionTrace *NativeSessionTrace::active_ = nullptr;
NativeSessionTrace::NativeSessionTrace(const ElfImage &image, bool enabled) {
    if (!enabled)
        return;
    if (active_)
        throw std::runtime_error("Native session trace already installed");
    if (sha256(image.source_bytes()) !=
        "a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e")
        throw std::runtime_error("Native session trace requires the supported library");
    event_operation_ = abi::require_function(image, "_ZN11GLonlineLib12APIBaseEvent9GetOpCodeEv");
    event_error_ = abi::require_function(image, "_ZN11GLonlineLib12APIBaseEvent12GetErrorCodeEv");
    application_instance_ = abi::require_function(image, "_ZN11Application11GetInstanceEv");
    coins_getter_ = abi::require_function(image, "_ZN11Application13GetCoinsCountEv");
    player_instance_ = abi::require_function(image, "_ZN14XPlayerManager8InstanceEv");
    manager_cell_ = word(image.address(0x113db80, 4), 0);
    federation_cell_ = word(image.address(0x113ddec, 4), 0);
    active_ = this;
    try {
        panel_ = std::make_unique<abi::GuestHook>(
            image, "_ZN19GS_OnlineLoginPanel6UpdateEv",
            std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x57, 0x56, 0x53},
            reinterpret_cast<uint32_t>(&panel_update));
        panel_original_ = panel_->original();
        response_ = std::make_unique<abi::GuestHook>(
            image, "_ZN7CSeshat14HandleResponseERN9GLBaseLib15EventDispatcherERNS0_8GLXEventE",
            std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x83, 0xec, 0x38},
            reinterpret_cast<uint32_t>(&storage_response));
        response_original_ = response_->original();
        send_get_ = std::make_unique<abi::GuestHook>(
            image,
            "_ZN11GLonlineLib7APIBase9SendByGetENS_"
            "12APIBaseEvent6OpCodeERKSsRKSt3mapISsSsSt4lessISsESaISt4pairIS3_SsEEESD_",
            std::initializer_list<uint8_t>{0x55, 0x89, 0xe5, 0x81, 0xec, 0xa8, 0, 0, 0},
            reinterpret_cast<uint32_t>(&send_get));
        send_get_original_ = send_get_->original();
        saves_ = std::make_unique<NativeSaveTrace>(image);
    } catch (...) {
        active_ = nullptr;
        throw;
    }
}
NativeSessionTrace::~NativeSessionTrace() {
    saves_.reset();
    send_get_.reset();
    response_.reset();
    panel_.reset();
    if (active_ == this)
        active_ = nullptr;
}
uint32_t __cdecl NativeSessionTrace::send_get(uint32_t receiver, uint32_t operation, uint32_t url,
                                              uint32_t parameters, uint32_t headers) noexcept {
    try {
        auto &self = *active_;
        const auto before = word(receiver, 0x20);
        const auto result =
            abi::invoke(self.send_get_original_, {receiver, operation, url, parameters, headers});
        std::fprintf(stderr,
                     "native-session send-get op=%u state_before=%u state_after=%u accepted=%u\n",
                     operation, before, word(receiver, 0x20), result & 255);
        return result;
    } catch (...) {
        TerminateProcess(GetCurrentProcess(), 86);
        return 0;
    }
}
uint32_t __cdecl NativeSessionTrace::panel_update(uint32_t receiver) noexcept {
    try {
        auto &self = *active_;
        const auto result = abi::invoke(self.panel_original_, {receiver});
        const auto legacy = word(receiver, 0x34), federation = word(receiver, 0x30);
        const auto manager = word(self.manager_cell_, 0);
        if (manager && legacy >= 5) {
            // The original panel update has already acquired these owners.
            // Observe the original wallet getter and parsed ranking self fields;
            // never write progress or substitute values in the client.
            const auto app = abi::invoke(self.application_instance_, {});
            const auto coins = abi::invoke(self.coins_getter_, {app});
            const auto player = abi::invoke(self.player_instance_, {});
            const auto api = player ? word(player, 0) : 0;
            const auto board = api ? word(api, 4) : 0;
            const auto kills = board ? word(board, 0x80) : ~0u;
            const auto deaths = board ? word(board, 0x84) : ~0u;
            if (coins != self.last_coins_ || kills != self.last_kills_ || deaths != self.last_deaths_) {
                std::fprintf(stderr, "native-session restored coins=%u kills=%d deaths=%d legacy=%u federation=%u\n",
                             coins, static_cast<int32_t>(kills), static_cast<int32_t>(deaths), legacy, federation);
                self.last_coins_ = coins;
                self.last_kills_ = kills;
                self.last_deaths_ = deaths;
            }
        }
        if (manager && word(manager, 0xb034) != self.last_xp_) {
            self.last_xp_ = word(manager, 0xb034);
            std::fprintf(stderr, "native-session progress xp=%u rank=%u level=%u\n", self.last_xp_,
                         word(manager, 0xb038), word(manager, 0xb030));
        }
        if (legacy != self.last_legacy_ || federation != self.last_federation_) {
            std::fprintf(stderr, "native-session panel legacy=%u federation=%u\n", legacy,
                         federation);
            const auto federation_owner = word(self.federation_cell_, 0);
            const auto wrapper = federation_owner ? word(federation_owner, 0x2c) : 0;
            if (wrapper) {
                std::fprintf(stderr,
                             "native-session wrapper op=%u completed=%u initialized=%u failed=%u "
                             "pending=%u\n",
                             word(wrapper, 0x10), *reinterpret_cast<uint8_t *>(wrapper + 8),
                             *reinterpret_cast<uint8_t *>(wrapper + 9),
                             *reinterpret_cast<uint8_t *>(wrapper + 10),
                             word(wrapper, 0x3c) != word(wrapper, 0x4c));
            }
            self.last_legacy_ = legacy;
            self.last_federation_ = federation;
        }
        return result;
    } catch (...) {
        std::fputs("native session observer failed\n", stderr);
        TerminateProcess(GetCurrentProcess(), 86);
        return 0;
    }
}
uint32_t __cdecl NativeSessionTrace::storage_response(uint32_t receiver, uint32_t dispatcher,
                                                      uint32_t event) noexcept {
    try {
        auto &self = *active_;
        const auto operation = abi::invoke(self.event_operation_, {event});
        const auto error = abi::invoke(self.event_error_, {event});
        const auto result = abi::invoke(self.response_original_, {receiver, dispatcher, event});
        std::fprintf(stderr,
                     "native-session storage event_op=%u error=%u wrapper_op=%u completed=%u "
                     "initialized=%u failed=%u\n",
                     operation, error, word(receiver, 0x10),
                     *reinterpret_cast<uint8_t *>(receiver + 8),
                     *reinterpret_cast<uint8_t *>(receiver + 9),
                     *reinterpret_cast<uint8_t *>(receiver + 10));
        return result;
    } catch (...) {
        std::fputs("native storage observer failed\n", stderr);
        TerminateProcess(GetCurrentProcess(), 86);
        return 0;
    }
}
} // namespace nova::net
