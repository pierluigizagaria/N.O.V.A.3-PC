#include "input/native_panel_movement.h"
#include <array>
#include <stdexcept>
#include <vector>

namespace {
using nova::abi::invoke;
uint32_t ptr(auto *p) { return reinterpret_cast<uint32_t>(p); }
uint32_t &word(uint32_t p,uint32_t o=0) { return *reinterpret_cast<uint32_t *>(p+o); }
void check(bool v,const char *m) { if(!v) throw std::runtime_error(m); }
uint32_t level_p,player_p,app_p,mp_p,move_p,jump_p,rush_p;
unsigned saves,stops,detaches,freezes,creates;
uint32_t __cdecl level() { return level_p; }
uint32_t __cdecl player(uint32_t=0,uint32_t=0) { return player_p; }
uint32_t __cdecl app() { return app_p; }
uint32_t __cdecl mp(uint32_t) { return mp_p; }
uint32_t __cdecl current(uint32_t stack) { return word(stack,word(stack,0x34)*4); }
uint32_t __cdecl parent(uint32_t state,uint32_t) { return word(state,0x20); }
uint32_t __cdecl yes(uint32_t=0) { return 1; }
uint32_t __cdecl no(uint32_t=0,uint32_t=0) { return 0; }
void __cdecl quiet(uint32_t=0,uint32_t=0,uint32_t=0) {}
void __cdecl save(uint32_t) { ++saves; }
void __cdecl stop(uint32_t) { ++stops; }
void __cdecl detach(uint32_t,uint32_t,uint32_t) { ++detaches; }
uint32_t __cdecl create(uint32_t) { ++creates;return 0; }
void __cdecl freeze_witness(uint32_t,uint32_t enabled) { if(!enabled) ++freezes; }
void __cdecl button_enable(uint32_t button,uint32_t value) { *reinterpret_cast<uint8_t *>(button+0x1d)=static_cast<uint8_t>(value); }
uint32_t __cdecl button_enabled(uint32_t button) { return *reinterpret_cast<uint8_t *>(button+0x1d); }
void __cdecl scheme_enable(uint32_t,uint32_t value) { for(auto p:{move_p,jump_p,rush_p}) button_enable(p,value); }
uint32_t __cdecl move(uint32_t) { return move_p; }
uint32_t __cdecl jump(uint32_t) { return jump_p; }
uint32_t __cdecl rush(uint32_t) { return rush_p; }
struct Globals {
    std::vector<std::pair<uint32_t,uint32_t>> saved;
    void set(uint32_t p,uint32_t value) { check(p!=0,"fixture global exists");saved.emplace_back(p,word(p));word(p)=value; }
    ~Globals() { for(auto i=saved.rbegin();i!=saved.rend();++i) word(i->first)=i->second; }
};
}
void panel_movement_real(const nova::ElfImage &image) {
    // Execute the original PushState -> GS_InGame::Pause -> normal
    // EnableControls(false) -> MpFreezeControl chain, with isolated storage.
    // Only asset/UI/event terminal dependencies are replaced.
    constexpr auto enable_name="_ZN21CLevelInterfaceNormal14EnableControlsEb";
    constexpr auto freeze_name="_ZN15PlayerComponent15MpFreezeControlEb";
    constexpr auto pause_name="_ZN9GS_InGame5PauseEv";
    const auto fn=[&](const char *name) { return nova::abi::require_function(image,name); };
    std::array<uint32_t,0x18800/4> application{};
    std::array<uint32_t,0x700/4> level_storage{};
    std::array<uint32_t,0x200/4> player_storage{};
    std::array<uint32_t,0x100/4> normal{},scheme{},old{},chat{},scores{},other{},mp_storage{},hud{};
    std::array<std::array<uint32_t,0x100/4>,4> buttons{};
    std::array<uint32_t,40> normal_table{},scheme_table{},old_table{},chat_table{},score_table{},other_table{},button_table{};
    level_p=ptr(level_storage.data());player_p=ptr(player_storage.data());app_p=ptr(application.data());mp_p=ptr(mp_storage.data());
    move_p=ptr(buttons[0].data());jump_p=ptr(buttons[1].data());rush_p=ptr(buttons[2].data());
    const auto touch=ptr(buttons[3].data()),normal_p=ptr(normal.data()),scheme_p=ptr(scheme.data()),old_p=ptr(old.data());
    word(level_p,0x178)=word(level_p,0x17c)=normal_p;word(level_p,0x638)=ptr(hud.data());
    word(normal_p)=ptr(normal_table.data());word(normal_p,0xc)=scheme_p;
    for(auto offset:{0x4cu,0x5cu,0x60u,0x68u,0x6cu,0x70u,0x74u}) word(normal_p,offset)=touch;
    word(scheme_p)=ptr(scheme_table.data());
    button_table[0x7c/4]=ptr(&button_enable);button_table[0x84/4]=ptr(&button_enabled);
    for(auto &b:buttons) b[0]=ptr(button_table.data());
    scheme_table[0xc/4]=ptr(&scheme_enable);scheme_table[0x28/4]=ptr(&move);
    scheme_table[0x38/4]=ptr(&jump);scheme_table[0x40/4]=ptr(&rush);scheme_table[0x44/4]=ptr(&move);
    normal_table[0x10/4]=fn(enable_name);normal_table[0x2c/4]=ptr(&save);
    old[0]=ptr(old_table.data());old[1]=old_p+4;old_table[0x1c/4]=fn(pause_name);
    for(auto *table:{&chat_table,&score_table,&other_table}) { (*table)[8/4]=ptr(&no);(*table)[0xc/4]=ptr(&create); }
    chat[0]=ptr(chat_table.data());scores[0]=ptr(score_table.data());other[0]=ptr(other_table.data());
    Globals globals;
    globals.set(image.symbol("_ZN13GameMpManager9SingletonE"),mp_p);
    globals.set(image.symbol("_ZN18GlobalEventManager9SingletonE"),mp_p);
    // Original PushState's touch queue reset uses these relocated global slots.
    for(auto delta:{0x2fcu,0x818u,0xc04u}) globals.set(word(image.address(0x113e340-delta,4)),delta==0x2fc ? 1u:0u);
    std::vector<std::unique_ptr<nova::abi::GuestCall>> calls;
    auto replace=[&](const char *owner,uint32_t offset,const char *callee,uint32_t target) {
        calls.push_back(std::make_unique<nova::abi::GuestCall>(image,owner,offset,callee,target));
    };
    replace(pause_name,0x21,"_ZN3glf21AndroidSetActionPhaseEi",ptr(&quiet));
    for(auto o:{0x26u,0x30u,0x43u}) replace(pause_name,o,"_ZN6CLevel8GetLevelEv",ptr(&level));
    replace(pause_name,0x6d,"_ZN13GameMpManager15StopPlayersFireEv",ptr(&stop));
    replace(pause_name,0x93,"_ZN12EventManager6detachEjP10IEventRecv",ptr(&detach));
    for(auto o:{0x52u,0x188u,0x24cu,0x3d5u}) replace(enable_name,o,"_ZN9MpManager17IsMultiplayerGameEv",ptr(&yes));
    replace(enable_name,0x6c,"_ZN9MpManager7IsLocalEv",ptr(&yes));
    replace(enable_name,0x16a,"_ZN13GameMpManager18IsGameModeInstagibEv",ptr(&yes));
    for(auto o:{0x1c1u,0x1f8u,0x222u}) replace(enable_name,o,"_ZN6CLevel8GetLevelEv",ptr(&level));
    replace(enable_name,0x1d6,"_ZN4CHud16EnableAllButtonsEb",ptr(&quiet));
    replace(enable_name,0x20d,"_ZN4CHud15EnabledGrenadesEb",ptr(&quiet));
    replace(enable_name,0x23a,"_ZN4CHud14EnabledWeaponsEb",ptr(&quiet));
    for(auto o:{0x2d5u,0x2f1u}) replace(enable_name,o,"_ZN13GameMpManager13GetMyMpPlayerEv",ptr(&mp));
    replace(enable_name,0x303,"_ZNK11CGameObject12GetComponentEi",ptr(&player));
    for(auto o:{0x5cu,0x6bu,0x88u,0x9du,0xcbu,0xe8u,0x110u}) replace(freeze_name,o,"_ZN6CLevel8GetLevelEv",ptr(&level));
    replace(freeze_name,0x83,"_ZN14CWeaponManager5UnAimEv",ptr(&quiet));
    for(auto o:{0x98u,0x120u}) replace(freeze_name,o,"_ZN6CLevel17MP_EnableControlsEb",ptr(&freeze_witness));
    replace(freeze_name,0xb6,"_ZN18CBaseControlScheme19EnableRotateControlEb",ptr(&quiet));
    replace(freeze_name,0xc6,"_ZN15PlayerComponent18EnableJetpackSoundEb",ptr(&quiet));
    replace(freeze_name,0xe3,"_ZN11CGameObject6EnableEb",ptr(&quiet));
    nova::input::Controls input(nova::input::ControlsConfig{});input.gameplay=true;
    nova::input::NativePanelMovement adapter(image,input,{ptr(&level),ptr(&player),ptr(&app),ptr(&current),ptr(&parent),
        ptr(old_table.data()),ptr(chat_table.data()),ptr(score_table.data()),ptr(normal_table.data())});
    const auto push=fn("_ZN12gxStateStack9PushStateEP11gxGameState"),stack=app_p+0x18738;
    const auto open=[&](uint32_t panel,bool allowed=true,bool frozen=false) {
        word(stack)=old_p;word(stack,0x34)=0;input.gameplay=true;input.context(true,false);
        button_enable(move_p,allowed);button_enable(jump_p,0);button_enable(rush_p,1);button_enable(touch,1);
        word(player_p,0x48)=0x3f800000;saves=stops=detaches=freezes=creates=0;
        *reinterpret_cast<uint8_t *>(mp_p+0x24)=frozen;
        invoke(push,{stack,panel});
        check(saves==1 && stops==1 && detaches==1 && creates==1,"native panel lifecycle remains intact");
        check(!button_enabled(move_p) && !button_enabled(touch),"original pause disables touch movement and combat");
    };
    for(auto panel:{ptr(chat.data()),ptr(scores.data())}) {
        open(panel);
        check(freezes==0 && word(player_p,0x48)==0x3f800000,"panel does not freeze actual player locomotion");
        check(adapter.permission(move_p)==true && adapter.permission(jump_p)==false && adapter.permission(rush_p)==true,
              "host locomotion retains only pre-panel native permissions");
        check(!adapter.permission(touch),"touch/combat control has no host exception");
        input.context(false,false);check(!adapter.permission(move_p),"focus loss blocks retained movement");input.context(true,false);
        word(level_p,0x178)=0;check(!adapter.permission(move_p),"interface replacement blocks lease");word(level_p,0x178)=normal_p;
        word(stack,0x34)=0;check(!adapter.permission(move_p),"closed panel blocks lease");word(stack,0x34)=1;
        invoke(fn(freeze_name),{player_p,1});
        check(freezes==1 && word(player_p,0x48)==0 && !adapter.permission(move_p),"unrelated native freeze executes and revokes lease");
        open(panel);invoke(fn(enable_name),{normal_p,0});
        check(!adapter.permission(move_p),"unrelated interface disable executes and revokes lease");
        open(panel,false);
        check(adapter.permission(move_p)==false,"already disabled movement never gains permission");
        open(panel,true,true);
        check(freezes==1 && word(player_p,0x48)==0 && !adapter.permission(move_p),"native MP freeze during panel entry executes and revokes lease");
    }
    open(ptr(other.data()),true,true);
    check(freezes==1 && word(player_p,0x48)==0 && !adapter.permission(move_p),"ordinary menus retain native freeze");
}
