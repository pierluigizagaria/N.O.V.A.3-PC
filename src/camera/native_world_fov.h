#pragma once
#include "abi/guest_hook.h"
#include "camera/world_fov.h"
#include "weapon_placement/position_policy.h"
#include "camera/viewmodel_eligibility_native.h"
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace nova::weapon_placement { class NativeRigidDrawBinding; }
namespace nova::camera {
enum class LensSite {
    consumer, raw, weapon_begin, pseudo_write, weapon_write, weapon_restore, weapon_diff, jeep_save, jeep_enable, jeep_update, jeep_helper,
    jeep_write, fast_node, level_first, level_accumulate, render_read, additive_write, vertical_write, camera_projection
};
// Fixture injection changes checked call-site locations, not adapter behavior.
struct LensCallSite {
    std::string owner;
    uint32_t offset;
    LensSite kind;
    std::vector<uint8_t> expected;
};
class NativeWorldFov {
  public:
    NativeWorldFov(const ElfImage &image, std::optional<float> horizontal_degrees,
                   const std::vector<LensCallSite> *fixture_sites = nullptr,
                   bool (*fixture_fail_lifecycle_allocation)() = nullptr,
                   std::optional<float> weapon_degrees = {},
                   bool prepare_interactive = false);
    ~NativeWorldFov();
    NativeWorldFov(const NativeWorldFov &) = delete;
    void bind_render_thread();
    // Acquire BEFORE entering a native continuation: this may call components.
    // Consume performs only the proved pure SP sample and field/token fences.
    // O08/V01 additionally prove their exact mesh/consumer. A capsule is bound
    // to its camera lifetime and weapon pass; never retain a Decision across
    // a native callback. Default capsule grants nothing.
    // O08h: a distinct pre-mix observation, never a render-pass permit.
    // Only identities escape acquisition; every use requires a fresh sample.
    class PreMixOwnerCapsule {
        friend class NativeWorldFov;
        viewmodel_eligibility::NativeOwners owners_{};
        uint64_t publication_=0;
      public:
        uint32_t weapon_identity() const { return owners_.weapon; }
        uint32_t camera_node_identity() const { return owners_.node; }
        uint32_t game_camera_identity() const { return owners_.game_camera; }
        uint64_t camera_generation() const { return owners_.camera_generation; }
    };
    static PreMixOwnerCapsule acquire_premix_owners();
    static viewmodel_eligibility::NativeSample sample_premix_eligibility(const PreMixOwnerCapsule &);
    static bool premix_camera_alive(const PreMixOwnerCapsule &);
    class ViewmodelOwnerCapsule {
        friend class NativeWorldFov;
        uint32_t level_=0,player_=0,actor_=0,camera_=0,node_=0;
        uint64_t generation_=0,publication_=0,pass_serial_=0;
        const void *weapon_pass_=nullptr;
      public:
        ViewmodelOwnerCapsule()=default;
    };
    static ViewmodelOwnerCapsule acquire_viewmodel_owners(uint32_t supplied_camera);
    static viewmodel_eligibility::NativeSample sample_viewmodel_eligibility(const ViewmodelOwnerCapsule &);
    // Callback-free consumption only. Acquire the capsule before continuation.
    // Borrowed identities are valid only until the next callback/native entry.
    struct ViewmodelWeaponSample {
        viewmodel_eligibility::NativeSample eligibility;
        uint32_t weapon=0,scene_manager=0;
    };
    static ViewmodelWeaponSample sample_viewmodel_weapon(const ViewmodelOwnerCapsule &);
    // Render lane, outside guest callbacks. Structural request only: this unit
    // does not activate XY or publish a modified matrix.
    bool set_rigid_draw_request(weapon_placement::Position);
    struct RigidDrawStatus { uint64_t acquisitions=0,samples=0,admitted=0; };
    RigidDrawStatus rigid_draw_status() const { return rigid_status_; }
    struct Options {
        std::optional<float> world, weapon;
        bool operator==(const Options &) const = default;
    };
    enum class Application { inactive, deferred, applied };
    enum class Gate { unobserved, no_local_camera, unsupported_owner, unproved_lens,
                      unproved_weapon_source, unproved_standard_output, inactive_camera, applied };
    struct WorldEvidence {
        const char *leaf="not-observed",*phase="none",*established="none",*revoked="none",*additive="not-observed";
        uint32_t node=0,raw=0,reference=0,establish_site=0,establish_lane=0,revoke_site=0;
        bool raw_observed=false,lens_present=false;
        const char *delta_reason="never-observed";
        uint64_t delta_lifetime=0,delta_order=0;
        const char *upstream="none";
        uint32_t upstream_site=0,upstream_lane=0;
        uint64_t upstream_origin=0;
    };
    struct LiveStatus {
        Options requested, committed;
        uint64_t requested_sequence=0, committed_sequence=0, option_epoch=0;
        uint64_t eligible_frame=0;
        // Last positively observed application frames, not a promise about a
        // later unobserved receiver. A deferred field never claims visibility.
        uint64_t world_applied_frame=0, weapon_applied_frame=0;
        Application application=Application::inactive;
        Application world_application=Application::inactive, weapon_application=Application::inactive;
        Gate world_gate=Gate::unobserved,weapon_gate=Gate::unobserved;
        WorldEvidence world_evidence;
        const char *weapon_leaf="not-observed";
        uint8_t weapon_stages=0; // before/current/source/sample already observed.
    };
    // Host lanes may queue/read copies. Only the bound render lane may service,
    // before NativeControls::begin_frame and the guest step, outside guest scopes.
    bool request_options(Options);
    LiveStatus live_status() const;
    void service_options(uint64_t frame);
    // T11e: bound render lane, outside guest/camera scopes. These form one
    // same-aspect resize proof; no historical raw angle is reinterpreted using
    // a new aspect. A false prepare/verify must defer or restore the resize.
    bool prepare_render_extent(int old_width,int old_height,int new_width,int new_height);
    bool verify_render_extent(bool changed);
    void finish_render_extent(bool changed);
    // UI owner: arms a bounded internal trace only for completed edit/Reset.
    // No guest query, optical mutation or per-drag logging is performed.
    void trace_completed_edit(Options);
    // Optional metadata bridge around the pre-existing native Jeep Update hook.
    // No instance/native setting: one null check, no query, lock or camera work.
    class JeepUpdateScope {
      public:
        JeepUpdateScope(uint32_t component, uint32_t dt);
        ~JeepUpdateScope();
      private:
        friend class NativeWorldFov;
        NativeWorldFov *owner_ = nullptr;
        JeepUpdateScope *previous_ = nullptr;
        uint32_t component_ = 0, dt_ = 0;
    };
  private:
    struct Root {
        uint32_t level=0, player=0, actor=0, camera=0;
        bool operator==(const Root &) const = default;
        explicit operator bool() const { return level && player && actor && camera; }
    };
    struct Stamp {
        std::array<uint32_t,16> matrix{};
        std::array<uint32_t,6> parameters{};
        float angle=0;
        bool effective=false;
        uint64_t option_epoch=0;
        enum class Context { native,world,weapon } context=Context::native;
    };
    struct Node {
        uint64_t generation=0, origin=0,write_epoch=0,raw_epoch=0;
        Root root{};
        std::optional<ReferenceLens> lens;
        std::optional<Stamp> stamp;
        bool refresh=false;
    };
    struct RenderLensWitness {
        uint32_t node,raw;
        uint64_t generation,origin,write_epoch,raw_epoch;
        ReferenceLens lens;
        std::array<uint32_t,6> parameters;
        std::array<uint32_t,16> matrix;
    };
    struct RenderExtentProof {
        Root root;
        int old_width,old_height,new_width,new_height;
        uint64_t serial;
        std::vector<RenderLensWitness> witnesses;
    };
    std::optional<RenderExtentProof> render_extent_proof_;
    struct Saved { Root root; float raw; double reference; };
    struct Pending {
        Root root{};
        uint32_t node=0, component=0, config=0;
        uint64_t generation=0, origin=0;
        ReferenceLens before{};
        float coefficient=0, lower=0, upper=0, alpha=0;
        uint32_t branch=0;
        bool speed=false;
        std::optional<double> saved;
    };
    struct Delta { Root root; float native; double reference; };
    struct DeltaPending {
        Root root; float raw,old_delta; double reference,old_reference_delta,scale;
        bool accumulate; uint64_t epoch;
    };
    struct AddPending {
        Root root; uint32_t node; uint64_t generation,origin;
        float raw,delta; double reference,reference_delta;
    };
    enum class EventKind { node, component, level, level_seed, external,raw_external,
                           raw_begin,pseudo_external,standard_begin,standard_external };
    struct ForeignWriter {
        uint64_t token=0,lifetime=0;
        uint32_t raw=0,caller=0,lane=0;
        int32_t width=0,height=0;
        bool coherent=false;
    };
    struct Event { uint32_t node; EventKind kind; uint64_t epoch; Event *next; uint32_t caller,lane; ForeignWriter writer; };
    struct ForeignState { uint64_t token=0,lifetime=0; unsigned active=0,builders=0; bool overlap=false; };
    struct PendingForeign { ForeignWriter writer; uint64_t generation=0; Root root; bool standard=false; };
    struct DeltaHistory { const char *reason="never-observed"; uint64_t lifetime=0,order=0; };
    struct Revocation { const char *kind="none"; uint32_t site=0,lane=0; uint64_t origin=0; };
    struct LensHistory {
        const char *established="none",*revoked="none",*additive="not-observed";
        uint32_t establish_site=0,establish_lane=0,revoke_site=0;
        DeltaHistory delta;
        Revocation last,upstream;
    };
    struct WeaponReference {
        Root root; uint32_t node; uint64_t generation,origin; ReferenceLens lens;
    };
    struct WeaponSource {
        Root root{}; uint32_t manager=0,owner=0,flag=0,value=0,base=0,mode=0;
        uint32_t inventory=0,end=0,index=0,component=0,config=0;
        std::array<uint32_t,4> endpoints{};
        int32_t width=0,height=0;
        double ratio=0;
        bool tweak=false,ratio_free_zero=false;
        bool operator==(const WeaponSource &) const = default;
    };
    // Pointer values are comparison identities; every use reacquires current
    // ownership and lifecycle tokens. Skipped frames inherit without queries.
    struct WeaponFrame {
        WeaponFrame *parent; bool regular=false,changed=false,restored=false;
        uint64_t eligibility_pass=0;
        ViewmodelOwnerCapsule rigid_owners;
        uint64_t applied_epoch=UINT64_MAX;
        std::optional<WeaponSource> source;
        std::optional<float> sample;
        std::optional<WeaponReference> before,current;
    };
    weapon_placement::Position rigid_request_{};
    std::atomic<bool> rigid_request_active_{false};
    RigidDrawStatus rigid_status_{};
    std::unique_ptr<weapon_placement::NativeRigidDrawBinding> rigid_binding_;
    uint32_t original_rigid_model_=0,rigid_model_return_=0;
    bool rigid_requested() const { return rigid_request_active_.load(std::memory_order_relaxed); }
    static void __cdecl rigid_model(uint32_t,uint32_t,uint32_t) noexcept;
    WeaponFrame *weapon_frame_=nullptr;
    uint64_t eligibility_pass_serial_=0;
    struct CameraScope { uint32_t node; uint64_t generation; Root owners; CameraScope *parent; };
    CameraScope *camera_scope_=nullptr;
    bool force_native_weapon_=false,eligibility_rejected_=false;
    const char *eligibility_reason_="native-fps-not-sampled";
    std::unique_ptr<viewmodel_eligibility::NativeProducer> eligibility_;
    std::unique_ptr<abi::GuestCall> camera_projection_call_;
    uint32_t original_camera_render_=0,original_driver_projection_=0;
    uint32_t projection_return_=0,camera_epilogue_=0;
    bool camera_alive(const CameraScope &) const;
    viewmodel_eligibility::NativeSample sample_weapon_eligibility(uint32_t,const Root &);
    bool eligible_weapon(uint32_t,const Root &);
    // True means the original builder ran, even if its callback retired the receiver.
    bool retire_weapon_stamp(uint32_t,uint64_t);
    static void __cdecl camera_render(uint32_t,uint32_t);
    static __declspec(noinline) void __cdecl driver_projection(uint32_t,uint32_t,uint32_t,uint32_t);
    std::optional<float> setting_;
    std::optional<float> weapon_setting_;
    const bool prepared_, installed_, track_weapon_;
    mutable std::mutex options_mutex_;
    LiveStatus status_;
    Gate world_gate_=Gate::unobserved,weapon_gate_=Gate::unobserved;
    WorldEvidence world_evidence_;
    const char *weapon_leaf_="not-observed";
    uint8_t weapon_stages_=0;
    std::map<uint32_t,LensHistory> lens_history_; // Render-only passive history; never grants lens authority.
    std::mutex foreign_mutex_; // Never held across guest work or another adapter lock.
    std::map<uint32_t,ForeignState> foreign_state_;
    std::map<uint32_t,PendingForeign> foreign_pseudo_; // Render-owned only.
    uint64_t foreign_serial_=0;
    ForeignWriter begin_foreign(uint32_t,uint32_t);
    bool end_foreign(uint32_t,const ForeignWriter &);
    void foreign_builder(uint32_t,bool);
    bool valid_foreign(uint32_t,const ForeignWriter &,bool *busy=nullptr);
    void invalidate_foreign(uint32_t,bool lifetime=false);
    void consume_foreign(uint32_t,const Root &);
    void world_rejected(const char *,uint32_t,const Node &,std::optional<uint32_t> raw={});
    void established(uint32_t,const char *,uint32_t site=0);
    void revoked(uint32_t,const char *,uint32_t site=0);
    void weapon_observed(const char *);
    uint64_t trace_edit_=0,trace_until_=0,trace_last_ms_=0,trace_sequence_=0;
    Options trace_target_;
    unsigned trace_remaining_=0;
    LiveStatus trace_last_;
    bool trace_observed_=false;
    void trace_status(const LiveStatus &);
    uint64_t option_epoch_=0, committed_sequence_=0;
    uint64_t service_frame_=0, published_generation_=0;
    uint32_t published_node_=0;
    unsigned guest_depth_=0, service_depth_=0;
    uint64_t world_applied_epoch_=UINT64_MAX, weapon_applied_epoch_=UINT64_MAX;
    uint64_t world_applied_frame_=0, weapon_applied_frame_=0;
    void invalidate_application();
    void publish_status(uint64_t frame,bool eligible);
    // Foreign callbacks publish events only; they never touch render depths.
    class CallbackScope {
        NativeWorldFov &owner_; bool render_;
      public:
        explicit CallbackScope(NativeWorldFov &owner):owner_(owner),render_(owner.lane()) {
            if (render_) ++owner_.guest_depth_;
        }
        ~CallbackScope() { if (render_) --owner_.guest_depth_; }
    };
    std::atomic<uint32_t> lane_{0};
    std::atomic<Event *> events_{nullptr};
    std::atomic<uint64_t> event_epoch_{0};
    std::atomic<uint64_t> publication_serial_{0};
    bool (*fixture_fail_lifecycle_allocation_)() = nullptr;
    uint64_t generation_=0;
    Root observed_root_{};
    uint32_t observed_component_=0;
    bool observed_=false;
    uint32_t get_level_=0,get_player_=0,get_jeep_=0,screen_w_=0,screen_h_=0,base_=0,vtable_=0;
    uint32_t scene_manager_=0,render_camera_=0,tweak_=0;
    std::map<uint32_t,Node> nodes_;
    std::map<uint32_t,Saved> saved_;
    std::map<uint32_t,LensSite> sites_;
    std::optional<Pending> pending_;
    std::optional<Delta> delta_;
    std::optional<DeltaPending> delta_pending_;
    std::optional<AddPending> add_pending_;
    const char *additive_reject_="not-observed";
    uint32_t additive_capture_node_=0;
    DeltaHistory additive_delta_history_;
    Revocation additive_upstream_;
    std::map<uint32_t,DeltaHistory> delta_history_; // Private passive Level metadata only.
    std::map<uint32_t,bool> delta_seed_;
    std::map<uint32_t,uint64_t> level_epoch_;
    std::vector<std::unique_ptr<abi::GuestHook>> hooks_;
    std::unique_ptr<abi::GuestCall> fast_node_call_;
    std::unique_ptr<abi::GuestCall> weapon_diff_call_;
    uint32_t original_fast_node_=0;
    uint32_t original_weapon_diff_=0;
    uint32_t original_fov_=0,original_set_=0,original_projection_=0,original_matrices_=0;
    uint32_t original_get_projection_=0,original_frustum_=0,original_custom_=0;
    uint32_t original_weapons_=0,original_ctor1_=0,original_ctor2_=0,original_dtor_=0;
    uint32_t original_enable_=0,original_helper_=0;
    uint32_t original_jeep_ctor1_=0,original_jeep_ctor2_=0;
    uint32_t original_level_ctor1_=0,original_level_ctor2_=0,original_level_reset_=0;
    unsigned matrix_depth_=0,stage_depth_=0;
    uint32_t scoped_component_=0,scoped_dt_=0;
    bool scoped_force_=false;
    static NativeWorldFov *instance_;
    static thread_local JeepUpdateScope *jeep_scope_;
    bool lane() const;
    bool native_weapon_mode() const;
    WeaponFrame *regular_frame() const;
    bool valid_reference(WeaponReference,uint32_t,const Root *acquired=nullptr);
    std::optional<WeaponReference> incoming_reference(uint32_t,float);
    std::optional<WeaponSource> weapon_source(uint32_t,const Root *acquired=nullptr);
    bool source_matches(const WeaponFrame &,const Root *acquired=nullptr);
    void weapon_write(uint32_t,float,bool restore);
    void reconcile_weapons(bool publish_parent,bool live_service=false);
    void publish(uint32_t node,EventKind kind=EventKind::node,uint64_t epoch=0,uint32_t caller=0,
                 const ForeignWriter *writer=nullptr) noexcept;
    void drain();
    Root root();
    bool owns(const Root &,uint32_t node) const;
    Node &node(uint32_t address);
    LensSite site(uint32_t caller) const;
    std::array<uint32_t,6> parameters(uint32_t address) const;
    bool matches(uint32_t address,const Stamp &) const;
    std::optional<float> effective(uint32_t address,Node snapshot,const Root *acquired=nullptr);
    void finish_pending(std::optional<float> native_output = {});
    void capture_jeep(uint32_t address,float raw,LensSite kind);
    void capture_delta(uint32_t address,float raw,bool accumulate);
    void finish_delta();
    void capture_additive(uint32_t address,float raw);
    void finish_additive(uint32_t address,float output);
    void ensure(uint32_t address,bool forward_matrices=false);
    void rebuild(uint32_t address);
    void native_projection(uint32_t address);
    static float __cdecl fov(uint32_t address);
    static void __cdecl set_fov(uint32_t address,float value);
    static void __cdecl projection(uint32_t address);
    static void __cdecl matrices(uint32_t address);
    static uint32_t __cdecl get_projection(uint32_t address);
    static uint32_t __cdecl frustum(uint32_t address);
    static void __cdecl custom(uint32_t address,uint32_t matrix,uint32_t orthogonal);
    static void __cdecl weapons(uint32_t manager);
    static void __cdecl ctor1(uint32_t address,uint32_t database,uint32_t camera);
    static void __cdecl ctor2(uint32_t address,uint32_t vtt,uint32_t database,uint32_t camera);
    static void __cdecl dtor(uint32_t address,uint32_t vtt);
    static void __cdecl enable(uint32_t component,uint32_t value);
    static void __cdecl helper(uint32_t component,uint32_t dt,uint32_t force);
    static void __cdecl fast_node(uint32_t pointer);
    static float __cdecl weapon_diff(uint32_t manager);
    static void __cdecl jeep_ctor1(uint32_t component,uint32_t actor);
    static void __cdecl jeep_ctor2(uint32_t component,uint32_t actor);
    static void __cdecl level_ctor1(uint32_t level);
    static void __cdecl level_ctor2(uint32_t level);
    static void __cdecl level_reset(uint32_t level);
};
} // namespace nova::camera
