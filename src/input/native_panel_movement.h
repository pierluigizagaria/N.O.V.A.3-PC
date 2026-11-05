#pragma once
#include "abi/guest_hook.h"
#include "input/controls.h"
#include <array>
#include <memory>
#include <optional>

namespace nova::input {
// GS_InGame::Pause disables the touch scheme before a child panel is installed.
// Retain only host locomotion permissions across that specific transition.
class NativePanelMovement {
public:
    struct Bindings {
        uint32_t level,player,application,current,parent;
        uint32_t ingame_table,chat_table,scores_table,normal_table;
    };
    NativePanelMovement(const ElfImage &,Controls &);
    NativePanelMovement(const ElfImage &,Controls &,Bindings);
    ~NativePanelMovement();
    std::optional<bool> permission(uint32_t button) const;
private:
    struct Lease {
        uint32_t panel=0,parent=0,level=0,player=0,interface_=0,scheme=0;
        std::array<uint32_t,3> buttons{};
        std::array<bool,3> enabled{};
    };
    static NativePanelMovement *instance_;
    static uint32_t __cdecl push(uint32_t stack,uint32_t state) noexcept;
    static void __cdecl enable(uint32_t interface_,uint32_t value) noexcept;
    static void __cdecl freeze(uint32_t player,uint32_t value) noexcept;
    bool identity(const Lease &) const;
    uint32_t current() const;
    Controls &input_;
    Bindings bindings_;
    Lease lease_,entering_;
    uint32_t pause_return_=0;
    std::unique_ptr<abi::GuestHook> push_,enable_,freeze_;
};
}
