#include "input/native_power_selection.h"
#include "input/grenade_gesture.h"
#include "bionic/runtime.h"
#include "audio/opensl.h"
#include "gles/runtime.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {
uint32_t ptr(auto *p) { return reinterpret_cast<uint32_t>(p); }
uint32_t &word(uint32_t p,uint32_t offset=0) { return *reinterpret_cast<uint32_t *>(p+offset); }
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
uint32_t level_p,player_p,power_button;
uint32_t __cdecl level() { return level_p; }
uint32_t __cdecl player(uint32_t) { return player_p; }
uint32_t __cdecl powers(uint32_t) { return power_button; }
void __cdecl quiet(uint32_t=0,uint32_t=0,uint32_t=0,uint32_t=0) {}
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    const bool real=std::strcmp(argv[2],"--real")==0;
    if(real && !std::filesystem::exists(argv[1])) return 77;
    try {
        using nova::input::GrenadeCommand;
        nova::input::GrenadeGesture gesture;
        check(gesture.sample(true,true,100,1)==GrenadeCommand::none,"press does not place before hold is known");
        check(gesture.sample(false,false,200,1)==GrenadeCommand::throw_grenade,"short release throws once");
        check(gesture.sample(false,false,210,1)==GrenadeCommand::none,"release cannot repeat");
        check(gesture.sample(false,true,220,1)==GrenadeCommand::throw_grenade,"sub-frame tap survives");
        check(gesture.sample(true,true,300,1)==GrenadeCommand::none,"hold starts without throwing");
        check(gesture.sample(true,false,649,1)==GrenadeCommand::none,"hold threshold is strict");
        check(gesture.sample(true,false,650,1)==GrenadeCommand::detonate,"hold detonates once");
        check(gesture.sample(true,false,1200,1)==GrenadeCommand::none &&
              gesture.sample(false,false,1300,1)==GrenadeCommand::none,"held repetition and release never place after detonation");
        gesture.sample(true,true,1400,1);
        check(gesture.sample(false,false,1450,2)==GrenadeCommand::none,"focus/rebind epoch cancels pending throw");
        gesture.sample(true,true,1500,2);gesture.cancel();
        check(gesture.sample(false,false,1600,2)==GrenadeCommand::none,"owner loss cancels pending throw");
        nova::ImportRegistry imports;
        if(real) { nova::bionic::register_boot(imports); nova::audio::register_opensl(imports); nova::gles::register_imports(imports); }
        nova::bionic::CxaRegistry cxa(imports); nova::ElfImage image(argv[1],imports);
        auto fn=[&](const char *name) { return nova::abi::require_function(image,name); };
        std::array<uint32_t,128> level_data{},player_data{},normal{},scheme{},table{},manager{},button{},hud{};
        std::array<uint32_t,33> frames{};
        std::array<uint32_t,3> ids{0,2,4};
        std::array<uint32_t,10> special_slots{};
        std::array<uint32_t,5> normal_slots{};
        level_p=ptr(level_data.data());player_p=ptr(player_data.data());power_button=ptr(button.data());
        const auto hud_p=ptr(hud.data()),manager_p=ptr(manager.data()),normal_p=ptr(normal.data());
        for(size_t i=0;i<3;++i) frames[i*11+6]=ids[i];
        button[0x84/4]=ptr(frames.data());button[0x88/4]=button[0x84/4]+sizeof(frames);
        *reinterpret_cast<uint8_t *>(power_button+0xba)=1;
        hud[0x98/4]=ptr(ids.data());hud[0x9c/4]=hud[0x98/4]+sizeof(ids);
        manager[0x10/4]=ptr(normal_slots.data());manager[0x14/4]=manager[0x10/4]+sizeof(normal_slots);
        manager[0x28/4]=ptr(special_slots.data());manager[0x2c/4]=manager[0x28/4]+sizeof(special_slots);
        for(size_t i=0;i<5;++i) special_slots[i*2+1]=1;
        player_data[0x174/4]=manager_p;
        level_data[0x178/4]=level_data[0x17c/4]=normal_p;
        normal[0xc/4]=ptr(scheme.data());normal[0x70/4]=hud_p;
        scheme[0]=ptr(table.data());table[0x44/4]=ptr(&powers);
        std::vector<std::unique_ptr<nova::abi::GuestCall>> calls;
        uint32_t sound_global=0,sound_before=0;
        constexpr auto special="_Z13SweepCallbackP12CButtonSweepi";
        constexpr auto grenade="_Z16HudSweepCallbackP15CButtonHudSweepi";
        if(real) {
            auto replace=[&](const char *owner,uint32_t offset,const char *callee,uint32_t target) {
                calls.push_back(std::make_unique<nova::abi::GuestCall>(image,owner,offset,callee,target));
            };
            for(auto o:{0x1du,0x39u,0x49u,0xaau,0xdeu,0x108u}) replace(special,o,"_ZN6CLevel8GetLevelEv",ptr(&level));
            for(auto o:{0xb2u,0xeau}) replace(special,o,"_ZN6CLevel18GetPlayerComponentEv",ptr(&player));
            replace(special,0xa5,"_ZN15VoxSoundManager4PlayEPKcii",ptr(&quiet));
            for(auto o:{0x20u,0x32u,0x62u,0x89u,0xceu,0xedu}) replace(grenade,o,"_ZN6CLevel8GetLevelEv",ptr(&level));
            for(auto o:{0x6eu,0xd6u}) replace(grenade,o,"_ZN6CLevel18GetPlayerComponentEv",ptr(&player));
            replace(grenade,0xc9,"_ZN15VoxSoundManager4PlayEPKcii",ptr(&quiet));
            for(auto setter:{"_ZN13CPowerManager21SetCurrentNormalPowerEi","_ZN13CPowerManager15SetSpecialPowerEi"}) {
                replace(setter,0x34,"_ZN6CLevel8GetLevelEv",ptr(&level));
                replace(setter,0x40,std::strstr(setter,"Normal") ? "_ZN6CLevel24NotifyNormalPowerChangedEi" :
                    "_ZN6CLevel25NotifySpecialPowerChangedEi",ptr(&quiet));
            }
            replace("_ZN13CPowerManager22SetCurrentSpecialPowerEii",0x68,"_ZN6CLevel8GetLevelEv",ptr(&level));
            replace("_ZN13CPowerManager22SetCurrentSpecialPowerEii",0x74,"_ZN6CLevel25NotifySpecialPowerChangedEi",ptr(&quiet));
            sound_global=image.symbol("_ZN15VoxSoundManager9SingletonE");
            check(sound_global!=0,"sound singleton exists");sound_before=word(sound_global);word(sound_global)=1;
        }
        const auto special_set=fn(real ? "_ZN12CButtonSweep19SetCurrentSelectionEi" : "selection_special_set");
        const auto grenade_set=fn(real ? "_ZN15CButtonHudSweep19SetCurrentSelectionEi" : "selection_grenade_set");
        const auto special_callback=fn(real ? special : "selection_callback");
        const auto grenade_callback=fn(real ? grenade : "selection_callback");
        auto cycle=[&](bool grenade_kind,int direction) {
            return nova::input::cycle_power_selection(grenade_kind ? hud_p : power_button,grenade_kind,direction,
                grenade_kind ? grenade_set : special_set,grenade_kind ? grenade_callback : special_callback);
        };
        check(cycle(false,1) && button[0xa4/4]==1,"next special selects original frame ID2");
        if(real) check(manager[0x50/4]==2 && *reinterpret_cast<uint8_t *>(normal_p+0x42)==1,
            "original callback changes actual manager slot and tutorial flag");
        check(cycle(false,-1) && button[0xa4/4]==0,"previous special restores first");
        check(cycle(false,-1) && button[0xa4/4]==2,"previous wraps to last");
        frames[11+4]=1;
        check(cycle(false,-1) && button[0xa4/4]==0,"disabled intermediate choice is skipped");
        *reinterpret_cast<uint8_t *>(power_button+0xba)=0;
        check(!cycle(false,1) && button[0xa4/4]==0,"native sweep gate remains authoritative");
        *reinterpret_cast<uint8_t *>(power_button+0xba)=1;
        frames[22+4]=1;
        check(!cycle(false,1),"only current choice available emits no callback");
        check(cycle(true,1) && hud[0xb4/4]==1,"grenade selection uses sparse ID table");
        if(real) check(manager[0x4c/4]==2 && *reinterpret_cast<uint8_t *>(normal_p+0x3f)==1,
            "original grenade callback changes manager and tutorial flag");
        check(cycle(true,1) && cycle(true,1) && hud[0xb4/4]==0,"grenade cycle wraps");
        hud[0x9c/4]=hud[0x98/4]+4; check(!cycle(true,1),"single grenade does not retrigger");
        hud[0x9c/4]=hud[0x98/4]; check(!cycle(true,1),"empty grenade list does nothing");
        hud[0x9c/4]=hud[0x98/4]+3;
        bool rejected=false;try { cycle(true,1); } catch(const std::runtime_error &) { rejected=true; }
        check(rejected,"misaligned native selection list is rejected");
        if(sound_global) word(sound_global)=sound_before;
        std::puts("power selection: sparse IDs, direction, wrap, gates and callbacks passed");return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"power selection: %s\n",e.what());return 1; }
}
