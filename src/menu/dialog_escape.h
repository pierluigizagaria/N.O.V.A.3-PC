#pragma once
#include "abi/guest_hook.h"
#include <memory>

namespace nova::menu {
// Scoped native BACK handling, never a global Yes or key mapping override.
class DialogEscape {
  public:
    explicit DialogEscape(const ElfImage &image);
    ~DialogEscape();
    DialogEscape(const DialogEscape &)=delete;
    DialogEscape &operator=(const DialogEscape &)=delete;
  private:
    static uint32_t __cdecl cancel_button(uint32_t buttons,uint32_t index);
    static void __cdecl pause_event(uint32_t receiver,uint32_t event,uint32_t manager);
    static DialogEscape *instance_;
    uint32_t get_button_=0;
    std::unique_ptr<abi::GuestCall> menu_back_,confirm_back_;
    std::unique_ptr<abi::GuestHook> pause_event_;
};
}
