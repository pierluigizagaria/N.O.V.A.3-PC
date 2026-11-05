#include "input/native_communications.h"
#include "input/bindings.h"
#include "bionic/runtime.h"
#include "audio/opensl.h"
#include "gles/runtime.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <cstdio>
using namespace nova::input;
void panel_movement_real(const nova::ElfImage &image);
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
uint32_t ptr(auto *p) { return reinterpret_cast<uint32_t>(p); }
uint32_t level_address=0,application_address=0,state_address=0;
uint32_t __cdecl level() noexcept { return level_address; }
uint32_t __cdecl app() noexcept { return application_address; }
uint32_t __cdecl current(uint32_t) noexcept { return state_address; }
uint32_t &word(void *p,size_t offset) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p)+offset); }
NativeCommunications *active_adapter=nullptr;
uint32_t parent_address=0;unsigned pops=0;
uint32_t __cdecl parent(uint32_t,uint32_t) noexcept { return parent_address; }
uint32_t __cdecl no(uint32_t=0) noexcept { return 0; }
void __cdecl quiet(uint32_t) noexcept {}
uint32_t __cdecl kind(uint32_t,uint32_t requested) noexcept { return requested==0x4c; }
uint32_t __cdecl resume_query(uint32_t button) noexcept { return active_adapter->panel_close(button).value_or(0); }
void __cdecl pop(uint32_t) noexcept { ++pops; }
SDL_Event key(Uint32 type,SDL_Keycode code,bool repeat=false);
void original_score_update(const nova::ElfImage &image,NativeCommunications &adapter,Controls &input,
                           void *state,void *resume) {
    // Actual native Update through its virtual Resume query and PopState call.
    // Parent/UI/manager storage and terminal pop are isolated fixture controls.
    constexpr auto owner="_ZN16GS_MPInGameScore6UpdateEv";
    std::array<uint32_t,40> table{};table[8/4]=ptr(&kind);table[0x14/4]=ptr(&quiet);
    table[0x68/4]=ptr(&quiet);table[0x30/4]=ptr(&resume_query);
    std::array<uint32_t,1> parent_object{ptr(table.data())};parent_address=ptr(parent_object.data());
    std::array<uint32_t,0xb100/4> manager{};
    const auto singleton=image.symbol("_ZN13GameMpManager9SingletonE");
    check(singleton!=0,"original manager slot");
    auto &slot=*reinterpret_cast<uint32_t *>(singleton);const auto old_slot=slot;
    const auto old_state=word(state,0),old_resume=word(resume,0);
    // The adapter checks the real score vtable identity, whose isKindOf is safe
    // without game initialization; the child controls use the fixture table.
    word(state,0x34)=ptr(parent_object.data());word(resume,0)=ptr(table.data());
    slot=ptr(manager.data());active_adapter=&adapter;pops=0;
    struct Restore { uint32_t &slot;uint32_t previous;void *resume;uint32_t table;
        ~Restore(){slot=previous;word(resume,0)=table;active_adapter=nullptr;} } restore{slot,old_slot,resume,old_resume};
    std::array<std::unique_ptr<nova::abi::GuestCall>,6> calls;
    calls[0]=std::make_unique<nova::abi::GuestCall>(image,owner,0x22,"_ZN11gxGameState9GetParentEi",ptr(&parent));
    calls[1]=std::make_unique<nova::abi::GuestCall>(image,owner,0x2f,"_ZN11Application11GetInstanceEv",ptr(&app));
    calls[2]=std::make_unique<nova::abi::GuestCall>(image,owner,0xed,"_ZN13GameMpManager14IsTeamGameModeEv",ptr(&no));
    calls[3]=std::make_unique<nova::abi::GuestCall>(image,owner,0x159,"_ZN3glf3App11GetInstanceEv",ptr(&no));
    calls[4]=std::make_unique<nova::abi::GuestCall>(image,owner,0x1a3,"_ZN11Application11GetInstanceEv",ptr(&app));
    calls[5]=std::make_unique<nova::abi::GuestCall>(image,owner,0x1b0,"_ZN12gxStateStack8PopStateEv",ptr(&pop));
    const auto update=nova::abi::require_function(image,owner);
    nova::abi::invoke(update,{ptr(state)});
    check(pops==0 && word(state,0)==old_state,"original scoreboard stays open while held");
    input.handle(key(SDL_EVENT_KEY_UP,SDLK_TAB));
    nova::abi::invoke(update,{ptr(state)});
    check(pops==1,"original scoreboard reaches native PopState on held-key release");
}
void original_chat_update(const nova::ElfImage &image,NativeCommunications &adapter,Controls &input,
                          void *state,void *left,void *right) {
    constexpr auto owner="_ZN13GS_InGameChat6UpdateEv";
    std::array<uint32_t,40> table{};
    table[0x14/4]=table[0x68/4]=ptr(&quiet);table[0x30/4]=ptr(&resume_query);
    std::array<uint32_t,0xc0/4> tabs{};tabs[0]=ptr(table.data());
    tabs[0x48/4]=1;tabs[0x94/4]=0xffffffffu;
    std::array<uint32_t,1> parent_object{ptr(table.data())};parent_address=ptr(parent_object.data());
    const auto old_left=word(left,0),old_right=word(right,0);
    word(left,0)=word(right,0)=ptr(table.data());word(state,0x64)=ptr(tabs.data());
    word(state,0x60)=0x3f800000u; // Native UI scale 1.0, settled panel position.
    word(state,0x50)=word(state,0x54)=0;word(state,0x74)=word(state,0x78)=0;
    active_adapter=&adapter;pops=0;
    struct Restore {void *left,*right;uint32_t a,b;
        ~Restore(){word(left,0)=a;word(right,0)=b;active_adapter=nullptr;}} restore{left,right,old_left,old_right};
    std::vector<std::unique_ptr<nova::abi::GuestCall>> calls;
    const auto replace=[&](uint32_t offset,const char *callee,uint32_t target) {
        calls.push_back(std::make_unique<nova::abi::GuestCall>(image,owner,offset,callee,target));
    };
    replace(0x17,"_ZN3glf3App11GetInstanceEv",ptr(&no));
    for(auto offset:{0x50u,0x76u,0x2c8u}) replace(offset,"_ZN11Application11GetInstanceEv",ptr(&app));
    replace(0x69,"_ZN11gxGameState9GetParentEi",ptr(&parent));
    for(auto offset:{0x115u,0x187u,0x295u,0x502u,0xa1du}) replace(offset,"_ZN6CLevel8GetLevelEv",ptr(&level));
    replace(0xa33,"_ZN13CButtonIGChat17SetButtonLocationEi",ptr(&quiet));
    replace(0x2d5,"_ZN12gxStateStack8PopStateEv",ptr(&pop));
    const auto update=nova::abi::require_function(image,owner);
    nova::abi::invoke(update,{ptr(state)});
    check(pops==0 && static_cast<uint8_t *>(state)[0x75]==0,"native voice panel remains open while V held");
    input.handle(key(SDL_EVENT_KEY_UP,SDLK_V));
    nova::abi::invoke(update,{ptr(state)});
    check(pops==0 && static_cast<uint8_t *>(state)[0x75]==1,"V release starts original close animation without sending a voice line");
    // Settle the fixture's tab at the native animation's own destination.
    tabs[0x48/4]=word(state,0x78)&0xffffu;
    nova::abi::invoke(update,{ptr(state)});
    check(pops==1,"settled native voice animation reaches original PopState call");
}
SDL_Event key(Uint32 type,SDL_Keycode code,bool repeat) {
    SDL_Event e{};e.type=type;e.key.key=code;e.key.repeat=repeat;return e;
}
void bindings() {
    ControlsConfig config;
    check(config.keys.at(SDLK_V)==Action::chat && config.keys.at(SDLK_TAB)==Action::scores,"communication defaults");
    check(rebind(config,Action::chat,{BindingKind::key,SDLK_Y}).empty() &&
          rebind(config,Action::scores,{BindingKind::key,SDLK_F1}).empty(),"both commands remappable");
    check(!config.keys.contains(SDLK_V) && !config.keys.contains(SDLK_TAB),"old aliases removed");
    std::istringstream saved(serialize_bindings(config));
    check(same_bindings(ControlsConfig::parse(saved),config),"communication bindings persist");
    reset_bindings(config);
    check(config.keys.at(SDLK_V)==Action::chat && config.keys.at(SDLK_TAB)==Action::scores,"reset restores defaults");
    std::istringstream aliases("bind.TAB=chat\nbind.Y=scores\n");
    check(ControlsConfig::parse(aliases).keys.at(SDLK_TAB)==Action::chat,"readable Tab alias");
    Controls input(config);input.gameplay=true;
    input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W)); input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_V));
    check(input.take_pressed(Action::chat) && !input.take_pressed(Action::chat),"single command edge");
    check(input.take().down(Action::forward),"command drain preserves movement");
    input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_V,true));check(!input.take_pressed(Action::chat),"repeat suppressed");
    input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_TAB));
    check(!input.take().edge(Action::scores) && input.take_pressed(Action::scores),
          "command arriving after frame-start survives ordinary gameplay drain");
    input.handle(key(SDL_EVENT_KEY_UP,SDLK_V));input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_V));
    input.context(false,false);check(!input.take_pressed(Action::chat),"focus loss revokes request");
    check(!rebind(config,Action::scores,{BindingKind::wheel_up}).empty(),"scoreboard requires a held binding");
    check(!rebind(config,Action::chat,{BindingKind::wheel_up}).empty(),"voice lines require a held binding");
    ControlsConfig custom;check(rebind(custom,Action::scores,{BindingKind::mouse,SDL_BUTTON_X1}).empty(),"mouse score binding");
    input.replace_bindings(custom);input.context(true,false);input.gameplay=true;
    SDL_Event side{};side.type=SDL_EVENT_MOUSE_BUTTON_DOWN;side.button.button=SDL_BUTTON_X1;
    input.handle(side);check(input.panel_held(Action::scores),"physical mouse score hold");
    input.gameplay=false;input.clear();input.context(true,false);
    check(input.panel_held(Action::scores),"modal capture release retains physical hold");
    side.type=SDL_EVENT_MOUSE_BUTTON_UP;check(input.handle(side) && !input.panel_held(Action::scores),"modal physical release is consumed and closes hold");
    input.gameplay=true;side.type=SDL_EVENT_MOUSE_BUTTON_DOWN;input.handle(side);
    input.communication_panel(true);input.clear_except_movement();side.type=SDL_EVENT_MOUSE_BUTTON_UP;
    check(input.handle(side) && !input.panel_held(Action::scores),"movement-enabled panel consumes its mouse-bound release without stray touch");
    input.communication_panel(false);
    input.gameplay=true;side.type=SDL_EVENT_MOUSE_BUTTON_DOWN;input.handle(side);
    input.overlay(true);check(!input.panel_held(Action::scores),"Settings cancels held scoreboard");
    input.overlay(false);input.handle(side);input.replace_bindings(ControlsConfig{});
    check(!input.panel_held(Action::scores),"rebinding cancels old physical owner");
    input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_V));
    input.gameplay=false;input.clear();input.context(true,false);
    check(input.panel_held(Action::chat) && !input.panel_held(Action::scores),"voice physical hold survives modal capture independently");
    check(input.handle(key(SDL_EVENT_KEY_UP,SDLK_V)) && !input.panel_held(Action::chat),"voice release consumed in modal");
}
void installed(const nova::ElfImage &image,bool real) {
    constexpr auto name="_ZN13CButtonIGChat7PressedEv";
    const auto chat_query=nova::abi::require_function(image,name);
    std::array<uint8_t,5> before{};std::memcpy(before.data(),reinterpret_cast<void *>(chat_query),5);
    std::array<uint32_t,0x300/4> native_level{};
    std::array<uint32_t,0x18820/4> application{};
    std::array<uint32_t,0x134/4> state{};state[0]=0x12345678;
    std::array<uint8_t,0xb4> chat{},score{},other{},resume{};chat[0x1d]=score[0x1d]=other[0x1d]=resume[0x1d]=1;
    state[0x38/4]=ptr(resume.data());
    std::array<uint8_t,0xb4> close_left{},close_right{};close_left[0x1d]=close_right[0x1d]=1;
    state[0x68/4]=ptr(close_left.data());state[0x6c/4]=ptr(close_right.data());
    native_level[0x2cc/4]=ptr(chat.data());native_level[0x2c8/4]=ptr(score.data());
    level_address=ptr(native_level.data());application_address=ptr(application.data());state_address=ptr(state.data());
    application[0x18738/4]=state_address;
    Controls input(ControlsConfig{});input.gameplay=true;
    // Real lane executes the original CurrentState and original chat Pressed.
    const auto state_query=real ? nova::abi::require_function(image,"_ZN12gxStateStack12CurrentStateEv") : ptr(&current);
    const auto scores_vtable=real ? image.symbol("_ZTV16GS_MPInGameScore")+8 : 0x1234567cu;
    const auto chat_vtable=real ? image.symbol("_ZTV13GS_InGameChat")+8 : 0x12345680u;
    std::array<uint32_t,9> game_parent{};game_parent[0]=state[0];
    parent_address=ptr(game_parent.data());state[0x20/4]=parent_address;
    const auto parent_query=real ? nova::abi::require_function(image,"_ZN11gxGameState9GetParentEi") : ptr(&parent);
    const NativeCommunications::Bindings providers{ptr(&level),ptr(&app),state_query,state[0],scores_vtable,chat_vtable,parent_query};
    if(real) check(word(reinterpret_cast<void *>(image.symbol("_ZTV10CButtonSpr")+8),0x30)==
        nova::abi::require_function(image,"_ZN7CButton7PressedEv"),"native panel button virtual query uses installed CButton hook");
    {
        NativeCommunications adapter(image,input,providers);
        check(adapter.gameplay_state(state_address),"ordinary game state permits movement");
        for(auto panel:{scores_vtable,chat_vtable}) {
            state[0]=panel;check(adapter.gameplay_state(state_address),"communication panel with live game parent permits movement");
            game_parent[0]=0xabcdef00;check(!adapter.gameplay_state(state_address),"foreign parent rejects panel movement");
            game_parent[0]=providers.ingame_vtable;
            state[0x20/4]=parent_address=0;check(!adapter.gameplay_state(state_address),"missing game parent rejects panel movement");
            state[0x20/4]=parent_address=ptr(game_parent.data());
        }
        state[0]=0xabcdef00;check(!adapter.gameplay_state(state_address),"pause and unrelated screens remain excluded");
        state[0]=providers.ingame_vtable;
        const auto query=[&](auto &button) { return nova::abi::invoke_bool(chat_query,{ptr(button.data())}); };
        const auto offer=[&](SDL_Keycode code) {
            input.context(false,false);input.clear();input.gameplay=true;input.context(true,false);input.overlay(false);
            input.handle(key(SDL_EVENT_KEY_DOWN,code));adapter.begin_frame();
        };
        offer(SDLK_V);
        (void)input.take(); // Normal gameplay may drain its own actions first.
        check(!query(other) && query(chat) && !query(chat),"original chat query receives one matching edge");
        offer(SDLK_TAB);
        check(!adapter.press_score(ptr(other.data())) && adapter.press_score(ptr(score.data())) &&
              !adapter.press_score(ptr(score.data())),"score command reaches only the level-owned query once");
        offer(SDLK_V);score[0x1d]=0;chat[0x1d]=0;
        check(!query(chat),"disabled original chat stays disabled");chat[0x1d]=score[0x1d]=1;
        offer(SDLK_TAB);score[0x1d]=0;check(!adapter.press_score(ptr(score.data())),"disabled scores stays disabled");score[0x1d]=1;
        offer(SDLK_V);adapter.end_frame();check(!query(chat),"unpolled request expires at frame end");
        offer(SDLK_V);input.clear();check(!query(chat),"input epoch revokes pending command");
        offer(SDLK_V);input.context(false,false);check(!query(chat),"focus loss between offer and native poll");
        offer(SDLK_V);input.overlay(true);check(!query(chat),"Settings takeover revokes command");
        offer(SDLK_V);level_address=0;check(!query(chat),"level teardown revokes command");level_address=ptr(native_level.data());
        offer(SDLK_V);state[0]=0x87654321;check(!query(chat),"another native screen rejects stale gameplay edge");state[0]=providers.ingame_vtable;
        input.clear();input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_TAB));
        input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_V));adapter.begin_frame();
        check(adapter.press_score(ptr(score.data())) && !query(chat),"only one modal activation per frame");
        chat[0x20]=1;check(query(chat),"native touch edge remains authoritative without a keyboard request");chat[0x20]=0;
        input.gameplay=false;input.clear();adapter.end_frame();
        SDL_Event click{};click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;click.button.button=SDL_BUTTON_LEFT;
        SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.xrel=20;
        check(!input.active() && !input.handle(click) && !input.handle(motion),"modal releases capture and forwards clicks/motion to absolute input");
        check(!input.handle(key(SDL_EVENT_KEY_DOWN,SDLK_V)),"chat text is not a gameplay command");
        input.gameplay=true;input.clear();click.type=SDL_EVENT_MOUSE_BUTTON_UP;input.handle(click);
        check(input.active() && !input.take().requested(Action::fire),"dismissal recaptures without turning closing release into fire");
        check(!query(chat) && !adapter.press_score(ptr(score.data())),"modal dismissal does not reopen commands");
        offer(SDLK_TAB);input.handle(key(SDL_EVENT_KEY_UP,SDLK_TAB));
        check(!adapter.press_score(ptr(score.data())),"release before native query never opens a sticky scoreboard");
        offer(SDLK_TAB);check(adapter.press_score(ptr(score.data())),"held scoreboard admission");
        state[0]=scores_vtable;input.gameplay=false;input.clear();input.context(true,false);
        check(input.panel_held(Action::scores) && adapter.panel_close(ptr(resume.data()))==0 &&
              !adapter.panel_close(ptr(other.data())),"owned Resume stays closed and unrelated controls pass through");
        resume[0x20]=1;
        check(adapter.panel_close(ptr(resume.data()))==0,"invisible Resume hotspot cannot dismiss while held");
        if(real) original_score_update(image,adapter,input,state.data(),resume.data());
        else input.handle(key(SDL_EVENT_KEY_UP,SDLK_TAB));
        check(adapter.panel_close(ptr(resume.data()))==1,"release selects the original Resume query");
        resume[0x1d]=0;check(adapter.panel_close(ptr(resume.data()))==0,"disabled Resume remains native");resume[0x1d]=1;
        state[0]=providers.ingame_vtable;
        check(!adapter.panel_close(ptr(resume.data())),"no close request crosses into another state");
        offer(SDLK_V);input.handle(key(SDL_EVENT_KEY_UP,SDLK_V));
        check(!query(chat),"voice release before query never opens a sticky panel");
        offer(SDLK_V);check(query(chat),"held voice panel admission");
        state[0]=chat_vtable;input.gameplay=false;input.clear();input.context(true,false);
        check(adapter.panel_close(ptr(close_left.data()))==0 && adapter.panel_close(ptr(close_right.data()))==0 &&
              !adapter.panel_close(ptr(other.data())),"voice close buttons suppressed while held; selections untouched");
        if(real) original_chat_update(image,adapter,input,state.data(),close_left.data(),close_right.data());
        else input.handle(key(SDL_EVENT_KEY_UP,SDLK_V));
        check(adapter.panel_close(ptr(close_left.data()))==1 && adapter.panel_close(ptr(close_right.data()))==1,
              "voice release selects native close query");
        state[0]=providers.ingame_vtable;
        if(!real) {
            std::array<uint32_t,2> draws{};
            for(auto p:{chat.data(),resume.data(),other.data()}) { word(p,0x80)=ptr(draws.data());word(p,0x88)=0x1234; }
            const auto draw_chat=nova::abi::require_function(image,"_ZN13CButtonIGChat4DrawEi");
            const auto draw_button=nova::abi::require_function(image,"_ZN10CButtonSpr4DrawEi");
            nova::abi::invoke(draw_chat,{ptr(chat.data()),255});
            check(draws==std::array<uint32_t,2>{0,0} && word(chat.data(),0xa4)==1 && word(chat.data(),0x88)==0x1234,
                  "chat artwork/label hidden while original fade side effect runs and text pointer restores");
            nova::abi::invoke(draw_chat,{ptr(other.data()),255});
            check(draws==std::array<uint32_t,2>{2,1},"unrelated chat receiver retains both raster sites and label");
            draws={};state[0]=scores_vtable;
            nova::abi::invoke(draw_button,{ptr(resume.data()),255});
            check(draws==std::array<uint32_t,2>{0,0},"whole Resume graphic/text suppressed");
            nova::abi::invoke(draw_button,{ptr(other.data()),255});
            check(draws==std::array<uint32_t,2>{1,1},"other scoreboard buttons render normally");
            state[0]=providers.ingame_vtable;nova::abi::invoke(draw_button,{ptr(resume.data()),255});
            check(draws==std::array<uint32_t,2>{2,2},"other screens retain Resume rendering");
        }
    }
    check(!std::memcmp(before.data(),reinterpret_cast<void *>(chat_query),5),"chat hook restores exact original entry");
}
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    const bool real=std::strcmp(argv[2],"--real")==0;
    if(real && !std::filesystem::exists(argv[1]))return 77;
    try {
        bindings();nova::ImportRegistry imports;
        if(real) { nova::bionic::register_boot(imports);nova::audio::register_opensl(imports);nova::gles::register_imports(imports); }
        nova::bionic::CxaRegistry cxa(imports);nova::ElfImage image(argv[1],imports);
        installed(image,real);
        if(real) panel_movement_real(image);
        std::puts("communication bindings, installed queries and modal routing passed");return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"communications: %s\n",e.what());return 1; }
}
