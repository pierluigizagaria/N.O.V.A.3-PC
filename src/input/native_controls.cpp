#include "input/native_controls.h"
#include "input/native_draw.h"
#include "input/native_ads.h"
#include "input/normal_keyboard_movement.h"
#include "input/native_power_selection.h"
#include "camera/native_world_fov.h"
#include "camera/native_death_camera.h"
#include "net/native_snapshot_presentation.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <windows.h>
#include <intrin.h>

namespace nova::input {
namespace {
template<class T = uint32_t> T read(uint32_t p, uint32_t offset = 0) {
    return *reinterpret_cast<const T *>(p + offset);
}
uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }
using abi::invoke;
using abi::invoke_bool;
thread_local BikeHostEventTag bike_host_event;
thread_local uint32_t bike_event_depth=0;
struct NormalAimDispatch {
    uint32_t manager,level,player,interface_,state;
    uint64_t epoch;
    bool reload_ready=false,reload_waiting=false;
};
struct NormalInputBoundary {
    NormalInputBoundary *previous;
    uint32_t interface_,level,player,state;
    uint64_t epoch;
    bool attempted=false;
};
thread_local unsigned normal_input_depth=0;
thread_local NormalInputBoundary *normal_input_boundary=nullptr;
struct NormalBoundaryScope {
    NormalInputBoundary *previous;
    explicit NormalBoundaryScope(NormalInputBoundary &current) : previous(normal_input_boundary) { normal_input_boundary=&current; }
    ~NormalBoundaryScope() { normal_input_boundary=previous; }
};
thread_local NormalAimDispatch *normal_aim_dispatch=nullptr;
thread_local uint32_t normal_switch_depth=0;
thread_local bool normal_switch_authorized=false;
constexpr auto get_level = "_ZN6CLevel8GetLevelEv";
constexpr auto get_player = "_ZN6CLevel18GetPlayerComponentEv";
constexpr auto get_mech = "_ZN6CLevel22GetPlayerMechComponentEv";
constexpr auto get_machinegun = "_ZN6CLevel28GetPlayerMachineGunComponentEv";
constexpr auto get_bike = "_ZN6CLevel22GetPlayerBikeComponentEv";
constexpr auto get_jeep = "_ZN6CLevel22GetPlayerJeepComponentEv";
constexpr auto jeep_control_type = "_ZNK20CPlayerJeepComponent14GetControlTypeEv";
constexpr uint32_t move_control = 0x28, rotate_control = 0x2c, shoot_control = 0x30;
constexpr uint32_t second_shoot_control = 0x34;
constexpr uint32_t jump_control = 0x38, aim_control = 0x3c, rush_control = 0x40, powers_control = 0x44;
}
NativeControls *NativeControls::instance_ = nullptr;
uint32_t NativeControls::fn(const char *name) const { return functions_.at(name); }
NativeControls::NativeControls(const ElfImage &image, Controls &input, bool detailed_motion)
    : image_(image), input_(input), communications_(image,input), panel_movement_(image,input), gamepad_(image,input), trace_(detailed_motion), diagnostics_(detailed_motion) {
    if (instance_) throw std::runtime_error("Only one native controls instance is supported");
    auto symbol = [&](const std::string &name) {
        auto address = image.symbol(name);
        if (!address || !image.contains(address)) throw std::runtime_error("Missing native controls symbol: " + name);
        return address;
    };
    for (auto name : {get_level,get_player,get_mech,get_machinegun,get_jeep,get_bike,jeep_control_type,
        "_ZNK11CGameObject12GetComponentEi", "_ZN16CWeaponComponent16HasSecondaryFireEv",
        "_ZN14CWeaponManager17FireButtonReleaseEi", "_ZN11CVehicleCar15StopShooterFireEv",
        "_ZN14CPlayerControl7onEventEPK6IEventPK12EventManager", "_ZN16CWeaponComponent8FireStopEib",
        "_ZN14CPlayerControl13ResetControlsEv",
        "_Z25HudPressedNoSweepCallbackP15CButtonHudSweep",
        "_Z22PressedNoSweepCallbackP12CButtonSweep",
        "_Z16HudSweepCallbackP15CButtonHudSweepi", "_Z13SweepCallbackP12CButtonSweepi",
        "_ZN15CButtonHudSweep19SetCurrentSelectionEi", "_ZN12CButtonSweep19SetCurrentSelectionEi",
        "_ZN12EventManager9raiseSyncERK6IEvent", "_ZN13CPowerManager21GetNormalPowerCrtAmmoEi",
        "_ZN13CPowerManager11CanUsePowerEb", "_ZN13CPowerManager18GetPowerTypeOnSlotEi",
        "_ZN11Application11GetInstanceEv", "_ZN12gxStateStack12CurrentStateEv",
        "_ZNK11CGameObject6IsDeadEv", "_ZN15PlayerComponent19IsWatchingCinematicEv",
        "_ZN11CGameObject9RotateYawEf", "_ZN11CGameObject11RotatePitchEf",
        "_ZN15PlayerComponent13UseMeleePowerEv", "_ZN15PlayerComponent8InteractEv",
        "_ZN15PlayerComponent10UnInteractEv",
        "_ZN14CWeaponManager5UnAimEv",
        "_ZN16CWeaponComponent5UnAimEb",
        "_ZN16CWeaponComponent21GetMainAnimationStateEv",
        "_ZN14CWeaponManager13SetNextWeaponEb",
        "_ZN14CWeaponManager13SetPrevWeaponEb", "_ZN14CWeaponManager16SetCurrentWeaponEii",
        "_ZN14CWeaponManager11IsAvailableEi", "_ZN14CWeaponManager18GetWeaponIndexByIdEi", "_ZN7CButton10EnableDrawEb",
        "_ZN4CHud10HideButtonEi", "luaL_newstate", "lua_pushboolean", "lua_settop",
        "lua_toboolean", "lua_close", "_Z17Menu_SetAimAssistP9lua_State",
        "_Z17Menu_GetAimAssistP9lua_State"}) functions_[name] = abi::require_function(image,name);
    normal_update_entry_=abi::require_function(image,"_ZN21CLevelInterfaceNormal6UpdateEi");
    constexpr std::array<uint8_t,11> detonator_query{0x8b,0x47,0x68,0x8b,0x10,0x89,0x04,0x24,0xff,0x52,0x30};
    if(!std::equal(detonator_query.begin(),detonator_query.end(),
                  reinterpret_cast<const uint8_t *>(normal_update_entry_+0x5ee)))
        throw std::runtime_error("Native detonator query layout mismatch");
    normal_switch_call_=std::make_unique<abi::GuestCall>(image,"_ZN21CLevelInterfaceNormal6UpdateEi",0xe4c,
        "_ZN14CWeaponManager9SwitchAimEv",reinterpret_cast<uint32_t>(&normal_switch));
    normal_can_aim_call_=std::make_unique<abi::GuestCall>(image,"_ZN14CWeaponManager9SwitchAimEv",0x40,
        "_ZN16CWeaponComponent11CanStartAimEv",reinterpret_cast<uint32_t>(&normal_can_aim));
    // Directional filters are restricted to our original normal-input scope.
    // The native SwitchAim active branch passes true for ordinary animated exit.
    normal_unaim_call_=std::make_unique<abi::GuestCall>(image,"_ZN14CWeaponManager9SwitchAimEv",0x93,
        "_ZN16CWeaponComponent5UnAimEb",reinterpret_cast<uint32_t>(&normal_native_unaim));
    normal_reload_restore_=std::make_unique<abi::GuestCall>(image,"_ZN16CWeaponComponent7OnEventEiPKv",0x453,
        "_ZN16CWeaponComponent3AimEv",reinterpret_cast<uint32_t>(&normal_reload_restore_aim));
    normal_switch_hook_=std::make_unique<abi::GuestHook>(image,"_ZN14CWeaponManager9SwitchAimEv",
        std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&normal_switch_entry));
    normal_select_=std::make_unique<abi::GuestHook>(image,"_ZN14CWeaponManager16SetCurrentWeaponEii",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x18},reinterpret_cast<uint32_t>(&normal_select_weapon));
    normal_ctor1_=std::make_unique<abi::GuestHook>(image,"_ZN16CWeaponComponentC1EP11CGameObject",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x53},reinterpret_cast<uint32_t>(&normal_ctor1));
    if(image.symbol("_ZN16CWeaponComponentC2EP11CGameObject")!=image.symbol("_ZN16CWeaponComponentC1EP11CGameObject"))
      normal_ctor2_=std::make_unique<abi::GuestHook>(image,"_ZN16CWeaponComponentC2EP11CGameObject",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x53},reinterpret_cast<uint32_t>(&normal_ctor2));
    normal_reset_=std::make_unique<abi::GuestHook>(image,"_ZN16CWeaponComponent11ResetWeaponEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&normal_reset));
    normal_reinit_=std::make_unique<abi::GuestHook>(image,"_ZN16CWeaponComponent6ReInitEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x83,0xec,0x14},reinterpret_cast<uint32_t>(&normal_reinit));
    normal_dtor0_=std::make_unique<abi::GuestHook>(image,"_ZN16CWeaponComponentD0Ev",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x18},reinterpret_cast<uint32_t>(&normal_dtor0));
    normal_dtor1_=std::make_unique<abi::GuestHook>(image,"_ZN16CWeaponComponentD1Ev",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&normal_dtor1));
    if(image.symbol("_ZN16CWeaponComponentD2Ev")!=image.symbol("_ZN16CWeaponComponentD1Ev"))
      normal_dtor2_=std::make_unique<abi::GuestHook>(image,"_ZN16CWeaponComponentD2Ev",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&normal_dtor2));
    update_ = std::make_unique<abi::GuestHook>(image,"_ZN21CLevelInterfaceNormal6UpdateEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&update));
    pressed_ = std::make_unique<abi::GuestHook>(image,"_ZN7CButton8IsActiveEv",
        std::initializer_list<uint8_t>{0x55,0x31,0xc0,0x89,0xe5},reinterpret_cast<uint32_t>(&pressed));
    draw_ = std::make_unique<abi::GuestHook>(image,"_ZN21CLevelInterfaceNormal4DrawEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0xbf,0xff,0x00,0x00,0x00},reinterpret_cast<uint32_t>(&draw));
    hud_ = std::make_unique<abi::GuestHook>(image,"_ZN4CHud6RenderEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&hud_render));
    for (auto [type,stack] : {std::pair{"19CLevelTutorialShoot",0x18},
             std::pair{"26CLevelTutorialSpecialPower",0x18},std::pair{"18CLevelTutorialRush",0x18},
             std::pair{"20CLevelTutorialRotate",0x18},std::pair{"18CLevelTutorialMove",0x28},
             std::pair{"18CLevelTutorialJump",0x18},std::pair{"23CLevelTutorialIronSight",0x18},
             std::pair{"22CLevelTutorialDetonate",0x18}}) {
        auto hook = std::make_unique<abi::GuestHook>(image,std::string("_ZN")+type+"4DrawEv",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,static_cast<uint8_t>(stack)},
            reinterpret_cast<uint32_t>(&tutorial_draw));
        tutorial_originals_[symbol(std::string("_ZTV")+type)+8] = hook->original();
        tutorials_.push_back(std::move(hook));
    }
    edge_ = std::make_unique<abi::GuestHook>(image,"_ZN7CButton7PressedEv",
        std::initializer_list<uint8_t>{0x55,0x31,0xc0,0x89,0xe5},reinterpret_cast<uint32_t>(&edge_pressed));
    interaction_draw_ = std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponent25DrawCustomInteractButtonsEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},reinterpret_cast<uint32_t>(&interaction_draw));
    player_input_ = std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponent15UpdateFromInputEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&player_input));
    mech_input_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerMechComponent15UpdateFromInputEi",
        std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&mech_input));
    for (uint32_t offset : {0xe71u,0xee1u})
        mech_stops_.push_back(std::make_unique<abi::GuestCall>(image,"_ZN20CPlayerMechComponent6UpdateEi",offset,
            "_ZN16CWeaponComponent8FireStopEib",reinterpret_cast<uint32_t>(&mech_stop)));
    machinegun_update_ = std::make_unique<abi::GuestHook>(image,"_ZN26CPlayerMachineGunComponent6UpdateEi",
        std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&machinegun_update));
    machinegun_input_ = std::make_unique<abi::GuestHook>(image,"_ZN26CPlayerMachineGunComponent15UpdateFromInputEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&machinegun_input));
    machinegun_stop_ = std::make_unique<abi::GuestCall>(image,"_ZN26CPlayerMachineGunComponent6UpdateEi",0xb31,
        "_ZN16CWeaponComponent8FireStopEib",reinterpret_cast<uint32_t>(&machinegun_stop));
    machinegun_release_ = std::make_unique<abi::GuestCall>(image,"_ZN26CPlayerMachineGunComponent6UpdateEi",0xb4d,
        "_ZN14CWeaponManager17FireButtonReleaseEi",reinterpret_cast<uint32_t>(&machinegun_release));
    bike_events_ = std::make_unique<abi::GuestHook>(image,"_ZN14CPlayerControl7onEventEPK6IEventPK12EventManager",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&bike_on_event));
    bike_reset_input_ = std::make_unique<abi::GuestHook>(image,"_ZN14CPlayerControl12ResetControlEv",
        std::initializer_list<uint8_t>{0x55,0x31,0xd2,0x89,0xe5},reinterpret_cast<uint32_t>(&bike_reset_control));
    bike_reset_inputs_ = std::make_unique<abi::GuestHook>(image,"_ZN14CPlayerControl13ResetControlsEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&bike_reset_controls));
    bike_base_ = std::make_unique<abi::GuestCall>(image,"_ZN20CPlayerBikeComponent6UpdateEi",0x34,
        "_ZN20CPlayerBaseComponent10UpdateBaseEi",reinterpret_cast<uint32_t>(&bike_base));
    bike_primary_ = std::make_unique<abi::GuestCall>(image,"_ZN20CPlayerBikeComponent6UpdateEi",0x13c8,
        "_ZN11CGameObject21SetRotationQauternionERKN6glitch4core10quaternionE",reinterpret_cast<uint32_t>(&bike_primary));
    bike_enable_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerBikeComponent6EnableEb",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x58},reinterpret_cast<uint32_t>(&bike_enable));
    bike_state_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerBikeComponent8SetStateEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x81,0xec,0xe8,0,0,0},reinterpret_cast<uint32_t>(&bike_state));
    bike_road_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerBikeComponent7SetRoadEP15CWayPointObject",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&bike_road));
    bike_configure_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerBikeComponent9ConfigureEPK26CComponentPlayerBikeConfig",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x83,0xec,0x14},reinterpret_cast<uint32_t>(&bike_configure));
    bike_move_to_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerBikeComponent11StartMoveToERKN6glitch4core8vector3dIfEES5_PKc",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x53},reinterpret_cast<uint32_t>(&bike_move_to));
    bike_reset_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerBikeComponent14ResetComponentEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x53},reinterpret_cast<uint32_t>(&bike_reset));
    bike_load_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerBikeComponent8SaveLoadEP13CMemoryStream",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&bike_load));
    jeep_update_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerJeepComponent6UpdateEi",
        std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&jeep_update));
    for (uint32_t offset : {0x1d13u,0xfe5u})
        jeep_release_calls_.push_back(std::make_unique<abi::GuestCall>(image,"_ZN20CPlayerJeepComponent6UpdateEi",offset,
            "_ZN16CWeaponComponent8FireStopEib",reinterpret_cast<uint32_t>(&jeep_stop)));
    jeep_release_calls_.push_back(std::make_unique<abi::GuestCall>(image,"_ZN20CPlayerJeepComponent6UpdateEi",0x1d21,
        "_ZN11CVehicleCar15StopShooterFireEv",reinterpret_cast<uint32_t>(&jeep_primary_animation)));
    jeep_release_calls_.push_back(std::make_unique<abi::GuestCall>(image,"_ZN20CPlayerJeepComponent6UpdateEi",0xff3,
        "_ZN11CVehicleCar15StopShooterFireEv",reinterpret_cast<uint32_t>(&jeep_secondary_animation)));
    driver_camera_ = std::make_unique<abi::GuestHook>(image,"_ZN20CPlayerJeepComponent17UpdateFromPhysicsEi",
        std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&driver_camera));
    driver_pedals_ = std::make_unique<abi::GuestHook>(image,"_ZN21CLevelInterfaceNormal16UpdateJeepPedalsEv",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x28},reinterpret_cast<uint32_t>(&driver_pedals));
    analog_draw_ = std::make_unique<abi::GuestHook>(image,"_ZN14AnalogJoystick4DrawEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&analog_draw));
    mobile_draw_ = std::make_unique<abi::GuestHook>(image,"_ZN20MobileAnalogJoystick4DrawEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&mobile_draw));
    angular_draw_ = std::make_unique<abi::GuestHook>(image,"_ZN20AngularRotateControl4DrawEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&angular_draw));
    for (auto type : {"24CLevelTutorialMechRocket","28CLevelTutorialMechMachineGun"}) {
        auto hook = std::make_unique<abi::GuestHook>(image,std::string("_ZN")+type+"4DrawEv",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x18},reinterpret_cast<uint32_t>(&tutorial_draw));
        auto vtable = symbol(std::string("_ZTV")+type)+8;
        mech_tutorials_.insert(vtable);
        tutorial_originals_[vtable] = hook->original();
        tutorials_.push_back(std::move(hook));
    }
    velocity_ = std::make_unique<abi::GuestHook>(image,"_ZN15PlayerComponent22ComputeControlVelocityEi",
        std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},reinterpret_cast<uint32_t>(&velocity));
    auto add_scheme = [&](const char *type, std::initializer_list<uint8_t> prologue) {
        auto hook = std::make_unique<abi::GuestHook>(image,std::string("_ZN")+type+"4DrawEi",
            prologue,reinterpret_cast<uint32_t>(&scheme_draw));
        scheme_originals_[symbol(std::string("_ZTV")+type)+8] = hook->original();
        scheme_draws_.push_back(std::move(hook));
    };
    add_scheme("13ControlScheme",{0x55,0x89,0xe5,0x83,0xec,0x18});
    add_scheme("14ControlScheme1",{0x55,0x89,0xe5,0x83,0xec,0x18});
    add_scheme("14ControlScheme2",{0x55,0x89,0xe5,0x57,0x56});
    add_scheme("14ControlScheme3",{0x55,0x89,0xe5,0x57,0x56});
    add_scheme("14ControlScheme4",{0x55,0x89,0xe5,0x57,0x56,0x53});
    add_scheme("14ControlScheme5",{0x55,0x89,0xe5,0x83,0xec,0x28});
    add_scheme("14ControlScheme7",{0x55,0x89,0xe5,0x57,0x56,0x53});
    // Direct controls.bsprite frame 57 bypasses button/scheme Draw visibility.
    // Change only this CALL; retain native eligibility, percent, clip and cleanup.
    rush_overlay_ = std::make_unique<abi::GuestCall>(image,"_ZN6CLevel17Render2DInterfaceEi",0x1743,
        "_ZNK7CSprite10PaintFrameEiffjffh",reinterpret_cast<uint32_t>(&rush_overlay));
    event_manager_ = symbol("_ZN18GlobalEventManager9SingletonE");
    event_generic_ = symbol("_ZTV9EvGeneric") + 8;
    event_move_ = symbol("_ZTV11EvStickMove") + 8;
    event_release_ = symbol("_ZTV15EvStickReleased") + 8;
    ingame_vtable_ = symbol("_ZTV9GS_InGame") + 8;
    instance_ = this;
    std::printf("native-controls installed; sensitivity=%.4f invert_y=%d touch_autoaim=%d\n",
                input.config.sensitivity,input.config.invert_y,input.config.touch_autoaim);
}
NativeControls::~NativeControls() {
    restore_aim();
    if (lua_) invoke(fn("lua_close"),{lua_});
    instance_ = nullptr;
}
uint32_t NativeControls::control(uint32_t slot) const {
    if (!scheme_) return 0;
    return invoke(read(read(scheme_),slot),{scheme_});
}
bool NativeControls::enabled(uint32_t button) const {
    if(const auto permission=panel_movement_.permission(button)) return *permission;
    return button && read<uint8_t>(button,0x1d);
}
InputTarget NativeControls::machinegun_identity(uint32_t level,uint32_t component,uint32_t player) const {
    return identify_machinegun(level,component,player,[](uint32_t p,uint32_t o) { return read(p,o); },
        [](uint32_t p,uint32_t o) { return read<uint8_t>(p,o); },
        [&](uint32_t p,uint32_t type) { return invoke(fn("_ZNK11CGameObject12GetComponentEi"),{p,type}); });
}
bool NativeControls::machinegun_structure(InputTarget target,uint32_t previous_level) const {
    if (target.kind != TargetKind::machinegun || !previous_level) return false;
    const auto level = invoke(fn(get_level));
    if (level != previous_level || invoke(fn(get_machinegun),{level}) != target.component) return false;
    const auto player = invoke(fn(get_player),{level});
    if (!player || player != target.machinegun.normal_player || read<uint8_t>(player,0xd)) return false;
    const auto mech = invoke(fn(get_mech),{level}), jeep = invoke(fn(get_jeep),{level});
    if ((mech && read<uint8_t>(mech,0xd)) || (jeep && read<uint8_t>(jeep,0xd)) ||
        invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{player})) return false;
    const auto bike = invoke(fn(get_bike),{level});
    if (bike && read<uint8_t>(bike,0xd)) return false;
    return machinegun_structure_matches(target,level,player,[](uint32_t p,uint32_t o) { return read(p,o); },
        [](uint32_t p,uint32_t o) { return read<uint8_t>(p,o); },
        [&](uint32_t p,uint32_t type) { return invoke(fn("_ZNK11CGameObject12GetComponentEi"),{p,type}); });
}
bool NativeControls::machinegun_controller(InputTarget target,uint32_t level) const {
    return machinegun_structure(target,level) && machinegun_controller_matches(target,
        [](uint32_t p,uint32_t o) { return read(p,o); },
        [&](uint32_t p,uint32_t type) { return invoke(fn("_ZNK11CGameObject12GetComponentEi"),{p,type}); }) &&
        machinegun_structure(target,level);
}
bool NativeControls::machinegun_weapon(InputTarget target,uint32_t level) const {
    return machinegun_structure(target,level) && machinegun_weapon_matches(target,
        [](uint32_t p,uint32_t o) { return read(p,o); },
        [&](uint32_t p,uint32_t type) { return invoke(fn("_ZNK11CGameObject12GetComponentEi"),{p,type}); }) &&
        machinegun_structure(target,level);
}
bool NativeControls::machinegun_manager(InputTarget target,uint32_t level) const {
    return machinegun_structure(target,level) && target.machinegun.manager &&
        read(target.machinegun.normal_player,0x170) == target.machinegun.manager;
}
bool NativeControls::machinegun_context(uint32_t level,uint32_t player,uint32_t machinegun) const {
    const auto bike = level ? invoke(fn(get_bike),{level}) : 0;
    if (bike && read<uint8_t>(bike,0xd)) return false;
    const auto owner = machinegun_identity(level,machinegun,player);
    if (!owner) return false;
    const auto jeep = invoke(fn(get_jeep),{level});
    // Ordinary native entry has one local input owner. A Jeep component on the
    // parent (including a remote occupant) is not this local-player getter.
    return !(jeep && read<uint8_t>(jeep,0xd)) &&
        !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{owner.machinegun.actor}) &&
        !invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{player}) &&
        (enabled(control(rotate_control)) || enabled(control(shoot_control)) || enabled(control(second_shoot_control)));
}
bool NativeControls::live_machinegun() const {
    if (target_.kind != TargetKind::machinegun || !target_.component) return false;
    const auto level = invoke(fn(get_level));
    const auto component = level ? invoke(fn(get_machinegun),{level}) : 0;
    if (!live_input_target(target_,level_,level,0,0,component)) return false;
    const auto normal = read(level,0x17c);
    const auto player = invoke(fn(get_player),{level}), mech = invoke(fn(get_mech),{level});
    if (!player) return false;
    const auto app = invoke(fn("_ZN11Application11GetInstanceEv"));
    const auto state = app ? invoke(fn("_ZN12gxStateStack12CurrentStateEv"),{app+0x18738}) : 0;
    return machinegun_owner_matches(target_,level_,normal_,scheme_,
        {level,component,normal,read(level,0x178),normal ? read(normal,0xc) : 0,
         read<uint8_t>(player,0xd) != 0,mech && read<uint8_t>(mech,0xd),communications_.gameplay_state(state)}) &&
        machinegun_context(level,player,component) && machinegun_identity(level,component,player) == target_;
}
InputTarget NativeControls::driver_identity(uint32_t level,uint32_t component) const {
    return identify_jeep(level,component,[](uint32_t p,uint32_t offset) { return read(p,offset); },
        [](uint32_t p,uint32_t offset) { return read<uint8_t>(p,offset); },
        [&](uint32_t p) { return invoke(fn(jeep_control_type),{p}); });
}
bool NativeControls::driver_lifetime(InputTarget target,uint32_t previous_level) const {
    if (target.kind != TargetKind::driver || !target.component) return false;
    const auto level = invoke(fn(get_level));
    const auto component = level && level == previous_level ? invoke(fn(get_jeep),{level}) : 0;
    return driver_lifetime_matches(target,previous_level,level,component,[&] { return driver_identity(level,component); });
}
bool NativeControls::driver_camera_lifetime(InputTarget target,uint32_t previous_level) const {
    if (target.kind != TargetKind::driver || !target.component) return false;
    const auto level = invoke(fn(get_level));
    const auto component = level && level == previous_level ? invoke(fn(get_jeep),{level}) : 0;
    return driver_lifetime_matches(jeep_camera_identity(target),previous_level,level,component,[&] {
        return identify_jeep(level,component,[](uint32_t p,uint32_t offset) { return read(p,offset); },
            [](uint32_t p,uint32_t offset) { return read<uint8_t>(p,offset); },
            [&](uint32_t p) { return invoke(fn(jeep_control_type),{p}); },false);
    });
}
bool NativeControls::driver_input_lifetime(InputTarget target,uint32_t previous_level) const {
    if (target.kind != TargetKind::driver || !target.component) return false;
    const auto level = invoke(fn(get_level));
    const auto component = level && level == previous_level ? invoke(fn(get_jeep),{level}) : 0;
    // Selection tracks weapon/animation/camera replacement. Driver cleanup
    // retains only its still-current driving bindings and driver capability.
    return driver_lifetime_matches(driver_cleanup_identity(target),previous_level,level,component,[&] {
        return driver_cleanup_identity(identify_driver(level,component,[](uint32_t p,uint32_t offset) { return read(p,offset); },
            [](uint32_t p,uint32_t offset) { return read<uint8_t>(p,offset); },
            [&](uint32_t p) { return invoke(fn(jeep_control_type),{p}); }));
    });
}
bool NativeControls::live_driver() const {
    if (!driver_lifetime(target_,level_)) return false;
    const auto current_bike = invoke(fn(get_bike),{level_});
    if (current_bike && read<uint8_t>(current_bike,0xd)) return false;
    const auto level = invoke(fn(get_level)), player = invoke(fn(get_player),{level});
    const auto mech = invoke(fn(get_mech),{level}), gun = invoke(fn(get_machinegun),{level});
    const auto app = invoke(fn("_ZN11Application11GetInstanceEv"));
    const auto state = app ? invoke(fn("_ZN12gxStateStack12CurrentStateEv"),{app+0x18738}) : 0;
    if (!player || read(player,4) != target_.driver.actor ||
        !jeep_owner_matches(target_,level_,{driver_identity(level,target_.component),level,read(level,0x17c),
        read(level,0x178),read(target_.driver.normal,0xc),read<uint8_t>(player,0xd) != 0,
        mech && read<uint8_t>(mech,0xd),gun && read<uint8_t>(gun,0xd),communications_.gameplay_state(state)})) return false;
    return !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{target_.driver.actor}) &&
        !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{read(target_.driver.vehicle,4)}) &&
        !invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{player}) &&
        (enabled(control(move_control)) || enabled(control(rotate_control)) ||
         ((target_.driver.capabilities & 1) && target_.driver.configuration == 1 &&
          (enabled(read(normal_,0x28)) || enabled(read(normal_,0x2c)))) ||
         ((target_.driver.capabilities & 2) && (enabled(control(shoot_control)) || enabled(control(second_shoot_control)))));
}
InputTarget NativeControls::bike_identity(uint32_t level,uint32_t component,uint32_t player) const {
    return identify_bike(level,component,player,bike_generation_,
        [](uint32_t p,uint32_t o) { return read(p,o); },[](uint32_t p,uint32_t o) { return read<uint8_t>(p,o); },
        [&](uint32_t p,uint32_t type) { return invoke(fn("_ZNK11CGameObject12GetComponentEi"),{p,type}); },
        [&](uint32_t scheme,uint32_t slot) { return invoke(read(read(scheme),slot),{scheme}); });
}
bool NativeControls::bike_core(InputTarget owner,uint32_t previous_level) const {
    if (owner.kind!=TargetKind::bike || !previous_level) return false;
    const auto level=invoke(fn(get_level));
    if (level!=previous_level || invoke(fn(get_bike),{level})!=owner.component) return false;
    const auto player=invoke(fn(get_player),{level});
    if (!player || player!=owner.bike.player || read<uint8_t>(player,0xd)) return false;
    for (auto getter : {get_mech,get_machinegun,get_jeep}) {
        const auto c=invoke(fn(getter),{level}); if(c && read<uint8_t>(c,0xd)) return false;
    }
    return bike_structure_matches(owner,level,player,[](uint32_t p,uint32_t o) { return read(p,o); },
        [](uint32_t p,uint32_t o) { return read<uint8_t>(p,o); });
}
bool NativeControls::bike_root(InputTarget owner,uint32_t level) const {
    return bike_core(owner,level) && read(owner.component,0xfc) && read(owner.bike.player,0x170) && bike_core(owner,level);
}
void NativeControls::publish_bike_receiver() {
    auto owner=target_.kind==TargetKind::bike ? target_ : bike_release_.owner;
    if(!owner && bike_retained(bike_observed_owner_,bike_observed_level_)) owner=bike_observed_owner_;
    bike_writers_.receiver=owner ? owner.component+0x10 : 0;
}
void NativeControls::update_bike_core(InputTarget owner,uint32_t level) {
    if(!owner) return;
    if(!same_bike_core(bike_observed_owner_,bike_observed_level_,owner,level)) {
        bike_writers_.receiver=0;
        ++bike_writers_.move; ++bike_writers_.primary; bike_writers_.foreign=false;
    }
    bike_observed_owner_=owner; bike_observed_level_=level;
}
void NativeControls::synchronize_bike_origins() {
    const auto move=bike_writers_.move.load(),primary=bike_writers_.primary.load(); const bool foreign=bike_writers_.foreign.load();
    revoke_bike_stale(bike_actions_,move,primary,foreign);
    revoke_bike_stale(bike_release_.actions,move,primary,foreign);
}
bool NativeControls::bike_fallback_core(InputTarget owner,uint32_t level) const {
    if(bike_writers_.foreign || !bike_core(owner,level) || read(owner.component,0x120)!=0) return false;
    const auto app=invoke(fn("_ZN11Application11GetInstanceEv"));
    const auto state=app ? invoke(fn("_ZN12gxStateStack12CurrentStateEv"),{app+0x18738}) : 0;
    return communications_.gameplay_state(state) &&
        !invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{owner.bike.player}) &&
        bike_core(owner,level) && read(owner.component,0x120)==0 &&
        (!read(owner.component,0xfc) || !read(owner.bike.player,0x170));
}
void NativeControls::fallback_bike_input(InputTarget owner,uint32_t level,BikeActions &actions) {
    synchronize_bike_origins();
    neutralize_owned_bike_input(actions,[&] {
        const bool valid=bike_fallback_core(owner,level); synchronize_bike_origins(); return valid && !bike_writers_.foreign;
    },[&](uint32_t offset) {
        return offset==0xc ? read(owner.component,0x10+offset) : uint32_t(read<uint8_t>(owner.component,0x10+offset));
    },[&](uint32_t offset,uint32_t value) {
        if(offset==0xc) *reinterpret_cast<uint32_t *>(owner.component+0x10+offset)=value;
        else *reinterpret_cast<uint8_t *>(owner.component+0x10+offset)=static_cast<uint8_t>(value);
    });
}
bool NativeControls::live_bike() const {
    if(bike_writers_.foreign) return false;
    const auto owner=target_; if (!bike_root(owner,level_)) return false;
    const auto player=invoke(fn(get_player),{level_});
    const auto app=invoke(fn("_ZN11Application11GetInstanceEv"));
    const auto state=app ? invoke(fn("_ZN12gxStateStack12CurrentStateEv"),{app+0x18738}) : 0;
    if (!communications_.gameplay_state(state) ||
        invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{owner.bike.actor}) ||
        invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{player})) return false;
    return bike_identity(level_,owner.component,player)==owner && bike_root(owner,level_);
}
bool NativeControls::bike_retained(InputTarget owner,uint32_t previous_level) const {
    if(owner.kind!=TargetKind::bike || !previous_level) return false;
    const auto level=invoke(fn(get_level));
    if(level!=previous_level || invoke(fn(get_bike),{level})!=owner.component) return false;
    const auto player=invoke(fn(get_player),{level});
    if(player!=owner.bike.player || !player || read(player,4)!=owner.bike.actor || read(owner.component,4)!=owner.bike.actor) return false;
    return read(level,0x17c)==owner.bike.normal && read(level,0x180)==owner.bike.interface_ &&
        read(owner.bike.interface_,0xc)==owner.bike.scheme &&
        read(owner.component,0x100)==owner.bike.actor+0x68 && read(owner.component,0x104)==owner.bike.actor+0x60;
}
void NativeControls::queue_bike_release(InputTarget owner,uint32_t level) {
    synchronize_bike_origins(); clean_bike_release();
    // One local Bike root retains independent input obligations while the
    // current template/normal-manager dependency is temporarily unavailable.
    retain_bike_cancellation(bike_release_,owner,level,bike_actions_);
    publish_bike_receiver(); clean_bike_release();
}
void NativeControls::clean_bike_release() {
    synchronize_bike_origins();
    const auto owner=bike_release_.owner; const auto level=bike_release_.level;
    if(!owner) return;
    fallback_bike_input(owner,level,bike_release_.actions);
    clean_bike_cancellation(bike_release_,[&] { return bike_retained(owner,level); },[&] {
        const bool valid=bike_retained(owner,level) && read(owner.component,0x120)==0 && !bike_writers_.foreign && bike_root(owner,level);
        synchronize_bike_origins();
        if(valid) validate_bike_raw(bike_release_.actions,[&](uint32_t offset) {
            return offset==0xc ? read(owner.component,0x1c) : uint32_t(read<uint8_t>(owner.component,0x10+offset));
        });
        return valid && !bike_writers_.foreign;
    },
        [&](uint32_t type,float x) { return bike_event(owner,type,x,&bike_release_.actions); },[&](uint32_t type) {
            const bool valid=!bike_writers_.foreign && bike_retained(owner,level);
            synchronize_bike_origins();
            if(!valid || bike_writers_.foreign) return false;
            return type==14 ? bike_release_.actions.moving && read<float>(owner.component,0x1c)==0 :
                bike_release_.actions.primary && !read<uint8_t>(owner.component,0x30) && !read<uint8_t>(owner.component,0x31);
        });
    if(bike_release_.owner==owner && bike_release_.level==level) fallback_bike_input(owner,level,bike_release_.actions);
}
bool NativeControls::bike_artwork(InputTarget owner,uint32_t level) const {
    if(!bike_retained(owner,level)) return false;
    const auto scheme=owner.bike.scheme;
    const auto move=invoke(read(read(scheme),move_control),{scheme});
    if(!bike_retained(owner,level) || move!=owner.bike.move) return false;
    const auto primary=invoke(read(read(scheme),shoot_control),{scheme});
    return primary==owner.bike.primary && bike_retained(owner,level);
}
bool NativeControls::bike_event(InputTarget owner,uint32_t type,float x,BikeActions *actions) {
    const auto level=actions==&bike_release_.actions ? bike_release_.level : level_;
    auto call=[&] {
        uint32_t data[]{type==13 ? event_move_ : type==14 ? event_release_ : event_generic_,type,bits(x),0};
        const auto move=bike_writers_.move.load(),primary=bike_writers_.primary.load();
        const auto saved=bike_host_event;
        bike_host_event={owner.component+0x10,reinterpret_cast<uint32_t>(data),bike_event_depth+1,true};
        try { invoke(fn("_ZN14CPlayerControl7onEventEPK6IEventPK12EventManager"),
            {owner.component+0x10,reinterpret_cast<uint32_t>(data),read(event_manager_)}); }
        catch(...) { bike_host_event=saved; throw; }
        bike_host_event=saved;
        if(actions && !bike_writers_.foreign && bike_retained(owner,level)) {
            if(type==13 && move==bike_writers_.move) {
                actions->move_generation=move; actions->move_bits=read(owner.component,0x1c); actions->move_raw=true;
            }
            if(type==29 && primary==bike_writers_.primary) {
                actions->primary_generation=primary; actions->primary_raw=read<uint8_t>(owner.component,0x30)==1; actions->primary_neutral=false;
            }
        }
        synchronize_bike_origins();
    };
    if(type==14 || type==30) {
        if(!actions) return false;
        const bool called=bike_cancel_boundary(*actions,type,[&] {
            return !bike_writers_.foreign && bike_retained(owner,level) && read(owner.component,0x120)==0 && bike_root(owner,level);
        },[&] { synchronize_bike_origins(); },[&] {
            validate_bike_raw(*actions,[&](uint32_t offset) {
                return offset==0xc ? read(owner.component,0x1c) : uint32_t(read<uint8_t>(owner.component,0x10+offset));
            });
        },call);
        if(!called && (actions->moving || actions->primary) && actions!=&bike_release_.actions) {
            retain_bike_cancellation(bike_release_,owner,level,*actions); publish_bike_receiver();
            fallback_bike_input(owner,level,bike_release_.actions);
        }
        return called;
    }
    if(bike_writers_.foreign) return false;
    call(); return true;
}
void NativeControls::bike_transition(uint32_t component) {
    if(target_.kind!=TargetKind::bike || target_.component!=component) return;
    const auto owner=target_; const auto level=level_;
    queue_bike_release(owner,level);
    // SetRoad is a real replacement boundary. Its advancing native +0x218
    // cursor is deliberately absent from identity; do not clear every waypoint.
    ++bike_generation_; target_={}; clear_actions(); input_.clear(); input_.gameplay=false; publish_bike_receiver();
}
void NativeControls::apply_bike() {
    const auto owner=target_;
    synchronize_bike_origins(); clean_bike_release();
    if(bike_release_.owner) { input_.clear(); return; }
    if (!input_.active() || !live_bike()) { release_target(owner); input_.clear(); return; }
    validate_bike_raw(bike_actions_,[&](uint32_t offset) {
        return offset==0xc ? read(owner.component,0x1c) : uint32_t(read<uint8_t>(owner.component,0x10+offset));
    });
    if (bike_applied_frame_==frame_) return;
    bike_applied_frame_=frame_; frame_input_=input_.take(); // Unsupported XY/actions are discarded.
    apply_bike_actions(frame_input_,bike_actions_,[&] { return input_.active() && live_bike(); },
        [&] { const auto move=control(move_control); return bike_move_available(enabled(move),move ? read(move,0x88) : 0); },
        [&] { return enabled(control(shoot_control)); },
        [&](uint32_t type,float x) { return bike_event(owner,type,x,&bike_actions_); });
}
bool NativeControls::draw_scope() const {
    if(target_.kind==TargetKind::bike) return input_.gameplay && live_bike();
    return hud_presentation_.hide_mobile;
}
void NativeControls::recover_mount_interaction(uint32_t player, bool on_foot) {
    uint32_t target=0;
    if (on_foot && player && read<uint8_t>(player,0x2b9)) {
        const auto slot=read(player,0x108);
        if (slot<12 && net::NativeSnapshotPresentation::generation(static_cast<uint8_t>(slot),false))
            target=read(player,0x1a0);
    }
    if (!target || (!invoke(fn("_ZNK11CGameObject12GetComponentEi"),{target,150}) &&
                    !invoke(fn("_ZNK11CGameObject12GetComponentEi"),{target,160}) &&
                    !invoke(fn("_ZNK11CGameObject12GetComponentEi"),{target,67}))) {
        interaction_wait_player_=interaction_wait_target_=0;
        return;
    }
    const auto now=SDL_GetTicks();
    if (interaction_wait_player_!=player || interaction_wait_target_!=target) {
        interaction_wait_player_=player; interaction_wait_target_=target; interaction_wait_started_=now;
    } else if (now-interaction_wait_started_>=2000) {
        // Native mount requests have no denial packet. Dedicated rejection or
        // a lost reply must not pin CheckInteract's pending flag indefinitely.
        // This only releases the native UI; seat authority stays on the server.
        invoke(fn("_ZN15PlayerComponent10UnInteractEv"),{player});
        interaction_wait_player_=interaction_wait_target_=0;
        std::printf("native-mount interaction wait released\n");
    }
}
void NativeControls::refresh() {
    auto app = invoke(fn("_ZN11Application11GetInstanceEv"));
    auto state = app ? invoke(fn("_ZN12gxStateStack12CurrentStateEv"),{app + 0x18738}) : 0;
    auto level = invoke(fn(get_level));
    auto player = level ? invoke(fn(get_player),{level}) : 0;
    auto mech = level ? invoke(fn(get_mech),{level}) : 0;
    auto machinegun = level ? invoke(fn(get_machinegun),{level}) : 0;
    auto jeep = level ? invoke(fn(get_jeep),{level}) : 0;
    auto bike = level ? invoke(fn(get_bike),{level}) : 0;
    auto normal = level ? read(level,0x17c) : 0;
    const bool bike_enabled = bike && read<uint8_t>(bike,0xd);
    const auto bike_interface = level ? read(level,0x180) : 0;
    const auto input_interface = bike_enabled && read(level,0x178)==bike_interface ? bike_interface : normal;
    scheme_ = input_interface ? read(input_interface,0xc) : 0;
    bool accepts_input = enabled(control(move_control)) || enabled(control(rotate_control)) || enabled(control(shoot_control));
    const bool normal_enabled = player && read<uint8_t>(player,0xd);
    const bool mech_enabled = mech && read<uint8_t>(mech,0xd);
    const bool machinegun_enabled = machinegun && read<uint8_t>(machinegun,0xd);
    const bool jeep_enabled = jeep && read<uint8_t>(jeep,0xd);
    const bool context = communications_.gameplay_state(state) && normal && read(level,0x178) == normal;
    const bool normal_alive = context && normal_enabled && accepts_input &&
        !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{read(player,4)});
    recover_mount_interaction(player,normal_alive);
    const bool mech_alive = context && mech_enabled && (accepts_input || enabled(control(second_shoot_control))) &&
        read(mech,4) && read(mech,0x118) && read(mech,0x120) &&
        !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{read(mech,4)});
    const auto gun_identity = machinegun_identity(level,machinegun,player);
    const auto driver = driver_identity(level,jeep);
    const bool driver_alive = context && driver && player && read(player,4) == driver.driver.actor &&
        !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{driver.driver.actor}) &&
        !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{read(driver.driver.vehicle,4)}) &&
        !invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{player}) &&
        (enabled(control(move_control)) || enabled(control(rotate_control)) ||
         ((driver.driver.capabilities & 1) && driver.driver.configuration == 1 &&
          (enabled(read(normal,0x28)) || enabled(read(normal,0x2c)))) ||
         ((driver.driver.capabilities & 2) && (enabled(control(shoot_control)) || enabled(control(second_shoot_control)))));
    const auto bike_owner = bike_identity(level,bike,player);
    if(bike_enabled && !normal_enabled && !mech_enabled && !machinegun_enabled && !jeep_enabled)
        update_bike_core(bike_owner,level);
    const bool bike_context = communications_.gameplay_state(state) && bike_owner && player &&
        !invoke_bool(fn("_ZNK11CGameObject6IsDeadEv"),{bike_owner.bike.actor}) &&
        !invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{player}) &&
        (enabled(control(move_control)) || enabled(control(shoot_control)));
    auto next = select_input_target(context || bike_context,
        {player,normal_enabled,normal_alive,normal_alive &&
         (!invoke_bool(fn("_ZN15PlayerComponent19IsWatchingCinematicEv"),{player}) || enabled(control(shoot_control)))},
        {mech,mech_enabled,mech_alive,mech_alive && read(mech,0x1bc) != 3},
        {machinegun,machinegun_enabled,context && bool(gun_identity) && machinegun_context(level,player,machinegun),true,{},{},gun_identity.machinegun},
        {jeep,jeep_enabled,driver_alive,driver_alive,driver.driver,driver.jeep},
        {bike,bike_enabled,bike_context,bike_context,{},{},{},bike_owner.bike});
    // Revoke the post-drain normal source before any role can take a frame.
    // Temporary normal disable with the same identity does not retire held input.
    gamepad_.target_observed(level,player,normal,scheme_,mech_enabled || machinegun_enabled || jeep_enabled || bike_enabled);
    // A dead player has no actor input target. Keep relative mouse capture for
    // the active online killcam without admitting movement/fire/weapon actions.
    const bool death_camera = context && camera::NativeDeathCamera::controllable(player);
    const bool gameplay = bool(next) || death_camera;
    const bool panel=communications_.is_panel(state) && gameplay;
    const bool panel_transition=(panel || input_.communication_panel()) && gameplay && input_.gameplay &&
        level==level_ && player==player_ && next==target_;
    input_.communication_panel(panel);
    const bool changed = change_input_target(target_,next,[&](InputTarget old) {
        // A pointer from a previous level/local player may already be destroyed.
        // Native teardown owns that case; never call it or dereference it here.
        if (live_input_target(old,level_,level,player,mech,machinegun,jeep,bike)) {
            // Retain ordinary on-foot context-loss notifications. Only an
            // ownership change needs cleanup addressed to the old receiver.
            if (normal_release_can_broadcast(old,normal_enabled,mech_enabled,machinegun_enabled,jeep_enabled,bike_enabled,bool(next)))
                release_actions(old.component);
            else release_target(old);
        }
    },[&] { clear_actions(); input_.clear(); });
    if (state != state_ || player != player_ || changed || gameplay != input_.gameplay.load()) {
        std::printf("native-state frame=%llu state=%s player=%08x enabled=%d mode=%u tutorial=%08x interface=%08x normal=%08x gameplay=%d\n",
            frame_,state ? image_.symbolize(read(state)).c_str() : "none",player,
            player ? read<uint8_t>(player,0xd) : 0,player ? read(player,0x130) : 0,level ? read(level,0x184) : 0,
            level ? read(level,0x178) : 0,normal,gameplay);
        if (!changed && target_ && level && level == level_) release_target(target_);
        clear_actions();
        if(panel_transition) input_.clear_except_movement();
        else input_.clear();
    }
    level_ = level; player_ = player; mech_ = mech; normal_ = normal;
    scheme_ = input_interface ? read(input_interface,0xc) : 0; state_ = state;
    input_.gameplay = gameplay;
    hud_presentation_=native_hud_presentation(context || bike_context,normal_enabled,
        mech_enabled || machinegun_enabled || jeep_enabled || bike_enabled);
    publish_bike_receiver();
    if (driver_pedal_release_.owned && !driver_input_lifetime(driver_pedal_release_.owner,driver_pedal_release_.level))
        driver_pedal_release_ = {}; // The new native context owns its teardown.
    if (target_ && !input_.active()) {
        if (target_.kind == TargetKind::normal) release_actions(target_.component);
        else release_target(target_);
    }
}
void NativeControls::begin_frame(uint64_t frame) {
    normal_aim_lane_.store(GetCurrentThreadId()); bike_writers_.lane=GetCurrentThreadId();
    frame_ = frame; observed_ = false; look_pending_ = false;
    gamepad_.begin_frame(); refresh(); communications_.begin_frame();
    if (!target_ && input_.pointer_capture() && camera::NativeDeathCamera::controllable(player_)) {
        const auto motion = input_.take();
        const auto delta = look_delta(motion.dx, motion.dy, 0, 80, input_.look_config());
        camera::NativeDeathCamera::rotate(player_, delta.yaw, delta.pitch);
    }
}
void NativeControls::end_frame() {
    communications_.end_frame();
    gamepad_.end_frame();
    restore_aim();
    refresh();
    diagnostics_.end_frame(frame_,input_.gameplay && target_.kind == TargetKind::normal,observed_,
        [&](const char *reason) { sample(reason); });
}
void NativeControls::restore_aim() {
    if (!aim_override_) return;
    invoke(fn("lua_settop"),{lua_,0});
    invoke(fn("lua_pushboolean"),{lua_,saved_aim_});
    invoke(fn("_Z17Menu_SetAimAssistP9lua_State"),{lua_});
    invoke(fn("lua_settop"),{lua_,0});
    aim_override_ = false;
    if (!aim_reported_) {
        invoke(fn("_Z17Menu_GetAimAssistP9lua_State"),{lua_});
        auto restored = invoke(fn("lua_toboolean"),{lua_,1});
        invoke(fn("lua_settop"),{lua_,0});
        std::printf("native-autoaim restored=%u expected=%u\n",restored,saved_aim_);
        if (restored != saved_aim_) throw std::runtime_error("Aim-assist option restoration failed");
        aim_reported_ = true;
    }
}
void NativeControls::event(uint32_t, uint32_t type, float x, float y) {
    // Synchronous native input events notify the player and tutorial listeners together.
    uint32_t table = type == 13 ? event_move_ : type == 14 ? event_release_ : event_generic_;
    uint32_t data[] = {table,type,bits(x),bits(y)};
    invoke(fn("_ZN12EventManager9raiseSyncERK6IEvent"),{read(event_manager_),reinterpret_cast<uint32_t>(data)});
}
void NativeControls::release_actions(uint32_t player) {
    grenade_gesture_.cancel(); detonate_pending_=false; grenade_gamepad_revision_=0;
    normal_aim_intent_.cancel_input();
    release_normal_owned(fire_,aim_,moving_,[&](uint32_t type) { event(player,type); },
        [&] { return read(player,0x170); },
        [&](uint32_t) { /* Current native normal-input boundary reconciles ordinary exit. */ });
    frame_input_ = {};
    look_pending_ = false;
}
void NativeControls::release_event(uint32_t player,uint32_t type) {
    // Component transitions can already have changed EventManager subscribers.
    // Deliver cleanup to the old input receiver, never broadcast to the new one.
    uint32_t data[]{type == 14 ? event_release_ : event_generic_,type,0,0};
    invoke(fn("_ZN14CPlayerControl7onEventEPK6IEventPK12EventManager"),
        {player+0x10,reinterpret_cast<uint32_t>(data),read(event_manager_)});
}
void NativeControls::clear_actions() {
    grenade_gesture_.cancel(); detonate_pending_=false; grenade_gamepad_revision_=0;
    cancel_normal_aim();
    fire_ = aim_ = moving_ = false;
    bike_actions_ = {};
    bike_applied_frame_ = UINT64_MAX;
    mech_actions_ = {};
    mech_weapon_ = 0;
    mech_exit_pending_ = false;
    machinegun_actions_ = {};
    machinegun_exit_pending_ = false;
    driver_actions_ = {};
    jeep_fire_ = {};
    jeep_role_edge_ = {};
    driver_exit_pending_ = false;
    frame_input_ = {};
    look_pending_ = false;
}
void NativeControls::release_target(InputTarget target) {
    auto player = target.component;
    if (target.kind == TargetKind::machinegun) {
        const auto level = invoke(fn(get_level));
        if (!level || level != level_ || invoke(fn(get_machinegun),{level}) != player) {
            clear_actions(); return;
        }
    }
    if (target.kind == TargetKind::bike) {
        queue_bike_release(target,level_);
    } else if (target.kind == TargetKind::driver) {
        release_jeep_fire(jeep_fire_,[&] { return jeep_controller(target,level_); },
            [&] { invoke(fn("_ZN14CPlayerControl13ResetControlsEv"),{player+0x10}); },
            [&] { return jeep_weapon(target,level_); },[&] { return jeep_animation(target,level_); },
            [&](unsigned channel) { invoke(fn("_ZN16CWeaponComponent8FireStopEib"),{target.jeep.weapon,channel,1}); },
            [&](unsigned) { invoke(fn("_ZN11CVehicleCar15StopShooterFireEv"),{target.driver.vehicle}); });
        const bool same = driver_input_lifetime(target,level_);
        release_driver_actions(driver_actions_,same,[&] { driver_event(target,14); });
        if (driver_pedal_release_.owned && driver_pedal_release_.owner == target) {
            if (same) { driver_pedal_release_.pending = true; clean_driver_pedals(); }
            else driver_pedal_release_ = {};
        }
    } else if (target.kind == TargetKind::mech) {
        release_mech_actions(mech_actions_,read<uint8_t>(player,0xd) != 0,
            [&] { invoke(fn("_ZN14CPlayerControl13ResetControlsEv"),{player+0x10}); },
            [&](uint32_t channel) {
                auto weapon = read(player,0x120);
                if (weapon && weapon == mech_weapon_)
                    invoke(fn("_ZN16CWeaponComponent8FireStopEib"),{weapon,channel,1});
            });
    } else if (target.kind == TargetKind::machinegun) {
        const auto level = level_;
        release_owned_machinegun(machinegun_actions_,[&] { return machinegun_structure(target,level); },
            [&] { return machinegun_controller(target,level); },
            [&] { invoke(fn("_ZN14CPlayerControl13ResetControlsEv"),{player+0x10}); },
            [&] { return machinegun_weapon(target,level); },
            [&] { invoke(fn("_ZN16CWeaponComponent8FireStopEib"),{target.machinegun.weapon,0,1}); },
            [&] { return machinegun_manager(target,level); },
            [&] { invoke(fn("_ZN14CWeaponManager17FireButtonReleaseEi"),{target.machinegun.manager,0}); });
    } else {
        if (fire_) release_event(player,30);
        auto weapons = read(player,0x170);
        if (aim_ && weapons) invoke(fn("_ZN14CWeaponManager5UnAimEv"),{weapons});
        if (moving_) release_event(player,14);
    }
    clear_actions();
}
void NativeControls::apply_mech() {
    if (!input_.active()) { release_target(target_); return; }
    frame_input_ = input_.take();
    mech_weapon_ = read(target_.component,0x120);
    mech_exit_pending_ = frame_input_.edge(Action::interact);
    apply_mech_actions(frame_input_,{enabled(control(move_control)),enabled(control(shoot_control)),
        enabled(control(second_shoot_control))},mech_actions_,
        [&](uint32_t type,float x,float y) { event(target_.component,type,x,y); });
    look_pending_ = frame_input_.dx != 0 || frame_input_.dy != 0;
    // E is consumed by the existing exit button edge in the original interface.
    // Other on-foot actions have no mech mapping and are deliberately ignored.
}
void NativeControls::apply_machinegun() {
    if (!input_.active() || !live_machinegun()) {
        // Cancel only the still-local controller and independently current
        // release receivers. A rebind does not prove old-resource teardown.
        release_target(target_); input_.clear(); return;
    }
    frame_input_ = input_.take();
    machinegun_exit_pending_ = frame_input_.edge(Action::interact);
    apply_machinegun_actions(frame_input_,enabled(control(shoot_control)),
        enabled(control(second_shoot_control)) && invoke_bool(fn("_ZN16CWeaponComponent16HasSecondaryFireEv"),{target_.machinegun.weapon}),
        machinegun_actions_,[&] { return live_machinegun(); },
        [&](uint32_t type) { event(target_.component,type); });
    look_pending_ = live_machinegun() && (frame_input_.dx != 0 || frame_input_.dy != 0);
}
void NativeControls::driver_event(InputTarget target,uint32_t type,float x,float y) {
    uint32_t data[]{type == 13 ? event_move_ : type == 14 ? event_release_ : event_generic_,type,bits(x),bits(y)};
    invoke(fn("_ZN14CPlayerControl7onEventEPK6IEventPK12EventManager"),
        {target.component+0x10,reinterpret_cast<uint32_t>(data),read(event_manager_)});
}
void NativeControls::clean_driver_pedals() {
    if (!driver_pedal_release_.pending) return;
    if (!driver_input_lifetime(driver_pedal_release_.owner,driver_pedal_release_.level)) {
        driver_pedal_release_ = {}; return;
    }
    // This is native input-timer cleanup, with all original state/configuration
    // guards. Never run physics or assign engine/brake/steering/camera state.
    driver_pedals(driver_pedal_release_.owner.driver.normal);
}
bool NativeControls::jeep_controller(InputTarget target,uint32_t previous_level) const {
    if (target.kind != TargetKind::driver || !target.component || !previous_level) return false;
    const auto level = invoke(fn(get_level));
    if (level != previous_level || invoke(fn(get_jeep),{level}) != target.component) return false;
    const auto player = invoke(fn(get_player),{level});
    return player && jeep_controller_matches(target,read(player,4),
        [](uint32_t p,uint32_t offset) { return read(p,offset); },
        [](uint32_t p,uint32_t offset) { return read<uint8_t>(p,offset); });
}
bool NativeControls::jeep_weapon(InputTarget target,uint32_t level) const {
    return jeep_controller(target,level) && jeep_weapon_matches(target,[](uint32_t p,uint32_t offset) { return read(p,offset); });
}
bool NativeControls::jeep_animation(InputTarget target,uint32_t level) const {
    return jeep_controller(target,level) && jeep_animation_matches(target,
        [](uint32_t p,uint32_t offset) { return read(p,offset); },
        [](uint32_t p,uint32_t offset) { return read<uint8_t>(p,offset); });
}
void NativeControls::apply_jeep_weapons() {
    if (!(target_.driver.capabilities & 2)) return;
    const bool secondary = live_driver() && enabled(control(second_shoot_control)) &&
        invoke_bool(fn("_ZN16CWeaponComponent16HasSecondaryFireEv"),{target_.jeep.weapon});
    apply_jeep_fire(frame_input_,true,enabled(control(shoot_control)),secondary,jeep_fire_,
        [&] { return input_.active() && live_driver(); },
        [&](uint32_t type) { driver_event(target_,type); });
}
void NativeControls::apply_driver() {
    if (!input_.active() || !live_driver()) { release_target(target_); input_.clear(); return; }
    frame_input_ = input_.take();
    driver_exit_pending_ = frame_input_.edge(Action::interact);
    offer_jeep_role_edge(jeep_role_edge_,frame_input_,target_,level_,
        [](uint32_t p,uint32_t offset) { return read(p,offset); });
    const auto move = control(move_control);
    apply_driver_actions(frame_input_,(target_.driver.capabilities & 1) && enabled(move),move ? read(move,0x88) : 0,driver_actions_,
        [&] { return live_driver(); },[&](uint32_t type,float x,float y) { driver_event(target_,type,x,y); });
    if ((target_.driver.capabilities & 1) && target_.driver.configuration == 1 &&
        ((frame_input_.down(Action::forward) && enabled(read(normal_,0x28))) ||
         (frame_input_.down(Action::back) && enabled(read(normal_,0x2c))))) {
        if (!driver_pedal_release_.pending) driver_pedal_release_ = {target_,level_,true,false};
    }
    apply_jeep_weapons();
    look_pending_ = live_driver() && (frame_input_.dx != 0 || frame_input_.dy != 0);
}
void NativeControls::cancel_normal_aim() {
    normal_aim_weapon_.store(0); normal_aim_manager_.store(0);
    normal_aim_seen_=normal_aim_epoch_.fetch_add(1)+1; normal_aim_intent_.cancel(); normal_weapon_owner_={};
}
void NativeControls::sync_normal_aim() {
    if(GetCurrentThreadId()!=normal_aim_lane_.load()) return;
    if(input_.revoke_stale_gamepad(frame_input_))
        normal_aim_intent_.sample(frame_input_.down(Action::aim));
    if(normal_pad_reload_revision_ && normal_pad_reload_revision_!=input_.gamepad_revision()) {
        normal_aim_intent_.cancel_reload(); normal_pad_reload_revision_=0;
    }
    if(normal_aim_seen_!=normal_aim_epoch_.load()) {
        cancel_normal_aim(); aim_=false; input_.clear();
    }
}
bool NativeControls::normal_aim_context(const NormalAimOwner &owner) const {
    return GetCurrentThreadId()==normal_aim_lane_.load() && owner.epoch==normal_aim_epoch_.load() &&
        (!frame_input_.gamepad_only_held.test(static_cast<size_t>(Action::aim)) ||
         frame_input_.gamepad_revision==input_.gamepad_revision()) &&
        (!normal_pad_reload_revision_ || normal_pad_reload_revision_==input_.gamepad_revision()) &&
        target_.kind==TargetKind::normal && owner.level==level_ && owner.player==player_ &&
        owner.interface_==normal_ && owner.state==state_;
}
NativeControls::NormalReloadState NativeControls::normal_reload_state(uint32_t weapon,const NormalAimOwner &owner) const {
    if(!weapon || normal_aim_weapon_.load()!=weapon || !normal_aim_context(owner)) return NormalReloadState::unknown;
    // Native accepted ADS->reload pending work blocks immediately. It is not
    // inferred from R, magazine changes, a submitted setter or a host timer.
    if(read<uint8_t>(weapon,0x62)) return NormalReloadState::busy;
    // This getter returns only the current main-slot INDEX. Unlike the unsafe
    // GetWeaponState/GetStateOnSlot path it never dereferences a state record.
    // Only the inactive SwitchAim branch calls this query. Its next original
    // CanStartAim reads the same initialized main slot on this live receiver.
    if(!read(weapon,0x10) || read<int>(weapon,0xf4)<0) return NormalReloadState::unknown;
    const auto index=static_cast<int>(invoke(fn("_ZN16CWeaponComponent21GetMainAnimationStateEv"),{weapon}));
    if(normal_aim_weapon_.load()!=weapon || !normal_aim_context(owner) || index<0) return NormalReloadState::unknown;
    return native_reload_blocks_aim(read<uint8_t>(weapon,0x62)!=0,index,read<int>(weapon,0xa0))
        ? NormalReloadState::busy : NormalReloadState::free;
}
void NativeControls::invalidate_normal_weapon(uint32_t weapon) {
    auto expected=weapon;
    if(weapon && normal_aim_weapon_.compare_exchange_strong(expected,0)) normal_aim_epoch_.fetch_add(1);
}
uint32_t __cdecl NativeControls::normal_switch(uint32_t manager) {
    auto &s=*instance_; auto boundary=normal_input_boundary;
    if(normal_input_depth!=1 || GetCurrentThreadId()!=s.normal_aim_lane_.load()) return invoke(s.normal_switch_call_->original(),{manager});
    if(!boundary || boundary->previous || boundary->interface_!=s.normal_ ||
       !s.normal_aim_context({boundary->level,boundary->player,boundary->interface_,boundary->state,boundary->epoch}))
        return invoke(s.normal_switch_call_->original(),{manager});
    // First physical native CALL keeps its tutorial side effects. A stable held
    // button does not manufacture release frames to rearm the native toggle.
    if(boundary->attempted) return 0; // This checked native caller ignores EAX.
    boundary->attempted=true;
    if(!s.input_.active()) s.normal_aim_intent_.cancel_input();
    NormalAimDispatch scope{manager,s.level_,s.player_,s.normal_,s.state_,s.normal_aim_epoch_.load()};
    auto previous=normal_aim_dispatch; normal_aim_dispatch=&scope;
    const auto result=invoke(s.normal_switch_call_->original(),{manager});
    normal_aim_dispatch=previous;
    if(s.normal_aim_context({scope.level,scope.player,scope.interface_,scope.state,scope.epoch}) &&
       s.input_.active() && s.normal_aim_intent_.reload_pending()) {
        if(scope.reload_ready && s.enabled(read(s.normal_,0x4c))) {
            // Transfer once to original native reload, with opaque outcome.
            // A tap can wait for observed ADS entry, never an arbitrary delay.
            s.normal_aim_intent_.take_reload();
            invoke(s.fn("_Z25HudPressedNoSweepCallbackP15CButtonHudSweep"),{read(s.normal_,0x4c)});
        } else if(!scope.reload_waiting) {
            // No attested transition owns this refusal. Do not leave an
            // unbounded surprise reload waiting for an unrelated later gate.
            s.normal_aim_intent_.cancel_reload();
        }
    }
    return result;
}
uint32_t __cdecl NativeControls::normal_switch_entry(uint32_t manager) {
    auto &s=*instance_; auto scope=normal_aim_dispatch;
    const bool previous=normal_switch_authorized;
    normal_switch_authorized=normal_input_depth==1 && !normal_switch_depth && scope && scope->manager==manager &&
        s.normal_aim_context({scope->level,scope->player,scope->interface_,scope->state,scope->epoch});
    ++normal_switch_depth;
    const auto result=invoke(s.normal_switch_hook_->original(),{manager});
    --normal_switch_depth; normal_switch_authorized=previous; return result;
}
uint32_t __cdecl NativeControls::normal_can_aim(uint32_t weapon) {
    auto &s=*instance_; auto scope=normal_aim_dispatch;
    const bool local=normal_input_depth==1 && normal_switch_authorized && normal_switch_depth==1 && scope;
    if(!local) return invoke(s.normal_can_aim_call_->original(),{weapon});
    if(!weapon || !s.normal_aim_context({scope->level,scope->player,scope->interface_,scope->state,scope->epoch})) return 0;
    // Bind before either guest query so first-receiver lifetime callbacks can
    // invalidate the token. Never call the original on an invalidated receiver.
    s.normal_aim_weapon_.store(weapon); s.normal_aim_manager_.store(scope->manager);
    s.normal_weapon_owner_={scope->level,scope->player,scope->interface_,scope->state,scope->epoch};
    const auto current=[&] {
        return s.normal_aim_weapon_.load()==weapon && s.normal_aim_manager_.load()==scope->manager &&
            s.normal_aim_context({scope->level,scope->player,scope->interface_,scope->state,scope->epoch});
    };
    const auto aim_button=s.control(aim_control);
    if(!current()) return 0; // Host request cancelled before original admission.
    const auto reload=s.normal_reload_state(weapon,{scope->level,scope->player,scope->interface_,scope->state,scope->epoch});
    if(!current() || reload!=NormalReloadState::free) return 0;
    // Native admission is LAST: the added HUD lookup can change native gates
    // even without changing identity, epoch or entry flags. No guest call may
    // intervene between this original AL result and the scalar decision.
    const auto result=invoke(s.normal_can_aim_call_->original(),{weapon});
    if(!current()) return result&~0xffu;
    const bool hud_enabled=s.enabled(aim_button);
    const bool enabled=read<uint8_t>(weapon,0xd)!=0;
    const bool submitted=read<uint8_t>(weapon,0x60)!=0;
    scope->reload_ready=enabled && !submitted; scope->reload_waiting=submitted;
    if(!s.input_.active()) s.normal_aim_intent_.cancel_input();
    const bool allow=s.normal_aim_intent_.permit_start((result&0xff)!=0,submitted,enabled,hud_enabled);
    // Only our directional request can mask AL. Native/foreign calls retain
    // their complete original result; the native caller consumes AL itself.
    return allow ? result : result&~0xffu;
}
uint32_t __cdecl NativeControls::normal_native_unaim(uint32_t weapon,uint32_t animated) {
    auto &s=*instance_; auto scope=normal_aim_dispatch;
    const bool local=normal_switch_authorized && normal_switch_depth==1 && scope;
    if(!local) return invoke(s.normal_unaim_call_->original(),{weapon,animated});
    const NormalAimOwner owner{scope->level,scope->player,scope->interface_,scope->state,scope->epoch};
    if(!weapon || !s.normal_aim_context(owner)) return 0;
    s.normal_aim_weapon_.store(weapon); s.normal_aim_manager_.store(scope->manager);
    s.normal_weapon_owner_={scope->level,scope->player,scope->interface_,scope->state,scope->epoch};
    const bool enabled=read<uint8_t>(weapon,0xd)!=0;
    scope->reload_ready=enabled;
    if(!s.input_.active()) s.normal_aim_intent_.cancel_input();
    if(enabled && s.normal_aim_intent_.permit_exit())
        return invoke(s.normal_unaim_call_->original(),{weapon,animated});
    // Preserve C04's directional exit. StartReload owns its direct animated
    // UnAim before publishing pending reload. An extra UnAim here would replace
    // the current native reload animation with the configured ADS exit state.
    return 0;
}
uint32_t __cdecl NativeControls::normal_reload_restore_aim(uint32_t weapon) {
    auto &s=*instance_;
    if(GetCurrentThreadId()==s.normal_aim_lane_.load() && weapon && s.normal_aim_weapon_.load()==weapon &&
       s.normal_aim_context(s.normal_weapon_owner_)) {
        // This exact native OnEvent CALL follows DoReload and precedes pending62
        // clear. It is an automatic restore, not a new physical aim request.
        // Defer to the accepted held-input reconciler after actual reload exit;
        // never start ADS even transiently while reload still owns this event.
        // OnEvent ignores this return and still clears its own pending byte.
        return 0;
    }
    return invoke(s.normal_reload_restore_->original(),{weapon});
}
uint32_t __cdecl NativeControls::normal_select_weapon(uint32_t manager,uint32_t index,uint32_t flags) {
    auto &s=*instance_;
    if(manager && manager==s.normal_aim_manager_.load()) {
        s.normal_aim_weapon_.store(0); s.normal_aim_epoch_.fetch_add(1);
    }
    return invoke(s.normal_select_->original(),{manager,index,flags});
}
uint32_t __cdecl NativeControls::normal_ctor1(uint32_t weapon,uint32_t actor) {
    auto &s=*instance_; s.invalidate_normal_weapon(weapon); return invoke(s.normal_ctor1_->original(),{weapon,actor});
}
uint32_t __cdecl NativeControls::normal_ctor2(uint32_t weapon,uint32_t actor) {
    auto &s=*instance_; s.invalidate_normal_weapon(weapon); return invoke(s.normal_ctor2_->original(),{weapon,actor});
}
uint32_t __cdecl NativeControls::normal_reset(uint32_t weapon) {
    auto &s=*instance_; s.invalidate_normal_weapon(weapon); return invoke(s.normal_reset_->original(),{weapon});
}
uint32_t __cdecl NativeControls::normal_reinit(uint32_t weapon) {
    auto &s=*instance_; s.invalidate_normal_weapon(weapon); return invoke(s.normal_reinit_->original(),{weapon});
}
uint32_t __cdecl NativeControls::normal_dtor0(uint32_t weapon) {
    auto &s=*instance_; s.invalidate_normal_weapon(weapon); return invoke(s.normal_dtor0_->original(),{weapon});
}
uint32_t __cdecl NativeControls::normal_dtor1(uint32_t weapon) {
    auto &s=*instance_; s.invalidate_normal_weapon(weapon); return invoke(s.normal_dtor1_->original(),{weapon});
}
uint32_t __cdecl NativeControls::normal_dtor2(uint32_t weapon) {
    auto &s=*instance_; s.invalidate_normal_weapon(weapon); return invoke(s.normal_dtor2_->original(),{weapon});
}
void NativeControls::apply(uint32_t player) {
    refresh();
    sync_normal_aim();
    if (target_.kind == TargetKind::bike) return; // Its dedicated native consumer owns input sampling.
    if (target_.kind == TargetKind::mech) { apply_mech(); return; }
    if (target_.kind == TargetKind::machinegun) { apply_machinegun(); return; }
    if (target_.kind == TargetKind::driver) { apply_driver(); return; }
    bool active = input_.active() && target_.kind == TargetKind::normal && player == player_;
    auto input = active ? input_.take() : FrameInput{};
    frame_input_ = input;
    auto weapons = read(player,0x170);
    if (!active) {
        release_actions(player);
        return;
    }
    if (!input_.config.touch_autoaim && !aim_override_) {
        if (!lua_) lua_ = invoke(fn("luaL_newstate"));
        if (!lua_) throw std::runtime_error("Native controls Lua option context allocation failed");
        invoke(fn("_Z17Menu_GetAimAssistP9lua_State"),{lua_});
        saved_aim_ = invoke(fn("lua_toboolean"),{lua_,1});
        invoke(fn("lua_settop"),{lua_,0});
        invoke(fn("lua_pushboolean"),{lua_,0});
        invoke(fn("_Z17Menu_SetAimAssistP9lua_State"),{lua_});
        aim_override_ = true;
        if (!aim_reported_) {
            invoke(fn("lua_settop"),{lua_,0});
            invoke(fn("_Z17Menu_GetAimAssistP9lua_State"),{lua_});
            auto effective = invoke(fn("lua_toboolean"),{lua_,1});
            invoke(fn("lua_settop"),{lua_,0});
            std::printf("native-autoaim saved=%u effective=%u\n",saved_aim_,effective);
            if (effective) throw std::runtime_error("Aim-assist option override failed");
        }
    }
    queue_normal_keyboard_movement(input,moving_,observed_,
        [&] { return enabled(control(move_control)); },
        [&](uint32_t type,float x,float y) { event(player,type,x,y); });
    // Queue counts here; consume them after all scheme/touch processing, at the
    // player's own input boundary. Earlier actor rotation was overwritten.
    look_pending_ = input.dx != 0 || input.dy != 0;
    bool fire = input.requested(Action::fire) && enabled(control(shoot_control));
    if (fire != fire_) {
        event(player,fire ? 29 : 30);
        fire_ = fire; observed_ = true;
        std::printf("native-action frame=%llu action=fire down=%d\n",frame_,fire); }
    normal_aim_intent_.sample(input.down(Action::aim));
    normal_aim_intent_.offer_reload(input.edge(Action::reload));
    if(input.edge(Action::reload))
        normal_pad_reload_revision_=input.gamepad_edge(Action::reload) ? input.gamepad_revision : 0;
    normal_aim_manager_.store(weapons);
    bool aim = input.requested(Action::aim) && enabled(control(aim_control));
    if (aim != aim_ && weapons) {
        // Press is consumed by the normal interface (SwitchAim and tutorial flags).
        // Release is retained until the live native Update confirms active ADS.
        aim_ = aim; observed_ = true;
        std::printf("native-action frame=%llu action=aim down=%d\n",frame_,aim);
    }
    for (auto a : input.pressed) {
        if(input.gamepad_edge(a) && input.gamepad_revision!=input_.gamepad_revision()) continue;
        // Interact can synchronously transfer native ownership to the mech.
        if (!read<uint8_t>(player,0xd)) break;
        std::printf("native-action frame=%llu action=%s\n",frame_,action_name(a));
        observed_ = true;
        switch(a) {
        case Action::jump:
            // The native IsActive query below lets the interface set its tutorial flags.
            break;
        case Action::reload:
            // Original callback above; only a native ADS-startup edge is deferred.
            break;
        case Action::grenade:
            // A short click throws on release; a hold feeds the native detonator.
            break;
        case Action::power:
            // The scheme consumes the native Pressed edge and calls its action callback.
            if(!enabled(control(powers_control)) && enabled(read(normal_,0x5c)) && read<int32_t>(read(normal_,0x5c),0xa0)==0)
                invoke(fn("_Z22PressedNoSweepCallbackP12CButtonSweep"),{read(normal_,0x5c)});
            break;
        case Action::previous_power: case Action::next_power: case Action::next_grenade: {
            const bool hud=a==Action::next_grenade;
            auto button=hud ? read(normal_,0x70) : control(powers_control);
            // Online disables the campaign selector; the visible +0x5c sweep
            // owns consumables instead. Preserve both widgets' native gates.
            if(!hud && !enabled(button) && enabled(read(normal_,0x5c))) button=read(normal_,0x5c);
            if(!hud && button==read(normal_,0x5c) && enabled(button)) {
                animate_power_selection(button,a==Action::previous_power?-1:1);
                break;
            }
            if(enabled(button)) cycle_power_selection(button,hud,a==Action::previous_power ? -1 : 1,
                fn(hud ? "_ZN15CButtonHudSweep19SetCurrentSelectionEi" : "_ZN12CButtonSweep19SetCurrentSelectionEi"),
                fn(hud ? "_Z16HudSweepCallbackP15CButtonHudSweepi" : "_Z13SweepCallbackP12CButtonSweepi"));
            break;
        }
        case Action::melee: invoke(fn("_ZN15PlayerComponent13UseMeleePowerEv"),{player}); break;
        case Action::interact: invoke(fn("_ZN15PlayerComponent8InteractEv"),{player}); break;
        case Action::next: case Action::previous:
            cancel_normal_aim();
            normal_aim_intent_.block_until_release();
            if (weapons) invoke(fn(a == Action::next ? "_ZN14CWeaponManager13SetNextWeaponEb" :
                                   "_ZN14CWeaponManager13SetPrevWeaponEb"),{weapons,1});
            break;
        default:
            if (a >= Action::slot1 && a <= Action::slot5 && weapons) {
                cancel_normal_aim();
                normal_aim_intent_.block_until_release();
                uint32_t slot = static_cast<uint32_t>(a) - static_cast<uint32_t>(Action::slot1);
                auto begin = read(weapons,0x154), end = read(weapons,0x158);
                if (end < begin || end-begin > 4096 || (end-begin)%4)
                    throw std::runtime_error("Native weapon slot layout mismatch");
                auto inventory = read(weapons,0x148), inventory_end = read(weapons,0x14c);
                if (inventory_end < inventory || inventory_end-inventory > 24576 || (inventory_end-inventory)%24)
                    throw std::runtime_error("Native weapon inventory layout mismatch");
                std::vector<uint32_t> ids;
                for (auto entry = inventory; entry < inventory_end; entry += 24) ids.push_back(read(entry));
                auto resolve = [&](uint32_t id) {
                    return static_cast<int>(invoke(fn("_ZN14CWeaponManager18GetWeaponIndexByIdEi"),{weapons,id}));
                };
                auto available = [&](int index) {
                    return index >= 0 && static_cast<uint32_t>(index) < ids.size() &&
                        read(inventory,static_cast<uint32_t>(index)*24+0xc) &&
                        invoke_bool(fn("_ZN14CWeaponManager11IsAvailableEi"),{weapons,static_cast<uint32_t>(index)});
                };
                auto slots = available_weapon_slots(
                    std::span<const uint32_t>(reinterpret_cast<const uint32_t *>(begin),(end-begin)/4),ids,resolve,available);
                std::printf("native-slot-map");
                for (size_t i=0; i<slots.size(); ++i) std::printf(" %zu:%u/%d",i+1,slots[i],resolve(slots[i]));
                std::printf("\n");
                auto result = select_weapon_slot(slots,slot,resolve,available,
                    [&](int index) { invoke(fn("_ZN14CWeaponManager16SetCurrentWeaponEii"),{weapons,static_cast<uint32_t>(index),0}); });
                std::printf("native-slot slot=%u id=%u inventory_index=%d result=%s current=%d\n",
                    slot+1,result.id,result.index,result.status == SlotStatus::selected ? "requested" :
                    result.status == SlotStatus::absent ? "absent" : "unavailable",read<int>(weapons,0x160));
            }
            break;
        }
    }
    detonate_pending_=false;
    if(!input_.active() || !read<uint8_t>(player,0xd)) { grenade_gesture_.cancel();return; }
    if(!enabled(read(normal_,0x70)) && !enabled(read(normal_,0x68))) { grenade_gesture_.cancel();return; }
    if(input.edge(Action::grenade))
        grenade_gamepad_revision_=input.gamepad_edge(Action::grenade) ? input.gamepad_revision : 0;
    if(grenade_gamepad_revision_ && grenade_gamepad_revision_!=input_.gamepad_revision()) {
        grenade_gesture_.cancel(); grenade_gamepad_revision_=0; return;
    }
    grenade_epoch_=input_.edge_epoch();
    const auto gesture=grenade_gesture_.sample(input.down(Action::grenade),input.edge(Action::grenade),
        SDL_GetTicks(),grenade_epoch_);
    if(gesture==GrenadeCommand::throw_grenade && enabled(read(normal_,0x70)))
        invoke(fn("_Z25HudPressedNoSweepCallbackP15CButtonHudSweep"),{read(normal_,0x70)});
    else if(gesture==GrenadeCommand::detonate) detonate_pending_=enabled(read(normal_,0x68));
}
void NativeControls::sample(const char *reason) {
    sample_native_controls({image_,player_,normal_,scheme_,
        fn("_ZN13CPowerManager21GetNormalPowerCrtAmmoEi"),fn("_ZN13CPowerManager11CanUsePowerEb"),
        fn("_ZN13CPowerManager18GetPowerTypeOnSlotEi"),frame_,rush_heading_},reason);
}
void __cdecl NativeControls::update(uint32_t interface_,uint32_t dt) {
    auto &s=*instance_;
    NativeInputInvocation invocation(normal_input_depth);
    // The guard already covers pre-apply and post-original guest refreshes.
    // Nested/foreign calls forward original behavior without host sampling or
    // acquiring an action scope, including while no boundary is published yet.
    if(!invocation.outer() || GetCurrentThreadId()!=s.normal_aim_lane_.load()) {
        invoke(s.update_->original(),{interface_,dt}); return;
    }
    const auto component=read(interface_,8);
    if(component) s.apply(component);
    NormalInputBoundary boundary{nullptr,interface_,s.level_,s.player_,s.state_,s.normal_aim_epoch_.load()};
    {
        NormalBoundaryScope scope(boundary);
        jeep_role_input_boundary(s.jeep_role_edge_,[&] { invoke(s.update_->original(),{interface_,dt}); });
    }
    s.detonate_pending_=false; // An unconsumed gesture expires with this native frame.
    // Reacquire AFTER original input, while the entire-invocation guard still
    // prevents guest reentry during refresh from becoming another outer update.
    s.refresh(); s.sync_normal_aim();
    if(boundary.attempted || interface_!=s.normal_ ||
       !s.normal_aim_context({boundary.level,boundary.player,boundary.interface_,boundary.state,boundary.epoch})) return;
    const auto manager=read(s.player_,0x170);
    if(!manager) return;
    NormalBoundaryScope scope(boundary);
    normal_switch(manager);
}
void __cdecl NativeControls::player_input(uint32_t player,uint32_t dt) {
    auto &s = *instance_;
    mouse_look_boundary(s.look_pending_,[&] {
        return s.input_.active() && look_target_matches(s.target_,TargetKind::normal,player,read<uint8_t>(player,0xd) != 0) &&
            s.enabled(s.control(rotate_control)) && !read<uint8_t>(player,0x139);
    },[&] {
        auto object = read(player,4);
        auto before_yaw = read<float>(object,0x68), before_pitch = read<float>(object,0x60);
        float limit = static_cast<float>(read<int>(read(player,0xfc),4));
        const auto aim=current_native_aim_scale(player,
            [](uint32_t p,uint32_t offset) { return read(p,offset); },
            [](uint32_t p,uint32_t offset) { return read<uint8_t>(p,offset); },
            [](uint32_t p,uint32_t offset) { return read<float>(p,offset); });
        // Restore the original upstream aim factor to the PC absolute queue.
        const auto config=s.input_.look_config(aim.factor,aim.aiming);
        auto delta = look_delta(s.frame_input_.dx,s.frame_input_.dy,before_pitch,limit,config);
        queue_mouse_look(*reinterpret_cast<GuestLookInput *>(player+0x48),before_yaw,before_pitch,delta);
        if (s.trace_.look_begin(s.frame_,s.frame_input_,delta,limit,before_yaw,before_pitch))
            s.observed_ = true;
    },[&] {
        invoke(s.player_input_->original(),{player,dt});
    },[&] {
        auto object = read(player,4);
        s.trace_.look_end(s.frame_,read<float>(object,0x68),read<float>(object,0x60));
    });
}
void __cdecl NativeControls::mech_input(uint32_t player,uint32_t dt) {
    auto &s = *instance_;
    mouse_look_boundary(s.look_pending_,[&] {
        return s.input_.active() && look_target_matches(s.target_,TargetKind::mech,player,read<uint8_t>(player,0xd) != 0) &&
            read(player,0x1bc) != 3 &&
            s.enabled(s.control(rotate_control)) && !read<uint8_t>(player,0x1cd);
    },[&] {
        auto actor = read(player,4);
        const float yaw = read<float>(actor,0x68), pitch = read<float>(actor,0x60);
        // The mech consumer imposes its own +/-20-degree range, not the normal
        // player's template limit. The guest still performs the final clamp.
        auto delta = look_delta(s.frame_input_.dx,s.frame_input_.dy,pitch,20,s.input_.look_config());
        queue_mouse_look(*reinterpret_cast<GuestLookInput *>(player+0x48),yaw,pitch,delta);
        s.trace_.look_begin(s.frame_,s.frame_input_,delta,20,yaw,pitch);
    },[&] { invoke(s.mech_input_->original(),{player,dt}); },[&] {
        auto actor = read(player,4);
        s.trace_.look_end(s.frame_,read<float>(actor,0x68),read<float>(actor,0x60));
    });
}
void __cdecl NativeControls::mech_stop(uint32_t weapon,uint32_t channel,uint32_t messages) {
    auto &s = *instance_;
    observe_mech_stop(s.mech_actions_,s.mech_weapon_,weapon,channel,[&] {
        invoke(s.fn("_ZN16CWeaponComponent8FireStopEib"),{weapon,channel,messages});
    });
}
void __cdecl NativeControls::machinegun_update(uint32_t player,uint32_t dt) {
    auto &s = *instance_;
    const bool selected = s.target_.kind == TargetKind::machinegun && s.target_.component == player;
    machinegun_action_boundary(selected,selected && s.input_.active() && s.live_machinegun(),[&] {
        s.release_target(s.target_); s.input_.clear();
    },[&] { invoke(s.machinegun_update_->original(),{player,dt}); });
}
uint32_t __cdecl NativeControls::bike_on_event(uint32_t control_,uint32_t event_,uint32_t manager) {
    auto &s=*instance_; ++bike_event_depth;
    s.gamepad_.native_event(control_,event_);
    const bool tracked=control_==s.bike_writers_.receiver.load();
    const auto type=tracked ? read(event_,4) : 64u;
    const auto key=tracked && type==1 ? read(event_,8) : 0u;
    const bool host=tracked && claim_bike_host_event(bike_host_event,control_,event_,bike_event_depth); // Nested native/reentrant emissions are never host-tagged.
    const bool foreign=tracked && GetCurrentThreadId()!=s.bike_writers_.lane.load();
    uint32_t result=0;
    try {
        result=observe_bike_writer(s.bike_writers_,tracked,host,foreign,type,key,
            [&] { return invoke(s.bike_events_->original(),{control_,event_,manager}); },
            [&] { return control_==s.bike_writers_.receiver; },[&](uint32_t move) {
                const auto value=read(control_,0xc);
                bike_carry_readback(s.bike_actions_,move,value); bike_carry_readback(s.bike_release_.actions,move,value);
            });
    } catch(...) { --bike_event_depth; throw; }
    --bike_event_depth; return result;
}
void __cdecl NativeControls::bike_reset_control(uint32_t control_) {
    auto &s=*instance_;
    s.gamepad_.native_reset(control_);
    observe_bike_reset(s.bike_writers_,control_==s.bike_writers_.receiver.load(),
        GetCurrentThreadId()!=s.bike_writers_.lane,false,[&] { invoke(s.bike_reset_input_->original(),{control_}); });
}
void __cdecl NativeControls::bike_reset_controls(uint32_t control_) {
    auto &s=*instance_;
    s.gamepad_.native_reset(control_);
    observe_bike_reset(s.bike_writers_,control_==s.bike_writers_.receiver.load(),
        GetCurrentThreadId()!=s.bike_writers_.lane,true,[&] { invoke(s.bike_reset_inputs_->original(),{control_}); });
}
void __cdecl NativeControls::bike_base(uint32_t base,uint32_t dt) {
    auto &s=*instance_;
    invoke(s.bike_base_->original(),{base,dt}); // Native health/state changes happen first.
    if(s.target_.kind==TargetKind::bike && s.target_.bike.base==base) s.apply_bike();
}
void __cdecl NativeControls::bike_primary(uint32_t visual,uint32_t quaternion) {
    auto &s=*instance_;
    invoke(s.bike_primary_->original(),{visual,quaternion}); // Always retain native visual work.
    s.synchronize_bike_origins(); s.clean_bike_release();
    const auto owner=s.target_; const auto level=s.level_;
    if(owner.kind!=TargetKind::bike || owner.bike.visual!=visual) return;
    validate_bike_primary(s.bike_actions_,[&] { return s.input_.active() && s.live_bike(); },
        [&] { return s.enabled(s.control(shoot_control)); },[&] {
            const bool valid=s.bike_retained(owner,level) && read(owner.component,0x120)==0 && s.bike_root(owner,level);
            s.synchronize_bike_origins();
            if(valid) validate_bike_raw(s.bike_actions_,[&](uint32_t offset) {
                return offset==0xc ? read(owner.component,0x1c) : uint32_t(read<uint8_t>(owner.component,0x10+offset));
            });
            return valid && !s.bike_writers_.foreign;
        },
        [&](uint32_t type,float x) { return s.bike_event(owner,type,x,&s.bike_actions_); },[&] {
            const bool valid=!s.bike_writers_.foreign && s.bike_retained(owner,level);
            s.synchronize_bike_origins();
            return valid && !s.bike_writers_.foreign && s.bike_actions_.primary &&
                !read<uint8_t>(owner.component,0x30) && !read<uint8_t>(owner.component,0x31);
        });
    if(s.bike_actions_.primary && (!s.input_.active() || !s.live_bike())) s.queue_bike_release(owner,level);
}
void __cdecl NativeControls::bike_enable(uint32_t c,uint32_t enabled_) {
    auto &s=*instance_;
    if(bike_enable_transition(read<uint8_t>(c,0xd)!=0,(enabled_&0xff)!=0)) s.bike_transition(c);
    invoke(s.bike_enable_->original(),{c,enabled_});
    if(enabled_&0xff) s.clean_bike_release();
}
void __cdecl NativeControls::bike_state(uint32_t c,uint32_t state) {
    auto &s=*instance_;
    if(bike_state_transition(read(c,0x120),state)) s.bike_transition(c);
    invoke(s.bike_state_->original(),{c,state});
}
void __cdecl NativeControls::bike_road(uint32_t c,uint32_t road) {
    auto &s=*instance_; s.bike_transition(c); invoke(s.bike_road_->original(),{c,road});
}
void __cdecl NativeControls::bike_configure(uint32_t c,uint32_t config) {
    auto &s=*instance_; s.bike_transition(c); invoke(s.bike_configure_->original(),{c,config});
}
void __cdecl NativeControls::bike_move_to(uint32_t c,uint32_t position,uint32_t direction,uint32_t animation) {
    auto &s=*instance_; s.bike_transition(c); invoke(s.bike_move_to_->original(),{c,position,direction,animation});
}
void __cdecl NativeControls::bike_reset(uint32_t c) {
    auto &s=*instance_; s.bike_transition(c); invoke(s.bike_reset_->original(),{c});
}
void __cdecl NativeControls::bike_load(uint32_t c,uint32_t stream) {
    auto &s=*instance_; s.bike_transition(c); invoke(s.bike_load_->original(),{c,stream});
}
void __cdecl NativeControls::machinegun_input(uint32_t player,uint32_t dt) {
    auto &s = *instance_;
    const auto owner = s.target_;
    bool traced = false;
    mouse_look_boundary(s.look_pending_,[&] {
        return s.input_.active() && look_target_matches(s.target_,TargetKind::machinegun,player,true) && s.live_machinegun() &&
            s.enabled(s.control(rotate_control)) &&
            machinegun_look_available(read<int>(player,0x1fc),read<int>(player,0xf8),static_cast<int>(dt));
    },[&] {
        const auto body = read(player,0x11c), limits = read(body,0x24);
        const float yaw = read<float>(body,0x54), pitch = read<float>(body,0x58);
        const auto delta = machinegun_mouse_delta(s.frame_input_.dx,s.frame_input_.dy,pitch,
            read<float>(limits,0x14),read<float>(limits,0x18),s.input_.look_config());
        queue_mouse_look(*reinterpret_cast<GuestLookInput *>(player+0x48),yaw,pitch,delta);
        traced = s.trace_.look_begin(s.frame_,s.frame_input_,delta,read<float>(limits,0x18),yaw,pitch);
    },[&] { invoke(s.machinegun_input_->original(),{player,dt}); },[&] {
        if (!traced || s.target_ != owner || !s.live_machinegun()) return;
        const auto body = read(player,0x11c);
        s.trace_.look_end(s.frame_,read<float>(body,0x54),read<float>(body,0x58));
    });
}
void __cdecl NativeControls::jeep_update(uint32_t player,uint32_t dt) {
    auto &s = *instance_;
    const bool selected = s.target_.kind == TargetKind::driver && s.target_.component == player;
    const bool eligible = selected && s.input_.active() && s.live_driver();
    const bool secondary = !selected || !s.jeep_fire_.weapon_owned[1] ||
        (s.jeep_weapon(s.target_,s.level_) && invoke_bool(s.fn("_ZN16CWeaponComponent16HasSecondaryFireEv"),{s.target_.jeep.weapon}));
    jeep_fire_boundary(selected,eligible,secondary,s.jeep_fire_,
        [&] { s.release_target(s.target_); s.input_.clear(); },[&] { s.apply_jeep_weapons(); },
        [&] { camera::NativeWorldFov::JeepUpdateScope lens_scope(player,dt);
              invoke(s.jeep_update_->original(),{player,dt}); });
}
void __cdecl NativeControls::jeep_stop(uint32_t weapon,uint32_t channel,uint32_t messages) {
    auto &s = *instance_;
    const auto owner = s.target_; const auto level = s.level_;
    observe_jeep_release(s.jeep_fire_,channel,false,[&] {
        invoke(s.fn("_ZN16CWeaponComponent8FireStopEib"),{weapon,channel,messages});
    },[&] { return s.target_ == owner && owner.jeep.weapon == weapon && s.jeep_weapon(owner,level); });
}
void NativeControls::jeep_animation_stop(uint32_t vehicle,unsigned channel) {
    const auto owner = target_; const auto level = level_;
    observe_jeep_release(jeep_fire_,channel,true,[&] {
        invoke(fn("_ZN11CVehicleCar15StopShooterFireEv"),{vehicle});
    },[&] { return target_ == owner && owner.driver.vehicle == vehicle && jeep_animation(owner,level); });
}
void __cdecl NativeControls::jeep_primary_animation(uint32_t vehicle) { instance_->jeep_animation_stop(vehicle,0); }
void __cdecl NativeControls::jeep_secondary_animation(uint32_t vehicle) { instance_->jeep_animation_stop(vehicle,1); }
void __cdecl NativeControls::driver_camera(uint32_t player,uint32_t dt) {
    auto &s = *instance_;
    const auto owner = s.target_;
    const auto previous_level = s.level_;
    const bool eligible = s.look_pending_ && s.input_.active() && owner.kind == TargetKind::driver &&
        owner.component == player && s.live_driver() && s.enabled(s.control(rotate_control));
    std::optional<Look> sample;
    bool native_clear = false;
    if (eligible) {
        const auto input_template = owner.driver.input_template;
        sample = driver_mouse_input(s.frame_input_.dx,s.frame_input_.dy,read<float>(input_template,0x2c),
            read<float>(input_template,0x30),read<uint8_t>(input_template,0x24) != 0,
            read<float>(input_template,0x28),s.input_.look_config());
        native_clear = read<uint8_t>(player,0xd4) != 0;
    }
    driver_mouse_boundary(s.look_pending_,eligible,sample,[&] {
        return !native_clear && s.driver_camera_lifetime(owner,previous_level) && !read<uint8_t>(player,0xd4);
    },[&] { return Look{read<float>(player,0x14),read<float>(player,0x18)}; },
    [&](Look raw) {
        *reinterpret_cast<float *>(player+0x14) = raw.yaw;
        *reinterpret_cast<float *>(player+0x18) = raw.pitch;
    },[&] { invoke(s.driver_camera_->original(),{player,dt}); });
}
void __cdecl NativeControls::driver_pedals(uint32_t interface_) {
    auto &s = *instance_;
    const auto owner = s.driver_pedal_release_.owner;
    const auto level = s.driver_pedal_release_.level;
    driver_pedal_boundary(s.driver_pedal_release_,[&] {
        return owner && interface_ == owner.driver.normal && s.driver_input_lifetime(owner,level);
    },[&] { invoke(s.driver_pedals_->original(),{interface_}); },
    [&] { return read<int>(interface_,0x30) == 0 && read<int>(interface_,0x34) == 0; });
}
void NativeControls::driver_touch_draw(uint32_t drawing,uint32_t alpha,uint32_t original) {
    if (target_.kind==TargetKind::bike && draw_scope() &&
        bike_touch(drawing,control(move_control),control(shoot_control)) && live_bike()) {
        // These native Draw bodies also prepare joystick positions/fades.
        // Alpha zero hides their artwork while preserving every native effect.
        draw_bike_artwork(true,alpha,[&](uint32_t opacity) { invoke(original,{drawing,opacity}); });
        return;
    }
    if (mounted_machinegun(target_) && draw_scope() &&
        mounted_machinegun_touch(drawing,control(move_control),control(rotate_control),
            control(shoot_control),control(second_shoot_control))) return;
    if (target_.kind == TargetKind::driver && draw_scope() &&
        jeep_touch_control(target_,drawing,control(move_control),control(rotate_control),
            control(shoot_control),control(second_shoot_control))) return;
    invoke(original,{drawing,alpha});
}
void __cdecl NativeControls::analog_draw(uint32_t drawing,uint32_t alpha) {
    instance_->driver_touch_draw(drawing,alpha,instance_->analog_draw_->original());
}
void __cdecl NativeControls::mobile_draw(uint32_t drawing,uint32_t alpha) {
    instance_->driver_touch_draw(drawing,alpha,instance_->mobile_draw_->original());
}
void __cdecl NativeControls::angular_draw(uint32_t drawing,uint32_t alpha) {
    instance_->driver_touch_draw(drawing,alpha,instance_->angular_draw_->original());
}
void __cdecl NativeControls::machinegun_stop(uint32_t weapon,uint32_t channel,uint32_t messages) {
    auto &s = *instance_;
    const auto owner = s.target_; const auto level = s.level_;
    const bool expected = channel == 0 && owner.machinegun.weapon == weapon && s.machinegun_weapon(owner,level);
    observe_owned_machinegun_release(s.machinegun_actions_.weapon_owned,expected,[&] {
        invoke(s.fn("_ZN16CWeaponComponent8FireStopEib"),{weapon,channel,messages});
    },[&] { return s.target_ == owner && s.machinegun_weapon(owner,level); });
}
void __cdecl NativeControls::machinegun_release(uint32_t manager,uint32_t channel) {
    auto &s = *instance_;
    const auto owner = s.target_; const auto level = s.level_;
    const bool expected = channel == 0 && owner.machinegun.manager == manager && s.machinegun_manager(owner,level);
    observe_owned_machinegun_release(s.machinegun_actions_.manager_owned,expected,[&] {
        invoke(s.fn("_ZN14CWeaponManager17FireButtonReleaseEi"),{manager,channel});
    },[&] { return s.target_ == owner && s.machinegun_manager(owner,level); });
}
void __cdecl NativeControls::velocity(uint32_t player,uint32_t dt) {
    auto &s = *instance_;
    float heading = 0;
    if (s.input_.active() && s.target_.kind == TargetKind::normal && player == s.player_ && read<uint8_t>(player,0xd) && read(player,0x130) == 0 &&
        read<uint8_t>(player,0x32c) && s.enabled(s.control(move_control)) && s.enabled(s.control(rush_control)))
        heading = s.normal_aim_intent_.wanted() ? 0 : sprint_heading(s.frame_input_);
    // Provisional PC directional adaptation, authorized by the T07 play-test
    // feedback: native rush discards lateral axes and runs along actor forward.
    // Supply forward input in a temporary diagonal basis, preserving all native
    // rush eligibility, animation, stamina and velocity calculations.
    s.rush_heading_ = heading;
    if (heading) s.event(player,13,0,-1);
    auto object = read(player,4);
    with_movement_heading(heading,[&] { return read<float>(object,0x68); },
        [&](float delta) { invoke(s.fn("_ZN11CGameObject9RotateYawEf"),{object,bits(delta)}); },
        [&] { invoke(s.velocity_->original(),{player,dt}); });
}
void __cdecl NativeControls::scheme_draw(uint32_t scheme,uint32_t alpha) {
    auto &s = *instance_;
    if (s.target_.kind==TargetKind::bike) {
        const auto owner=s.target_; const auto level=s.level_;
        draw_bike_scheme(s.draw_scope() && scheme==s.scheme_,[&] { return s.live_bike(); },[&] { return s.bike_artwork(owner,level); },[&](uint32_t slot) { return s.control(slot); },
            [&](uint32_t p) { return read<uint8_t>(p,0x5a); },
            [&](uint32_t p,bool v) { invoke(s.fn("_ZN7CButton10EnableDrawEb"),{p,v}); },
            [&] { invoke(s.scheme_originals_.at(read(scheme)),{scheme,alpha}); });
        return;
    }
    // Some joystick draws ignore EnableDraw, including the scheme used in MP.
    // Keep their suppression separate from the informative power selector.
    if (s.target_.kind == TargetKind::driver) {
        draw_jeep_scheme(s.draw_scope() && scheme == s.scheme_,(s.target_.driver.capabilities & 2) != 0,
            [&](uint32_t slot) { return s.control(slot); },
            [&](uint32_t button) { return read<uint8_t>(button,0x5a); },
            [&](uint32_t button,bool visible) { invoke(s.fn("_ZN7CButton10EnableDrawEb"),{button,visible}); },
            [&] { invoke(s.scheme_originals_.at(read(scheme)),{scheme,alpha}); });
        return;
    }
    if (mounted_machinegun(s.target_)) {
        draw_mounted_machinegun_scheme(s.draw_scope() && scheme == s.scheme_,
            [&](uint32_t slot) { return s.control(slot); },
            [&](uint32_t button) { return read<uint8_t>(button,0x5a); },
            [&](uint32_t button,bool visible) { invoke(s.fn("_ZN7CButton10EnableDrawEb"),{button,visible}); },
            [&] { invoke(s.scheme_originals_.at(read(scheme)),{scheme,alpha}); });
        return;
    }
    if (hide_scheme_draw(s.draw_scope(),scheme,s.scheme_)) {
        // The original scheme owns this selector's draw. Retain its native
        // IsEnabled gate and virtual Draw, with the caller's original alpha.
        const auto power=s.control(powers_control);
        if (power && invoke_bool(read(read(power),0x84),{power}))
            invoke(read(read(power),0x6c),{power,alpha});
        return;
    }
    invoke(s.scheme_originals_.at(read(scheme)),{scheme,alpha});
}
void __cdecl NativeControls::rush_overlay(uint32_t sprite,uint32_t frame,uint32_t x,uint32_t y,
                                        uint32_t flags,uint32_t scale_x,uint32_t scale_y,uint32_t alpha) {
    auto &s = *instance_;
    const uint32_t args[]{sprite,frame,x,y,flags,scale_x,scale_y,alpha};
    draw_rush_overlay(s.input_.gameplay && s.target_.kind == TargetKind::normal,s.rush_overlay_->original(),args);
}
uint32_t __cdecl NativeControls::pressed(uint32_t button) {
    auto &self = *instance_;
    if(self.target_.kind==TargetKind::normal) self.sync_normal_aim();
    if (self.target_.kind==TargetKind::bike) return invoke(self.pressed_->original(),{button});
    if (self.driver_pedal_release_.pending) {
        auto value = driver_pedal_input(self.driver_pedal_release_.owner,button,false,true,false,self.frame_input_,[&] {
            return self.driver_input_lifetime(self.driver_pedal_release_.owner,self.driver_pedal_release_.level);
        });
        if (value) return *value;
    }
    if (self.target_.kind == TargetKind::driver) {
        auto value = (self.target_.driver.capabilities & 1) ? driver_pedal_input(self.target_,button,self.input_.active(),false,self.enabled(button),
            self.frame_input_,[&] { return self.live_driver(); }) : std::optional<uint32_t>{};
        return value ? *value : invoke(self.pressed_->original(),{button});
    }
    if (self.input_.active()) {
        if (self.target_.kind == TargetKind::machinegun) {
            // Native events own the fire edges. Do not inject a second action
            // through shared buttons, or invent on-foot movement/aim behavior.
            return invoke(self.pressed_->original(),{button});
        }
        if (!read<uint8_t>(self.target_.component,0xd)) return invoke(self.pressed_->original(),{button});
        if (self.target_.kind == TargetKind::mech) {
            if (button == self.control(shoot_control)) return self.enabled(button) && self.frame_input_.requested(Action::fire);
            if (button == self.control(second_shoot_control)) return self.enabled(button) && self.frame_input_.requested(Action::aim);
            return invoke(self.pressed_->original(),{button});
        }
        if (button == self.control(rush_control))
            return self.enabled(button) && !self.normal_aim_intent_.wanted() && sprint_requested(self.frame_input_);
        if (button == self.control(aim_control)) {
            self.sync_normal_aim();
            return self.enabled(button) && self.normal_aim_intent_.button();
        }
        for (auto [slot,action] : {std::pair{shoot_control,Action::fire},
                                  std::pair{aim_control,Action::aim},std::pair{jump_control,Action::jump},std::pair{powers_control,Action::power}})
            if (button == self.control(slot)) return self.enabled(button) && self.frame_input_.requested(action);
    }
    return invoke(self.pressed_->original(),{button});
}
uint32_t __cdecl NativeControls::edge_pressed(uint32_t button) {
    auto &s = *instance_;
    const auto boundary=normal_input_boundary;
    // Original normal Update's detonator Pressed query (CALL+0x5f6). Let that
    // branch own mine use, tutorial completion, MP message and button disable.
    if(s.detonate_pending_ && s.target_.kind==TargetKind::normal && normal_input_depth==1 &&
       GetCurrentThreadId()==s.normal_aim_lane_.load() && boundary &&
       reinterpret_cast<uint32_t>(_ReturnAddress())==s.normal_update_entry_+0x5f9 &&
       boundary->interface_==s.normal_ && boundary->level==s.level_ && boundary->player==s.player_ &&
       boundary->state==s.state_ && s.input_.active() && s.input_.edge_epoch()==s.grenade_epoch_ &&
       read<uint8_t>(s.player_,0xd) && button==read(s.normal_,0x68)) {
        s.detonate_pending_=false;
        if(s.enabled(button)) return 1;
    }
    if(const auto resume=s.communications_.panel_close(button)) return *resume;
    if(s.communications_.press_score(button)) return 1;
    if(s.target_.kind==TargetKind::normal) s.sync_normal_aim();
    if (s.target_.kind==TargetKind::bike) return invoke(s.edge_->original(),{button});
    if (s.target_.kind == TargetKind::driver) {
        const auto returned=reinterpret_cast<uint32_t>(_ReturnAddress());
        if(jeep_role_query_matches(s.normal_update_entry_,returned,s.jeep_role_edge_.role) && normal_input_depth==1 && GetCurrentThreadId()==s.normal_aim_lane_.load() && boundary &&
           boundary->interface_==s.normal_ && boundary->level==s.level_ && boundary->player==s.player_ &&
           consume_jeep_role_edge(s.jeep_role_edge_,button,[&](InputTarget owner,uint32_t level,uint64_t epoch) {
               return s.target_==owner && s.level_==level && s.live_driver() && s.input_.active() && s.input_.edge_epoch()==epoch;
           },[](uint32_t p,uint32_t offset) { return read(p,offset); },
             [&](uint32_t current) { return s.enabled(current); })) return 1;
        if (s.input_.active() && s.live_driver() && button == read(s.normal_,0x10))
            return mech_exit_edge(s.driver_exit_pending_,s.enabled(button));
        return invoke(s.edge_->original(),{button});
    }
    if (s.input_.active()) {
        if (s.target_.kind == TargetKind::machinegun) {
            if (s.live_machinegun() && button == read(s.normal_,0x10))
                return mech_exit_edge(s.machinegun_exit_pending_,s.enabled(button));
            return invoke(s.edge_->original(),{button});
        }
        if (!read<uint8_t>(s.target_.component,0xd)) return invoke(s.edge_->original(),{button});
        if (s.target_.kind == TargetKind::mech) {
            if (button == read(s.normal_,0x10)) return mech_exit_edge(s.mech_exit_pending_,s.enabled(button));
            return invoke(s.edge_->original(),{button});
        }
        for (auto [slot,action] : {std::pair{jump_control,Action::jump},std::pair{powers_control,Action::power}})
            if (button == s.control(slot)) return s.enabled(button) &&
                s.frame_input_.edge(action);
    }
    return invoke(s.edge_->original(),{button});
}
void __cdecl NativeControls::draw(uint32_t interface_) {
    auto &s = *instance_;
    draw_contextual_buttons(s.draw_scope() && interface_==s.normal_ && !s.hud_presentation_.show_contextual,
        [&](uint32_t slot) { return read(interface_,slot); },
        [&](uint32_t button) { return read<uint8_t>(button,0x5a); },
        [&](uint32_t button,bool visible) { invoke(s.fn("_ZN7CButton10EnableDrawEb"),{button,visible}); },[&] {
    if (s.target_.kind==TargetKind::bike) { invoke(s.draw_->original(),{interface_}); return; }
    if (mounted_machinegun(s.target_)) {
        draw_mounted_machinegun_interface(s.draw_scope() && interface_ == s.normal_,
            [&](uint32_t slot) { return read(interface_,slot); },
            [&](uint32_t button) { return read<uint8_t>(button,0x5a); },
            [&](uint32_t button,bool visible) { invoke(s.fn("_ZN7CButton10EnableDrawEb"),{button,visible}); },
            [&] { invoke(s.draw_->original(),{interface_}); });
        return;
    }
    if (s.target_.kind == TargetKind::driver) {
        draw_jeep_controls(s.draw_scope() && interface_ == s.normal_,
            [&](uint32_t slot) { return read(interface_,slot); },
            [&](uint32_t button) { return read<uint8_t>(button,0x5a); },
            [&](uint32_t button,bool visible) { invoke(s.fn("_ZN7CButton10EnableDrawEb"),{button,visible}); },
            [&] { invoke(s.draw_->original(),{interface_}); });
        return;
    }
    draw_interface_controls(s.draw_scope() && interface_ == s.normal_,
        [&](uint32_t slot) { return s.control(slot); },
        [&](uint32_t offset) { return read(interface_,offset); },
        [&](uint32_t button) { return read<uint8_t>(button,0x5a); },
        [&](uint32_t button,bool visible) { invoke(s.fn("_ZN7CButton10EnableDrawEb"),{button,visible}); },
        [&] { invoke(s.draw_->original(),{interface_}); });
    });
}
void __cdecl NativeControls::interaction_draw(uint32_t player,uint32_t dt) {
    auto &s=*instance_;
    draw_native_interaction(s.hud_presentation_,player==s.player_,
        [&] { invoke(s.interaction_draw_->original(),{player,dt}); });
}
void __cdecl NativeControls::hud_render(uint32_t hud) {
    auto &s = *instance_;
    if (s.target_.kind == TargetKind::bike || s.target_.kind == TargetKind::driver || mounted_machinegun(s.target_) || !s.draw_scope()) { invoke(s.hud_->original(),{hud}); return; }
    auto begin = read(hud,0xd0), end = read(hud,0xd4);
    if (end < begin || end - begin > 4096) throw std::runtime_error("Native HUD button layout mismatch");
    // Restore each node's own visibility (bit 3), not effective parent visibility.
    // Visibility changes must not persist into the next native update.
    std::map<uint32_t,bool> visibility;
    for (uint32_t entry = begin; entry < end; entry += 16) {
        for (auto offset : {0u,4u}) {
            auto node = read(entry,offset);
            if (node) visibility.try_emplace(node,(read(node,0xf4) & 8) != 0);
        }
    }
    for (uint32_t i = 0; i < (end-begin)/16; ++i) invoke(s.fn("_ZN4CHud10HideButtonEi"),{hud,i});
    invoke(s.hud_->original(),{hud});
    for (auto [node,visible] : visibility) invoke(read(read(node),0x4c),{node,visible});
}
void __cdecl NativeControls::tutorial_draw(uint32_t tutorial) {
    auto &s = *instance_;
    if (s.target_.kind != TargetKind::bike && s.target_.kind != TargetKind::driver && !mounted_machinegun(s.target_) && s.draw_scope() && (!s.mech_tutorials_.contains(read(tutorial)) || s.target_.kind == TargetKind::mech))
        invoke(read(read(tutorial),0x20),{tutorial}); // Keep DrawDialog text.
    else invoke(s.tutorial_originals_.at(read(tutorial)),{tutorial});
}
}
