#include "menu/dialog_escape.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace nova::menu {
DialogEscape *DialogEscape::instance_=nullptr;
DialogEscape::DialogEscape(const ElfImage &image) {
    if(instance_) throw std::runtime_error("Dialog Escape adapter already installed");
    get_button_=abi::require_function(image,"_ZN12CMenuButtons9GetButtonEi");
    const auto no=abi::require_function(image,"_ZN11CMenuScreen12PushNoButtonEv");
    // CMenuManager already checked key event/type/press and the active screen.
    // Only screen 0x1fc8's BACK branch selected Yes; use its native No lookup.
    menu_back_=std::make_unique<abi::GuestCall>(image,
        "_ZN12CMenuManager7onEventEPK6IEventPK12EventManager",0x668,
        "_ZN11CMenuScreen13PushYesButtonEv",no);
    // GS_Confirm's BACK branch selected button1 for exit (mode2), unlike restart
    // modes0/1. Button0 is the native cancel action for all three modes.
    confirm_back_=std::make_unique<abi::GuestCall>(image,
        "_ZN10GS_Confirm7onEventEPK6IEventPK12EventManager",0xc0,
        "_ZN12CMenuButtons9GetButtonEi",reinterpret_cast<uint32_t>(&cancel_button));
    pause_event_=std::make_unique<abi::GuestHook>(image,
        "_ZN13GS_InGameMenu7onEventEPK6IEventPK12EventManager",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},
        reinterpret_cast<uint32_t>(&pause_event));
    instance_=this;
}
DialogEscape::~DialogEscape() {
    pause_event_.reset();
    confirm_back_.reset();
    menu_back_.reset();
    instance_=nullptr;
}
uint32_t __cdecl DialogEscape::cancel_button(uint32_t buttons,uint32_t) {
    // Re-enter through the bridge: Windows cdecl does not guarantee guest alignment.
    return abi::invoke(instance_->get_button_,{buttons,0});
}
void __cdecl DialogEscape::pause_event(uint32_t receiver,uint32_t event,uint32_t manager) {
    const auto *bytes=reinterpret_cast<const uint8_t *>(event);
    uint32_t type=0,key=0;
    std::memcpy(&type,bytes+4,4);
    if(type==1) {
        std::memcpy(&key,bytes+8,4);
        if(key==0 && bytes[12]) {
            // Android BACK release becomes native key0/pressed. Only this pause
            // consumer sees key1 (menu toggle/resume); keep the shared event intact.
            std::array<uint32_t,4> local{};
            std::memcpy(local.data(),bytes,sizeof local);
            local[2]=1;
            abi::invoke(instance_->pause_event_->original(),
                {receiver,reinterpret_cast<uint32_t>(local.data()),manager});
            return;
        }
    }
    abi::invoke(instance_->pause_event_->original(),{receiver,event,manager});
}
}
