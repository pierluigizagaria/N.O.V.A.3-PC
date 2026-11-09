#pragma once
#include "abi/guest_hook.h"
#include "input/controls.h"
#include "input/native_communications.h"
#include "input/native_panel_movement.h"
#include "input/native_gamepad_post_drain.h"
#include "input/native_adapter.h"
#include "input/native_target.h"
#include "input/native_machinegun.h"
#include "input/native_mounted_machinegun.h"
#include "input/native_driver.h"
#include "input/native_jeep.h"
#include "input/native_bike.h"
#include "input/trace.h"
#include "input/native_diagnostics.h"
#include "input/native_intents.h"
#include "input/grenade_gesture.h"
#include <map>
#include <memory>
#include <set>

namespace nova::input {
// Shared with the installed synthetic release fixture. Preserve native call
// order and query timing; the caller already proved the current normal owner.
template<class Event,class Weapons,class UnAim>
void release_normal_owned(bool &fire,bool &aim,bool &moving,Event event,Weapons weapons,UnAim unaim) {
    if(fire) event(30);
    const auto manager=weapons();
    if(aim && manager) unaim(manager);
    if(moving) event(14);
    fire=aim=moving=false;
}
// Library-specific layout knowledge belongs here, never in SDL or the generic hook facility.
class NativeControls {
  public:
    NativeControls(const ElfImage &image, Controls &input, bool detailed_motion = false);
    ~NativeControls();
    void begin_frame(uint64_t frame);
    void end_frame();
  private:
    static NativeControls *instance_;
    static void __cdecl update(uint32_t player, uint32_t dt);
    static uint32_t __cdecl pressed(uint32_t button);
    static uint32_t __cdecl edge_pressed(uint32_t button);
    static void __cdecl draw(uint32_t interface_);
    static void __cdecl hud_render(uint32_t hud);
    static void __cdecl tutorial_draw(uint32_t tutorial);
    static void __cdecl player_input(uint32_t player, uint32_t dt);
    static void __cdecl mech_input(uint32_t player, uint32_t dt);
    static void __cdecl mech_stop(uint32_t weapon, uint32_t channel, uint32_t messages);
    static void __cdecl machinegun_update(uint32_t player,uint32_t dt);
    static void __cdecl machinegun_input(uint32_t player, uint32_t dt);
    static void __cdecl jeep_update(uint32_t player,uint32_t dt);
    static void __cdecl jeep_stop(uint32_t weapon,uint32_t channel,uint32_t messages);
    static void __cdecl jeep_primary_animation(uint32_t vehicle);
    static void __cdecl jeep_secondary_animation(uint32_t vehicle);
    static void __cdecl driver_camera(uint32_t player, uint32_t dt);
    static void __cdecl driver_pedals(uint32_t interface_);
    static void __cdecl analog_draw(uint32_t control,uint32_t alpha);
    static void __cdecl mobile_draw(uint32_t control,uint32_t alpha);
    static void __cdecl angular_draw(uint32_t control,uint32_t alpha);
    static void __cdecl machinegun_stop(uint32_t weapon, uint32_t channel, uint32_t messages);
    static void __cdecl machinegun_release(uint32_t manager, uint32_t channel);
    static void __cdecl velocity(uint32_t player, uint32_t dt);
    static void __cdecl scheme_draw(uint32_t scheme, uint32_t alpha);
    static void __cdecl rush_overlay(uint32_t sprite, uint32_t frame, uint32_t x, uint32_t y,
                                    uint32_t flags, uint32_t scale_x, uint32_t scale_y, uint32_t alpha);
    static uint32_t __cdecl bike_on_event(uint32_t control,uint32_t event,uint32_t manager);
    static void __cdecl bike_reset_control(uint32_t control);
    static void __cdecl bike_reset_controls(uint32_t control);
    void publish_bike_receiver();
    void update_bike_core(InputTarget owner,uint32_t level);
    void synchronize_bike_origins();
    bool bike_core(InputTarget owner,uint32_t level) const;
    bool bike_fallback_core(InputTarget owner,uint32_t level) const;
    void fallback_bike_input(InputTarget owner,uint32_t level,BikeActions &actions);
    static void __cdecl bike_base(uint32_t base,uint32_t dt);
    static void __cdecl bike_primary(uint32_t visual,uint32_t quaternion);
    static void __cdecl bike_enable(uint32_t component,uint32_t enabled);
    static void __cdecl bike_state(uint32_t component,uint32_t state);
    static void __cdecl bike_road(uint32_t component,uint32_t road);
    static void __cdecl bike_configure(uint32_t component,uint32_t config);
    static void __cdecl bike_move_to(uint32_t component,uint32_t position,uint32_t direction,uint32_t animation);
    static void __cdecl bike_reset(uint32_t component);
    static void __cdecl bike_load(uint32_t component,uint32_t stream);
    InputTarget bike_identity(uint32_t level,uint32_t component,uint32_t player) const;
    bool bike_root(InputTarget owner,uint32_t previous_level) const;
    bool live_bike() const;
    bool bike_artwork(InputTarget owner,uint32_t level) const;
    bool bike_retained(InputTarget owner,uint32_t level) const;
    void queue_bike_release(InputTarget owner,uint32_t level);
    void clean_bike_release();
    void apply_bike();
    bool bike_event(InputTarget owner,uint32_t type,float x=0,BikeActions *acquire=nullptr);
    void bike_transition(uint32_t component);
    void refresh();
    void recover_mount_interaction(uint32_t player,bool on_foot);
    uint32_t interaction_wait_player_=0,interaction_wait_target_=0,interaction_wait_started_=0;
    void apply(uint32_t player);
    void cancel_normal_aim();
    void sync_normal_aim();
    struct NormalAimOwner { uint32_t level,player,interface_,state; uint64_t epoch; };
    NormalAimOwner normal_weapon_owner_{};
    bool normal_aim_context(const NormalAimOwner &owner) const;
    enum class NormalReloadState { unknown, free, busy };
    NormalReloadState normal_reload_state(uint32_t weapon,const NormalAimOwner &owner) const;
    void invalidate_normal_weapon(uint32_t weapon);
    static uint32_t __cdecl normal_switch(uint32_t manager);
    static uint32_t __cdecl normal_switch_entry(uint32_t manager);
    static uint32_t __cdecl normal_can_aim(uint32_t weapon);
    static uint32_t __cdecl normal_native_unaim(uint32_t weapon,uint32_t animated);
    static uint32_t __cdecl normal_reload_restore_aim(uint32_t weapon);
    static uint32_t __cdecl normal_select_weapon(uint32_t manager,uint32_t index,uint32_t flags);
    static uint32_t __cdecl normal_ctor1(uint32_t weapon,uint32_t actor);
    static uint32_t __cdecl normal_ctor2(uint32_t weapon,uint32_t actor);
    static uint32_t __cdecl normal_reset(uint32_t weapon);
    static uint32_t __cdecl normal_reinit(uint32_t weapon);
    static uint32_t __cdecl normal_dtor0(uint32_t weapon);
    static uint32_t __cdecl normal_dtor1(uint32_t weapon);
    static uint32_t __cdecl normal_dtor2(uint32_t weapon);
    void apply_mech();
    void apply_machinegun();
    void apply_driver();
    InputTarget driver_identity(uint32_t level,uint32_t component) const;
    bool driver_lifetime(InputTarget target,uint32_t level) const;
    bool driver_input_lifetime(InputTarget target,uint32_t level) const;
    bool driver_camera_lifetime(InputTarget target,uint32_t level) const;
    bool live_driver() const;
    void driver_event(InputTarget target,uint32_t type,float x = 0,float y = 0);
    void clean_driver_pedals();
    void apply_jeep_weapons();
    bool jeep_controller(InputTarget target,uint32_t level) const;
    bool jeep_weapon(InputTarget target,uint32_t level) const;
    bool jeep_animation(InputTarget target,uint32_t level) const;
    void jeep_animation_stop(uint32_t vehicle,unsigned channel);
    void driver_touch_draw(uint32_t control,uint32_t alpha,uint32_t original);
    InputTarget machinegun_identity(uint32_t level,uint32_t component,uint32_t player) const;
    bool machinegun_structure(InputTarget target,uint32_t level) const;
    bool machinegun_controller(InputTarget target,uint32_t level) const;
    bool machinegun_weapon(InputTarget target,uint32_t level) const;
    bool machinegun_manager(InputTarget target,uint32_t level) const;
    bool machinegun_context(uint32_t level,uint32_t player,uint32_t machinegun) const;
    bool live_machinegun() const;
    bool draw_scope() const;
    static void __cdecl interaction_draw(uint32_t player,uint32_t dt);
    NativeHudPresentation hud_presentation_;
    std::unique_ptr<abi::GuestHook> interaction_draw_;
    void release_target(InputTarget target);
    void clear_actions();
    void release_event(uint32_t player, uint32_t type);
    void sample(const char *reason);
    void event(uint32_t player, uint32_t type, float x = 0, float y = 0);
    void restore_aim();
    void release_actions(uint32_t player);
    uint32_t fn(const char *name) const;
    uint32_t control(uint32_t slot) const;
    bool enabled(uint32_t button) const;
    const ElfImage &image_;
    Controls &input_;
    NativeCommunications communications_;
    NativePanelMovement panel_movement_;
    NativeGamepadPostDrain gamepad_;
    uint64_t normal_pad_reload_revision_=0;
    Trace trace_;
    NativeDiagnostics diagnostics_;
    std::map<std::string,uint32_t> functions_;
    std::unique_ptr<abi::GuestHook> update_, pressed_, draw_, hud_, edge_, player_input_, velocity_;
    std::unique_ptr<abi::GuestCall> rush_overlay_;
    std::unique_ptr<abi::GuestHook> mech_input_;
    std::unique_ptr<abi::GuestHook> machinegun_input_, machinegun_update_;
    std::unique_ptr<abi::GuestCall> machinegun_stop_, machinegun_release_;
    std::unique_ptr<abi::GuestHook> driver_camera_, driver_pedals_, analog_draw_, mobile_draw_, angular_draw_;
    std::unique_ptr<abi::GuestHook> jeep_update_;
    std::vector<std::unique_ptr<abi::GuestCall>> jeep_release_calls_;
    uint32_t normal_update_entry_=0;
    JeepRoleEdge jeep_role_edge_;
    JeepFireActions jeep_fire_;
    DriverActions driver_actions_;
    DriverPedalRelease driver_pedal_release_;
    bool driver_exit_pending_ = false;
    MachineGunActions machinegun_actions_;
    bool machinegun_exit_pending_ = false;
    std::vector<std::unique_ptr<abi::GuestCall>> mech_stops_;
    uint32_t mech_weapon_ = 0;
    InputTarget target_;
    std::unique_ptr<abi::GuestCall> bike_base_,bike_primary_;
    std::unique_ptr<abi::GuestHook> bike_enable_,bike_state_,bike_road_,bike_configure_,bike_move_to_,bike_reset_,bike_load_;
    BikeActions bike_actions_;
    BikeCancellation bike_release_;
    BikeWriterEpochs bike_writers_;
    InputTarget bike_observed_owner_;
    uint32_t bike_observed_level_=0;
    std::unique_ptr<abi::GuestHook> bike_events_,bike_reset_input_,bike_reset_inputs_;
    uint64_t bike_generation_=0, bike_applied_frame_=UINT64_MAX;
    MechActions mech_actions_;
    bool mech_exit_pending_ = false;
    std::set<uint32_t> mech_tutorials_;
    std::vector<std::unique_ptr<abi::GuestHook>> tutorials_;
    std::map<uint32_t,uint32_t> tutorial_originals_;
    std::vector<std::unique_ptr<abi::GuestHook>> scheme_draws_;
    std::map<uint32_t,uint32_t> scheme_originals_;
    FrameInput frame_input_;
    GrenadeGesture grenade_gesture_;
    bool detonate_pending_=false;
    uint64_t grenade_epoch_=0,grenade_gamepad_revision_=0;
    NativeAimIntent normal_aim_intent_;
    std::atomic<uint32_t> normal_aim_lane_{0},normal_aim_weapon_{0},normal_aim_manager_{0};
    std::atomic<uint64_t> normal_aim_epoch_{0};
    uint64_t normal_aim_seen_=0;
    std::unique_ptr<abi::GuestCall> normal_switch_call_,normal_can_aim_call_,normal_unaim_call_,normal_reload_restore_;
    std::unique_ptr<abi::GuestHook> normal_switch_hook_,normal_select_;
    std::unique_ptr<abi::GuestHook> normal_ctor1_,normal_ctor2_,normal_reset_,normal_reinit_,normal_dtor0_,normal_dtor1_,normal_dtor2_;
    bool look_pending_ = false;
    float rush_heading_ = 0;
    uint64_t frame_ = 0;
    uint32_t level_ = 0, player_ = 0, mech_ = 0, normal_ = 0, scheme_ = 0, state_ = 0;
    uint32_t ingame_vtable_ = 0, event_manager_ = 0;
    uint32_t event_generic_ = 0, event_move_ = 0, event_release_ = 0;
    uint32_t lua_ = 0, saved_aim_ = 0;
    bool aim_override_ = false, fire_ = false, aim_ = false, moving_ = false;
    bool observed_ = false, aim_reported_ = false;
};
}
