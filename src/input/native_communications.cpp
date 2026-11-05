#include "input/native_communications.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace nova::input {
namespace {
uint32_t word(uint32_t address) {
    uint32_t value; std::memcpy(&value,reinterpret_cast<void *>(address),sizeof value); return value;
}
constexpr auto chat_pressed_name="_ZN13CButtonIGChat7PressedEv";
constexpr auto chat_draw_name="_ZN13CButtonIGChat4DrawEi";
thread_local bool hide_chat=false;
[[noreturn]] void boundary_failure(const char *boundary) {
    std::fprintf(stderr,"native-communications FAIL boundary=%s\n",boundary);
    std::fflush(stderr); TerminateProcess(GetCurrentProcess(),86); std::abort();
}
}
NativeCommunications *NativeCommunications::instance_=nullptr;
NativeCommunications::NativeCommunications(const ElfImage &image,Controls &input)
    : NativeCommunications(image,input,{
        abi::require_function(image,"_ZN6CLevel8GetLevelEv"),
        abi::require_function(image,"_ZN11Application11GetInstanceEv"),
        abi::require_function(image,"_ZN12gxStateStack12CurrentStateEv"),
        image.symbol("_ZTV9GS_InGame")+8,image.symbol("_ZTV16GS_MPInGameScore")+8,
        image.symbol("_ZTV13GS_InGameChat")+8,
        abi::require_function(image,"_ZN11gxGameState9GetParentEi")}) {}
NativeCommunications::NativeCommunications(const ElfImage &image,Controls &input,Bindings bindings)
    : input_(input),bindings_(bindings) {
    if(instance_) throw std::runtime_error("Only one native communications adapter is supported");
    if(!bindings.get_level || !bindings.application || !bindings.current_state || !bindings.get_parent || bindings.ingame_vtable<=8 || bindings.scores_vtable<=8 || bindings.chat_vtable<=8)
        throw std::runtime_error("Missing native communications bindings");
    chat_hook_=std::make_unique<abi::GuestHook>(image,chat_pressed_name,
        std::initializer_list<uint8_t>{0x55,0x31,0xc0,0x89,0xe5},
        reinterpret_cast<uint32_t>(&chat_pressed));
    chat_idle_paint_=std::make_unique<abi::GuestCall>(image,chat_draw_name,0x113,
        "_ZNK7CSprite10PaintFrameEiffjffh",reinterpret_cast<uint32_t>(&chat_paint));
    chat_active_paint_=std::make_unique<abi::GuestCall>(image,chat_draw_name,0x2c6,
        "_ZNK7CSprite10PaintFrameEiffjffh",reinterpret_cast<uint32_t>(&chat_paint));
    chat_draw_hook_=std::make_unique<abi::GuestHook>(image,chat_draw_name,
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&chat_draw));
    button_draw_hook_=std::make_unique<abi::GuestHook>(image,"_ZN10CButtonSpr4DrawEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&button_draw));
    instance_=this;
}
NativeCommunications::~NativeCommunications() {
    button_draw_hook_.reset(); chat_draw_hook_.reset(); chat_active_paint_.reset();
    chat_idle_paint_.reset(); chat_hook_.reset(); instance_=nullptr;
}
uint32_t NativeCommunications::current_state() const {
    const auto app=abi::invoke(bindings_.application);
    return app ? abi::invoke(bindings_.current_state,{app+0x18738}) : 0;
}
bool NativeCommunications::is_panel(uint32_t state) const {
    return state && (word(state)==bindings_.scores_vtable || word(state)==bindings_.chat_vtable);
}
bool NativeCommunications::gameplay_state(uint32_t state) const {
    if(!state) return false;
    if(word(state)==bindings_.ingame_vtable) return true;
    if(!is_panel(state)) return false;
    const auto parent=abi::invoke(bindings_.get_parent,{state,0});
    return parent && word(parent)==bindings_.ingame_vtable;
}
bool NativeCommunications::is_resume(uint32_t button) const {
    const auto state=current_state();
    return button && state && word(state)==bindings_.scores_vtable && word(state+0x38)==button;
}
void NativeCommunications::begin_frame() {
    end_frame();
    epoch_=input_.edge_epoch();
    chat_=input_.take_pressed(Action::chat);
    scores_=input_.take_pressed(Action::scores);
    if(chat_ || scores_) level_=abi::invoke(bindings_.get_level);
}
bool NativeCommunications::press(uint32_t button,uint32_t offset,bool &pending) {
    if(!pending || !button || !level_) return false;
    if(!input_.active() || input_.edge_epoch()!=epoch_) { end_frame(); return false; }
    const auto level=abi::invoke(bindings_.get_level);
    if(level!=level_) { end_frame(); return false; }
    if(word(level+offset)!=button) return false;
    const auto app=abi::invoke(bindings_.application);
    const auto state=app ? abi::invoke(bindings_.current_state,{app+0x18738}) : 0;
    if(!state || word(state)!=bindings_.ingame_vtable ||
       !*reinterpret_cast<const uint8_t *>(button+0x1d)) { end_frame(); return false; }
    // Native code polls scores before chat. If both arrive, open only the first.
    end_frame();
    return true;
}
bool NativeCommunications::press_score(uint32_t button) noexcept {
    try { return input_.panel_held(Action::scores) && press(button,0x2c8,scores_); }
    catch(...) {
        std::fputs("native-communications FAIL boundary=score_pressed\n",stderr);
        std::fflush(stderr); TerminateProcess(GetCurrentProcess(),86); std::abort();
    }
}
std::optional<uint32_t> NativeCommunications::panel_close(uint32_t button) noexcept {
    try {
        const auto state=current_state();
        if(!button || !state) return {};
        Action action;
        if(word(state)==bindings_.scores_vtable && word(state+0x38)==button) action=Action::scores;
        else if(word(state)==bindings_.chat_vtable &&
                (word(state+0x68)==button || word(state+0x6c)==button)) action=Action::chat;
        else return {};
        // Original panel Update owns its close animation and PopState. Voice
        // selections are unrelated receivers and retain native send behavior.
        return !input_.panel_held(action) && *reinterpret_cast<const uint8_t *>(button+0x1d) ? 1u : 0u;
    } catch(...) { boundary_failure("panel_close"); }
}
void __cdecl NativeCommunications::chat_paint(uint32_t sprite,uint32_t frame,uint32_t x,uint32_t y,
                                             uint32_t flags,uint32_t sx,uint32_t sy,uint32_t alpha) noexcept {
    try {
        if(hide_chat) return;
        abi::invoke(instance_->chat_idle_paint_->original(),{sprite,frame,x,y,flags,sx,sy,alpha});
    } catch(...) { boundary_failure("chat_paint"); }
}
void __cdecl NativeCommunications::chat_draw(uint32_t button,uint32_t alpha) noexcept {
    try {
        auto &self=*instance_;const auto level=abi::invoke(self.bindings_.get_level);
        const bool hidden=level && button && word(level+0x2cc)==button;
        struct Scope {
            bool previous;uint32_t *text=nullptr,saved=0;
            Scope(bool hidden,uint32_t button):previous(hide_chat) {
                hide_chat=hidden;
                if(hidden) { text=reinterpret_cast<uint32_t *>(button+0x88);saved=*text;*text=0; }
            }
            ~Scope() { if(text) *text=saved;hide_chat=previous; }
        } scope(hidden,button);
        // Run the native fade timers, disable/unregister and position updates.
        // Suppress only its two raster sites and optional label, then restore.
        abi::invoke(self.chat_draw_hook_->original(),{button,alpha});
    } catch(...) { boundary_failure("chat_draw"); }
}
void __cdecl NativeCommunications::button_draw(uint32_t button,uint32_t alpha) noexcept {
    try {
        auto &self=*instance_;
        if(self.is_resume(button)) return; // Whole Resume artwork, including text.
        abi::invoke(self.button_draw_hook_->original(),{button,alpha});
    } catch(...) { boundary_failure("button_draw"); }
}
uint32_t __cdecl NativeCommunications::chat_pressed(uint32_t button) noexcept {
    try {
        auto &self=*instance_;
        const auto original=abi::invoke(self.chat_hook_->original(),{button});
        const bool keyboard=self.input_.panel_held(Action::chat) && self.press(button,0x2cc,self.chat_);
        return keyboard ? 1u : original;
    } catch(...) {
        std::fputs("native-communications FAIL boundary=chat_pressed\n",stderr);
        std::fflush(stderr); TerminateProcess(GetCurrentProcess(),86); std::abort();
    }
}
}
