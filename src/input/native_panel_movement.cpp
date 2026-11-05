#include "input/native_panel_movement.h"
#include <intrin.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace nova::input {
namespace {
uint32_t word(uint32_t p) { uint32_t v;std::memcpy(&v,reinterpret_cast<void *>(p),4);return v; }
bool raw_enabled(uint32_t p) { return p && *reinterpret_cast<uint8_t *>(p+0x1d); }
constexpr auto push_name="_ZN12gxStateStack9PushStateEP11gxGameState";
constexpr auto enable_name="_ZN21CLevelInterfaceNormal14EnableControlsEb";
constexpr auto freeze_name="_ZN15PlayerComponent15MpFreezeControlEb";
[[noreturn]] void failure() {
    std::fputs("native-panel-movement FAIL\n",stderr);std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(),86);std::abort();
}
}
NativePanelMovement *NativePanelMovement::instance_=nullptr;
NativePanelMovement::NativePanelMovement(const ElfImage &image,Controls &input)
    : NativePanelMovement(image,input,{
        abi::require_function(image,"_ZN6CLevel8GetLevelEv"),
        abi::require_function(image,"_ZN6CLevel18GetPlayerComponentEv"),
        abi::require_function(image,"_ZN11Application11GetInstanceEv"),
        abi::require_function(image,"_ZN12gxStateStack12CurrentStateEv"),
        abi::require_function(image,"_ZN11gxGameState9GetParentEi"),
        image.symbol("_ZTV9GS_InGame")+8,image.symbol("_ZTV13GS_InGameChat")+8,
        image.symbol("_ZTV16GS_MPInGameScore")+8,image.symbol("_ZTV21CLevelInterfaceNormal")+8}) {}
NativePanelMovement::NativePanelMovement(const ElfImage &image,Controls &input,Bindings bindings)
    : input_(input),bindings_(bindings) {
    if(instance_) throw std::runtime_error("Only one panel movement adapter is supported");
    pause_return_=abi::require_function(image,"_ZN9GS_InGame5PauseEv")+0x5e;
    const uint8_t pause_call[]={0xff,0x52,0x10};
    if(std::memcmp(reinterpret_cast<void *>(pause_return_-3),pause_call,sizeof pause_call))
        throw std::runtime_error("Unexpected GS_InGame pause permission call");
    push_=std::make_unique<abi::GuestHook>(image,push_name,
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&push));
    enable_=std::make_unique<abi::GuestHook>(image,enable_name,
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&enable));
    freeze_=std::make_unique<abi::GuestHook>(image,freeze_name,
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x53},reinterpret_cast<uint32_t>(&freeze));
    instance_=this;
}
NativePanelMovement::~NativePanelMovement() { freeze_.reset();enable_.reset();push_.reset();instance_=nullptr; }
uint32_t NativePanelMovement::current() const {
    auto app=abi::invoke(bindings_.application);
    return app ? abi::invoke(bindings_.current,{app+0x18738}) : 0;
}
bool NativePanelMovement::identity(const Lease &l) const {
    if(!l.panel || !input_.active() || abi::invoke(bindings_.level)!=l.level) return false;
    // Check live owners before dereferencing any retained interface or scheme.
    return abi::invoke(bindings_.player,{l.level})==l.player &&
        word(l.level+0x178)==l.interface_ && word(l.level+0x17c)==l.interface_ &&
        word(l.interface_)==bindings_.normal_table && word(l.interface_+0xc)==l.scheme;
}
std::optional<bool> NativePanelMovement::permission(uint32_t button) const {
    if(!button || !identity(lease_) || current()!=lease_.panel ||
       abi::invoke(bindings_.parent,{lease_.panel,0})!=lease_.parent) return {};
    for(size_t i=0;i<lease_.buttons.size();++i)
        if(button==lease_.buttons[i]) return lease_.enabled[i];
    return {};
}
uint32_t __cdecl NativePanelMovement::push(uint32_t stack,uint32_t state) noexcept {
    try {
        auto &s=*instance_;s.lease_={};s.entering_={};
        const auto app=abi::invoke(s.bindings_.application),old=s.current();
        const auto level=abi::invoke(s.bindings_.level);
        const bool panel=state && (word(state)==s.bindings_.chat_table || word(state)==s.bindings_.scores_table);
        if(panel && app && stack==app+0x18738 && old && word(old)==s.bindings_.ingame_table &&
           level && s.input_.active()) {
            auto normal=word(level+0x17c),player=abi::invoke(s.bindings_.player,{level});
            if(normal && player && word(level+0x178)==normal && word(normal)==s.bindings_.normal_table) {
                auto scheme=word(normal+0xc);
                if(scheme) {
                    s.entering_={state,old,level,player,normal,scheme};
                    constexpr uint32_t slots[]={0x28,0x38,0x40}; // move, jump, sprint
                    for(size_t i=0;i<3;++i) {
                        auto button=abi::invoke(word(word(scheme)+slots[i]),{scheme});
                        s.entering_.buttons[i]=button;s.entering_.enabled[i]=raw_enabled(button);
                    }
                }
            }
        }
        const auto result=abi::invoke(s.push_->original(),{stack,state});
        if(s.identity(s.entering_) && s.current()==state &&
           abi::invoke(s.bindings_.parent,{state,0})==s.entering_.parent) s.lease_=s.entering_;
        s.entering_={};return result;
    } catch(...) { failure(); }
}
void __cdecl NativePanelMovement::enable(uint32_t interface_,uint32_t value) noexcept {
    const auto caller=reinterpret_cast<uint32_t>(_ReturnAddress());
    try {
        auto &s=*instance_;
        const bool owned=!value && caller==s.pause_return_ && interface_==s.entering_.interface_ && s.identity(s.entering_);
        if(!owned) {
            if(interface_==s.lease_.interface_) s.lease_={};
            if(interface_==s.entering_.interface_) s.entering_={};
        }
        abi::invoke(s.enable_->original(),{interface_,value});
    } catch(...) { failure(); }
}
void __cdecl NativePanelMovement::freeze(uint32_t player,uint32_t value) noexcept {
    try {
        auto &s=*instance_;
        // EnableControls and RestoreState reapply the MP player's existing
        // freeze flag. It is a gameplay restriction, never a panel exception.
        if(value) {
            if(player==s.lease_.player) s.lease_={};
            if(player==s.entering_.player) s.entering_={};
        }
        abi::invoke(s.freeze_->original(),{player,value});
    } catch(...) { failure(); }
}
}
