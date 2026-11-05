#include "menu/dialog_escape.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace {
constexpr auto menu="_ZN12CMenuManager7onEventEPK6IEventPK12EventManager";
constexpr auto confirm="_ZN10GS_Confirm7onEventEPK6IEventPK12EventManager";
constexpr auto pause="_ZN13GS_InGameMenu7onEventEPK6IEventPK12EventManager";
constexpr auto yes="_ZN11CMenuScreen13PushYesButtonEv";
constexpr auto no="_ZN11CMenuScreen12PushNoButtonEv";
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
template<class T> uint32_t ptr(T *p) { return reinterpret_cast<uint32_t>(p); }
uint32_t &word(void *p,size_t offset) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p)+offset); }
unsigned yes_calls=0,no_calls=0,back_calls=0,reset_calls=0;
uint32_t captured_screen=0;
void __cdecl capture_yes(uint32_t screen) { ++yes_calls; captured_screen=screen; }
void __cdecl capture_no(uint32_t screen) { ++no_calls; captured_screen=screen; }
void __cdecl capture_back(uint32_t screen) { ++back_calls; captured_screen=screen; }
void __cdecl reset(uint32_t) { ++reset_calls; }
void __cdecl quiet(uint32_t,uint32_t) {}
uint32_t pause_application=0;
uint32_t __cdecl application() { return pause_application; }
uint32_t __cdecl kind(uint32_t state,uint32_t requested) {
    return *reinterpret_cast<uint32_t *>(state+4)==requested;
}
uint32_t __cdecl selected(uint32_t) { return 1; }

void pause_consumer(const nova::ElfImage &image,bool fixed,bool real) {
    const auto function=nova::abi::require_function(image,pause);
    if(!real) {
        for(auto type:{1u,5u}) for(auto key:{0u,1u,4u,66u}) for(auto pressed:{0u,1u}) {
            std::array<uint32_t,4> event{0x1234,type,key,pressed},capture{};
            const auto original=event;
            nova::abi::invoke(function,{ptr(capture.data()),ptr(event.data()),0x4321});
            const auto expected=fixed && type==1 && key==0 && pressed ? 1u : key;
            check(capture==std::array<uint32_t,4>{type,expected,pressed,0x4321} && event==original,
                  "pause-only key remap preserves receiver, manager, flags and shared event; aligned re-entry");
        }
        return;
    }
    // Actual original CurrentState and event handler; only Application ownership
    // and button/state virtual methods use synthetic storage/terminal controls.
    std::array<uint32_t,0x18770/4> app{};
    pause_application=ptr(app.data());
    std::array<uint32_t,3> state_vtable{0,0,ptr(&kind)};
    std::array<uint32_t,2> state{ptr(state_vtable.data()),3};
    app[0x18738/4]=ptr(state.data());
    std::array<uint32_t,0x85/4+1> button_vtable{};button_vtable[0x84/4]=ptr(&selected);
    alignas(4) std::array<uint8_t,0x64> receiver{};
    alignas(4) std::array<uint8_t,0x24> resume{},main_menu{};
    word(main_menu.data(),0)=ptr(button_vtable.data());
    word(receiver.data(),0x5c)=ptr(resume.data());word(receiver.data(),0x60)=ptr(main_menu.data());
    std::array<std::unique_ptr<nova::abi::GuestCall>,4> app_calls;
    unsigned i=0;
    for(auto offset:{0x50u,0x92u,0xb8u,0x100u})
        app_calls[i++]=std::make_unique<nova::abi::GuestCall>(image,pause,offset,
            "_ZN11Application11GetInstanceEv",ptr(&application));
    for(auto current:{3u,14u}) for(auto type:{1u,5u})
        for(auto key:{0u,1u,4u,66u}) for(auto pressed:{0u,1u}) {
            state[1]=current;
            resume[0x1b]=resume[0x20]=main_menu[0x1b]=main_menu[0x20]=0;
            std::array<uint32_t,4> event{0,type,key,pressed};const auto original=event;
            nova::abi::invoke(function,{ptr(receiver.data()),ptr(event.data()),0});
            const bool active=current==3 && type==1 && pressed;
            const bool expect_resume=active && (key==1 || (key==0 && fixed));
            const bool expect_exit=active && key==0 && !fixed;
            check(resume[0x1b]==expect_resume && resume[0x20]==expect_resume &&
                  main_menu[0x1b]==expect_exit && main_menu[0x20]==expect_exit && event==original,
                  "original pause handler selects resume, preserves overlay gating and shared input");
        }
    pause_application=0;
}

void original_consumers(const nova::ElfImage &image,bool fixed) {
    const auto menu_fn=nova::abi::require_function(image,menu);
    const auto confirm_fn=nova::abi::require_function(image,confirm);
    // Synthetic storage and terminal UI observers; actual original event branches
    // and original GetButton execute. No initializers, application or network runs.
    alignas(4) std::array<uint8_t,0x110> manager{};
    alignas(4) std::array<uint8_t,0x60> screen{};
    std::array<uint32_t,1> stack{ptr(screen.data())};
    word(manager.data(),0x10)=ptr(stack.data());
    word(manager.data(),0x14)=ptr(stack.data()+1);
    manager[0xb8]=1; word(manager.data(),0xf8)=UINT32_MAX;
    word(screen.data(),0x40)=1; word(screen.data(),0x4c)=1;
    std::array<uint32_t,4> event{0,1,0,1}; // native BACK raised by AndroidOnKeyUp(4)
    for(auto screen_id:{0x1fc8u,0x13b6u,0x888u}) {
        word(screen.data(),8)=screen_id;
        yes_calls=no_calls=back_calls=0;
        nova::abi::invoke(menu_fn,{ptr(manager.data()),ptr(event.data()),0});
        check(captured_screen==ptr(screen.data()),"native menu retains actual screen receiver");
        check(screen_id==0x1fc8 ? (fixed ? no_calls==1 && yes_calls==0 : yes_calls==1 && no_calls==0)
              : screen_id==0x13b6 ? no_calls==1 && yes_calls==0 : back_calls==1 && yes_calls==0,
              "menu BACK chooses No; existing No and ordinary Back paths remain");
    }
    for(auto key:{4u,66u,67u}) {
        event[2]=key;yes_calls=no_calls=back_calls=0;
        nova::abi::invoke(menu_fn,{ptr(manager.data()),ptr(event.data()),0});
        check(!yes_calls && !no_calls && !back_calls,"unrelated/down menu keys remain untouched");
    }
    yes_calls=0;
    nova::abi::invoke(nova::abi::require_function(image,yes),{ptr(screen.data())});
    check(yes_calls==1,"explicit Yes retains its native destination");

    alignas(4) std::array<uint8_t,0x38> dialog{};
    alignas(4) std::array<uint8_t,0xb0> cancel{},accept{};
    std::array<uint32_t,8> vtable{};vtable[7]=ptr(&reset);
    word(accept.data(),0)=ptr(vtable.data());
    std::array<uint32_t,2> buttons{ptr(cancel.data()),ptr(accept.data())};
    std::array<uint32_t,4> collection{ptr(buttons.data()),ptr(buttons.data()+2),ptr(buttons.data()+2),0};
    word(dialog.data(),0x28)=ptr(collection.data());
    for(auto mode:{UINT32_MAX,0u,1u,2u,3u}) for(auto type:{1u,5u})
        for(auto key:{0u,4u,66u}) for(auto pressed:{0u,1u}) {
            cancel[0x1b]=cancel[0x20]=accept[0x1b]=accept[0x20]=0;
            word(dialog.data(),0x30)=mode;event={0,type,key,pressed};reset_calls=0;
            nova::abi::invoke(confirm_fn,{ptr(dialog.data()),ptr(event.data()),0});
            const bool eligible=type==1 && pressed && key==0 && mode<=2;
            const bool selects_yes=eligible && mode==2 && !fixed;
            check(cancel[0x1b]==(eligible && !selects_yes) && cancel[0x20]==cancel[0x1b] &&
                  accept[0x1b]==selects_yes && accept[0x20]==accept[0x1b],
                  "actual confirmation marks only native cancel for BACK, preserves other events");
            check(reset_calls==unsigned(type==1 && pressed),"native button reset path retained");
        }
}
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    const bool real=std::strcmp(argv[2],"--real")==0;
    if(real && !std::filesystem::exists(argv[1])) return 77;
    try {
        nova::ImportRegistry imports;
        if(real) { nova::bionic::register_boot(imports);nova::audio::register_opensl(imports);nova::gles::register_imports(imports); }
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1],imports);
        std::unique_ptr<nova::abi::GuestCall> logger;
        std::unique_ptr<nova::abi::GuestHook> yes_observer,no_observer,back_observer;
        if(real) {
            logger=std::make_unique<nova::abi::GuestCall>(image,"_ZN3glf7Console7PrintlnEPKcz",0x1f,
                "_ZN3glf7Console8VPrintlnEPKcPc",ptr(&quiet));
            yes_observer=std::make_unique<nova::abi::GuestHook>(image,yes,
                std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(&capture_yes));
            no_observer=std::make_unique<nova::abi::GuestHook>(image,no,
                std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(&capture_no));
            back_observer=std::make_unique<nova::abi::GuestHook>(image,"_ZN11CMenuScreen14PushBackButtonEv",
                std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},ptr(&capture_back));
        }
        const auto menu_fn=nova::abi::require_function(image,menu),confirm_fn=nova::abi::require_function(image,confirm);
        const auto pause_fn=nova::abi::require_function(image,pause);
        std::array<uint8_t,6> pause_before{};
        std::memcpy(pause_before.data(),reinterpret_cast<void *>(pause_fn),6);
        std::array<uint8_t,5> menu_before{},confirm_before{};
        std::memcpy(menu_before.data(),reinterpret_cast<void *>(menu_fn+0x668),5);
        std::memcpy(confirm_before.data(),reinterpret_cast<void *>(confirm_fn+0xc0),5);
        auto matrix=[&](bool fixed) {
            pause_consumer(image,fixed,real);
            if(real) original_consumers(image,fixed);
            else {
                check(nova::abi::invoke(menu_fn,{0})==unsigned(!fixed),"synthetic menu call destination/alignment");
                check(nova::abi::invoke(confirm_fn,{0})==unsigned(!fixed),"synthetic cancel index/aligned guest re-entry");
                check(nova::abi::invoke(nova::abi::require_function(image,yes),{0})==1,"explicit Yes preserved");
            }
        };
        matrix(false); // Reproduce both affirmative BACK paths before applying the fix.
        {
            nova::menu::DialogEscape policy(image);
            bool rejected=false;
            try { nova::menu::DialogEscape duplicate(image); } catch(const std::runtime_error &) { rejected=true; }
            check(rejected,"duplicate install rejected");matrix(true);
        }
        check(!std::memcmp(menu_before.data(),reinterpret_cast<void *>(menu_fn+0x668),5) &&
              !std::memcmp(confirm_before.data(),reinterpret_cast<void *>(confirm_fn+0xc0),5) &&
              !std::memcmp(pause_before.data(),reinterpret_cast<void *>(pause_fn),6),"exact call-site/hook restoration");
        matrix(false);
        {
            // A mismatched second call must roll back the first call and leave
            // somebody else's patch intact; failed construction publishes no owner.
            nova::abi::GuestCall foreign(image,confirm,0xc0,"_ZN12CMenuButtons9GetButtonEi",
                nova::abi::require_function(image,no));
            std::array<uint8_t,5> foreign_bytes{};
            std::memcpy(foreign_bytes.data(),reinterpret_cast<void *>(confirm_fn+0xc0),5);
            bool rejected=false;
            try { nova::menu::DialogEscape wrong(image); } catch(const std::runtime_error &) { rejected=true; }
            check(rejected && !std::memcmp(menu_before.data(),reinterpret_cast<void *>(menu_fn+0x668),5) &&
                  !std::memcmp(foreign_bytes.data(),reinterpret_cast<void *>(confirm_fn+0xc0),5),
                  "failed admission rolls back earlier call without overwriting foreign code");
        }
        {
            nova::abi::GuestHook foreign(image,pause,
                {0x55,0x89,0xe5,0x57,0x56,0x53},ptr(&quiet));
            std::array<uint8_t,6> foreign_bytes{};
            std::memcpy(foreign_bytes.data(),reinterpret_cast<void *>(pause_fn),6);
            bool rejected=false;
            try { nova::menu::DialogEscape wrong(image); } catch(const std::runtime_error &) { rejected=true; }
            check(rejected && !std::memcmp(menu_before.data(),reinterpret_cast<void *>(menu_fn+0x668),5) &&
                  !std::memcmp(confirm_before.data(),reinterpret_cast<void *>(confirm_fn+0xc0),5) &&
                  !std::memcmp(foreign_bytes.data(),reinterpret_cast<void *>(pause_fn),6),
                  "failed pause-hook admission rolls back both earlier calls without overwriting foreign code");
        }
        { nova::menu::DialogEscape retry(image);matrix(true); }
        std::puts("dialog/pause Escape: reproduction, cancel/resume, explicit Yes, unrelated events, alignment and restoration PASS");
        return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"dialog Escape: %s\n",e.what());return 1; }
}
