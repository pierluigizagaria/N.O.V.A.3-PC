#include "camera/native_world_fov.h"
#include "abi/guest_call.h"
#include "weapon_placement/native_rigid_draw_binding.h"
#include "movement_wobble/native_camera_bob.h"
#include <cstdlib>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <new>
#include <float.h>
#include <stdexcept>
#include <windows.h>
#include <xmmintrin.h>

namespace nova::camera {
namespace {
template<class T=uint32_t> T read(uint32_t p,uint32_t o=0) { return *reinterpret_cast<const T *>(p+o); }
template<class T> void write(uint32_t p,uint32_t o,T v) { *reinterpret_cast<T *>(p+o)=v; }
uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }
using abi::invoke;
constexpr auto fov_name="_ZNK6glitch5scene16CCameraSceneNode6getFOVEv";
constexpr auto set_name="_ZN6glitch5scene16CCameraSceneNode6setFOVEf";
constexpr auto proj_name="_ZN6glitch5scene16CCameraSceneNode27recalculateProjectionMatrixEv";
constexpr auto matrices_name="_ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv";
constexpr auto get_proj_name="_ZNK6glitch5scene16CCameraSceneNode19getProjectionMatrixEv";
constexpr auto frustum_name="_ZNK6glitch5scene16CCameraSceneNode14getViewFrustumEv";
float call_float(uint32_t fn,uint32_t receiver) { return abi::call_guest_float(fn,&receiver,1); }
const char *gate_name(NativeWorldFov::Gate gate) {
    using G=NativeWorldFov::Gate;
    switch(gate) {
    case G::no_local_camera:return "no-local-camera";
    case G::unsupported_owner:return "unsupported-owner";
    case G::unproved_lens:return "unproved-world-lens";
    case G::unproved_weapon_source:return "unproved-weapon-source";
    case G::unproved_standard_output:return "unproved-standard-output";
    case G::inactive_camera:return "inactive-camera";
    case G::applied:return "applied";
    default:return "not-observed";
    }
}
std::vector<LensCallSite> production_sites() {
    return {
        // Checked native producer CALLs. Unknown setters revoke lens origin.
        {"_ZN11CGameCamera11ResetCameraEv",0x86,LensSite::pseudo_write,{0xff,0x92,0x34,0x01,0x00,0x00}},
        // Actual observed writer: Fovy values are raw vertical radians. Count1
        // forwards its value; weighted values retain those units without aspect conversion.
        {"_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExIfNS1_25CCameraSceneNodeFovyMixinIfEEEEE15applyAddedValueEPKvPKfiPvPNS1_15CApplicatorInfoE",
            0x73,LensSite::vertical_write,{0xff,0x90,0x34,0x01,0x00,0x00}},
        // Main2/melee can choose direct, blended or key-based application.
        // All five native Fovy writers produce vertical radians. Omitting one
        // revoked world lens ownership and temporarily restored native FOV.
        {"_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExIfNS1_25CCameraSceneNodeFovyMixinIfEEEEE10applyValueEPKvPvPNS1_15CApplicatorInfoE",
            0x17,LensSite::vertical_write,{0xff,0x92,0x34,0x01,0x00,0x00}},
        {"_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExIfNS1_25CCameraSceneNodeFovyMixinIfEEEEE17applyBlendedValueEPKvPKfiPvPNS1_15CApplicatorInfoE",
            0x73,LensSite::vertical_write,{0xff,0x90,0x34,0x01,0x00,0x00}},
        {"_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExIfNS1_25CCameraSceneNodeFovyMixinIfEEEEE18applyKeyBasedValueERKNS0_18SAnimationAccessorEiPvPNS1_15CApplicatorInfoE",
            0x3f,LensSite::vertical_write,{0xff,0x90,0x34,0x01,0x00,0x00}},
        {"_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExIfNS1_25CCameraSceneNodeFovyMixinIfEEEEE18applyKeyBasedValueERKNS0_18SAnimationAccessorEiifPvPNS1_15CApplicatorInfoE",
            0x9a,LensSite::vertical_write,{0xff,0x90,0x34,0x01,0x00,0x00}},
        {"_ZN11CGameCamera6UpdateEi",0xb06,LensSite::pseudo_write,{0xff,0x92,0x34,0x01,0x00,0x00}},
        {"_ZN6CLevel12SetWeaponFOVEf",0x89,LensSite::pseudo_write,{0xff,0x97,0x34,0x01,0x00,0x00}},
        {"_ZN6CLevel12SetTargetFovEfib",0x7e,LensSite::pseudo_write,{0xff,0x90,0x34,0x01,0x00,0x00}},
        {"_ZN6CLevel6UpdateEv",0x2476,LensSite::pseudo_write,{0xff,0x90,0x34,0x01,0x00,0x00}},
        {"_ZN6CLevel6RenderEv",0x681,LensSite::additive_write,{0xff,0xd7}},
        {"_ZN6CLevel12SetWeaponFOVEf",0xf9,LensSite::raw,{0xff,0x92,0x20,0x01,0x00,0x00}},
        {"_ZN6CLevel12SetTargetFovEfib",0xfe,LensSite::raw,{0xff,0x92,0x20,0x01,0x00,0x00}},
        {"_ZN6CLevel6UpdateEv",0xef0,LensSite::level_first,{0xff,0x90,0x20,0x01,0x00,0x00}},
        {"_ZN6CLevel6UpdateEv",0x1a34,LensSite::level_accumulate,{0xff,0x90,0x20,0x01,0x00,0x00}},
        {"_ZN6CLevel6RenderEv",0x659,LensSite::render_read,{0xff,0x90,0x20,0x01,0x00,0x00}},
        {"_ZN17CNovaSceneManager11drawWeaponsEv",0x379,LensSite::weapon_begin,{0xff,0x90,0x20,0x01,0x00,0x00}},
        {"_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExIfNS1_25CCameraSceneNodeFovyMixinIfEEEEE13retrieveValueEPvS8_",0xe,LensSite::raw,{0xff,0x92,0x20,0x01,0x00,0x00}},
        {"_ZN20CPlayerJeepComponent6EnableEb",0x4f6,LensSite::jeep_save,{0xff,0x92,0x20,0x01,0x00,0x00}},
        {"_ZN20CPlayerJeepComponent6EnableEb",0x7ce,LensSite::jeep_enable,{0xff,0x92,0x20,0x01,0x00,0x00}},
        {"_ZN20CPlayerJeepComponent6UpdateEi",0x1ac,LensSite::jeep_update,{0xff,0x92,0x20,0x01,0x00,0x00}},
        {"_ZNK20CPlayerJeepComponent15UpdateCameraFOVEib",0x32,LensSite::jeep_helper,{0xff,0x92,0x20,0x01,0x00,0x00}},
        {"_ZN20CPlayerJeepComponent6EnableEb",0x89d,LensSite::jeep_write,{0xff,0x92,0x34,0x01,0x00,0x00}},
        {"_ZN20CPlayerJeepComponent6UpdateEi",0x1f6,LensSite::jeep_write,{0xff,0x92,0x34,0x01,0x00,0x00}},
        {"_ZNK20CPlayerJeepComponent15UpdateCameraFOVEib",0x134,LensSite::jeep_write,{0xff,0x92,0x34,0x01,0x00,0x00}},
        {"_ZN17CNovaSceneManager11drawWeaponsEv",0x397,LensSite::weapon_write,{0xff,0x90,0x34,0x01,0x00,0x00}},
        {"_ZN17CNovaSceneManager11drawWeaponsEv",0x252,LensSite::weapon_restore,{0xff,0x90,0x34,0x01,0x00,0x00}},
        {"_ZN20CPlayerJeepComponent6UpdateEi",0x21a,LensSite::fast_node,{0xe8,0x41,0x9f,0x13,0x00}},
    };
}
}
NativeWorldFov *NativeWorldFov::instance_=nullptr;
thread_local NativeWorldFov::JeepUpdateScope *NativeWorldFov::jeep_scope_=nullptr;
NativeWorldFov::NativeWorldFov(const ElfImage &image,std::optional<float> setting,
                             const std::vector<LensCallSite> *fixture_sites,
                             bool (*fixture_fail_lifecycle_allocation)(),std::optional<float> weapon_setting,
                             bool prepare_interactive)
    : setting_(setting),weapon_setting_(weapon_setting),prepared_(prepare_interactive),
      installed_(prepare_interactive || setting || weapon_setting),
      track_weapon_(prepare_interactive || weapon_setting),
      fixture_fail_lifecycle_allocation_(fixture_fail_lifecycle_allocation) {
    status_.requested=status_.committed=Options{setting,weapon_setting};
    if (!installed_) return;
    if (setting_ && (!std::isfinite(*setting_) || *setting_<60 || *setting_>120))
        throw std::runtime_error("world_fov must be native or finite horizontal degrees from 60 to 120 at 16:9");
    if (weapon_setting_ && (!std::isfinite(*weapon_setting_) || *weapon_setting_<60 || *weapon_setting_>104))
        throw std::runtime_error("weapon_fov must be native or finite horizontal degrees from 60 to 104 at 16:9");
    if (instance_) throw std::runtime_error("Only one native world FOV adapter is supported");
    get_level_=abi::require_function(image,"_ZN6CLevel8GetLevelEv");
    get_player_=abi::require_function(image,"_ZN6CLevel18GetPlayerComponentEv");
    get_jeep_=abi::require_function(image,"_ZN6CLevel22GetPlayerJeepComponentEv");
    auto data=[&](const char *name) {
        auto p=image.symbol(name);
        if (!p || !image.contains(p)) throw std::runtime_error(std::string("Missing world FOV data: ")+name);
        return p;
    };
    screen_w_=data("g_ScreenW"); screen_h_=data("g_ScreenH");
    static_assert(sizeof(int32_t)==4);
    if(uint64_t(screen_w_)+3>UINT32_MAX || uint64_t(screen_h_)+3>UINT32_MAX ||
       !image.contains(screen_w_+3) || !image.contains(screen_h_+3))
        throw std::runtime_error("world FOV scalar dimension bounds mismatch");
    base_=data("_ZN11CGameCamera11DEFAULT_FOVE");
    vtable_=data("_ZTV22CCustomCameraSceneNode")+0x10;
    scene_manager_=data("g_sceneManager");
    render_camera_=abi::require_function(image,"_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv");
    constexpr uint8_t render_entry[]={0x55,0x89,0xe5,0x81,0xec,0xe8,0x00,0x00,0x00};
    if (!image.executable(render_camera_+sizeof(render_entry)-1) ||
        std::memcmp(reinterpret_cast<void *>(render_camera_),render_entry,sizeof(render_entry)) ||
        read(vtable_,8)!=render_camera_)
        throw std::runtime_error("world FOV native camera setup contract mismatch");
    auto sites=fixture_sites ? *fixture_sites : production_sites();
    if (track_weapon_) {
        tweak_=data("_ZN16WeaponsTweakable10s_instanceE");
        if (!fixture_sites) sites.push_back({"_ZN17CNovaSceneManager11drawWeaponsEv",0xa6,
            LensSite::weapon_diff,{0xe8,0xb5,0x35,0x37,0x00}});
    }
    for (const auto &s:sites) {
        if (s.kind==LensSite::weapon_diff && !track_weapon_) continue;
        auto owner=abi::require_function(image,s.owner);
        if (uint64_t(owner)+s.offset+s.expected.size()>UINT32_MAX ||
            s.expected.empty() || !image.executable(owner+s.offset) ||
            !image.executable(owner+s.offset+uint32_t(s.expected.size())-1) ||
            std::memcmp(reinterpret_cast<void *>(owner+s.offset),s.expected.data(),s.expected.size()))
            throw std::runtime_error("world FOV producer CALL mismatch: "+s.owner);
        sites_.emplace(owner+s.offset+uint32_t(s.expected.size()),s.kind);
    }
    auto hook=[&](const char *name,std::initializer_list<uint8_t> expected,auto replacement) {
        auto item=std::make_unique<abi::GuestHook>(image,name,expected,reinterpret_cast<uint32_t>(replacement));
        auto original=item->original(); hooks_.push_back(std::move(item)); return original;
    };
    original_fov_=hook(fov_name,{0x55,0x89,0xe5,0x8b,0x45,0x08},&fov);
    original_set_=hook(set_name,{0x55,0x89,0xe5,0x8b,0x45,0x08},&set_fov);
    original_projection_=hook(proj_name,{0x55,0x89,0xe5,0x83,0xe4,0xf0},&projection);
    original_matrices_=hook(matrices_name,{0x55,0x89,0xe5,0x83,0xe4,0xf0},&matrices);
    original_get_projection_=hook(get_proj_name,{0x55,0x89,0xe5,0x56,0x83,0xec,0x14},&get_projection);
    original_frustum_=hook(frustum_name,{0x55,0x89,0xe5,0x8b,0x45,0x08},&frustum);
    original_custom_=hook("_ZN6glitch5scene16CCameraSceneNode19setProjectionMatrixERKNS_4core8CMatrix4IfEEb",
        {0x55,0x89,0xe5,0x8b,0x55,0x0c},&custom);
    original_weapons_=hook("_ZN17CNovaSceneManager11drawWeaponsEv",{0x55,0x89,0xe5,0x57,0x56,0x53},&weapons);
    original_ctor1_=hook("_ZN22CCustomCameraSceneNodeC1ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE",
        {0x55,0x89,0xe5,0x56,0x53},&ctor1);
    original_ctor2_=hook("_ZN22CCustomCameraSceneNodeC2ERKN6glitch7collada16CColladaDatabaseERNS1_7SCameraE",
        {0x55,0x89,0xe5,0x83,0xec,0x28},&ctor2);
    original_dtor_=hook("_ZN6glitch7collada16CCameraSceneNodeD2Ev",{0x55,0x89,0xe5,0x57,0x56},&dtor);
    original_enable_=hook("_ZN20CPlayerJeepComponent6EnableEb",{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},&enable);
    original_helper_=hook("_ZNK20CPlayerJeepComponent15UpdateCameraFOVEib",{0x55,0x89,0xe5,0x83,0xe4,0xf0},&helper);
    original_jeep_ctor1_=hook("_ZN20CPlayerJeepComponentC1EP11CGameObject",{0x55,0x89,0xe5,0x57,0x56,0x53},&jeep_ctor1);
    original_jeep_ctor2_=hook("_ZN20CPlayerJeepComponentC2EP11CGameObject",{0x55,0x89,0xe5,0x57,0x56,0x53},&jeep_ctor2);
    original_level_ctor1_=hook("_ZN6CLevelC1Ev",{0x55,0x89,0xe5,0x57,0x56,0x53},&level_ctor1);
    original_level_ctor2_=hook("_ZN6CLevelC2Ev",{0x55,0x89,0xe5,0x57,0x56,0x53},&level_ctor2);
    original_level_reset_=hook("_ZN6CLevel5ResetEv",{0x55,0x89,0xe5,0x57,0x56,0x53},&level_reset);
    for (const auto &s:sites) if (s.kind==LensSite::fast_node) {
        fast_node_call_=std::make_unique<abi::GuestCall>(image,s.owner,s.offset,
            "_Z14FastNodeUpdateN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE",reinterpret_cast<uint32_t>(&fast_node));
        original_fast_node_=fast_node_call_->original();
    }
    for (const auto &s:sites) if (s.kind==LensSite::weapon_diff && track_weapon_) {
        weapon_diff_call_=std::make_unique<abi::GuestCall>(image,s.owner,s.offset,
            "_ZN14CWeaponManager23GetCurrentWeaponFovDiffEv",reinterpret_cast<uint32_t>(&weapon_diff));
        original_weapon_diff_=weapon_diff_call_->original();
    }
    if(track_weapon_) {
        eligibility_=std::make_unique<viewmodel_eligibility::NativeProducer>(image,fixture_sites!=nullptr);
        original_camera_render_=hook("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv",
            {0x55,0x89,0xe5,0x81,0xec,0xe8,0x00,0x00,0x00},&camera_render);
        uint32_t offset=0x53;
        if(fixture_sites) {
            const auto call=image.symbol("fov_eligibility_projection_call");
            if(call<render_camera_) throw std::runtime_error("Missing fixture first projection CALL");
            offset=call-render_camera_;
        }
        bool final_sink=!fixture_sites;
        for(const auto &site:sites) if(site.kind==LensSite::camera_projection) final_sink=true;
        if(final_sink) {
            projection_return_=render_camera_+offset+5;
            camera_epilogue_=fixture_sites ? image.symbol("fov_eligibility_camera_epilogue") : render_camera_+0x96;
            const std::vector<uint8_t> epilogue=fixture_sites ? std::vector<uint8_t>{0xc9,0xc3} :
                std::vector<uint8_t>{0x8b,0x5d,0xf4,0x8b,0x75,0xf8,0x8b,0x7d,0xfc,0x89,0xec,0x5d,0xc3};
            if(!camera_epilogue_ || !image.executable(camera_epilogue_+uint32_t(epilogue.size())-1) ||
               std::memcmp(reinterpret_cast<void *>(camera_epilogue_),epilogue.data(),epilogue.size()))
                throw std::runtime_error("O09 camera cancellation epilogue mismatch");
            camera_projection_call_=std::make_unique<abi::GuestCall>(image,
                "_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv",offset,
                "_ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj",
                reinterpret_cast<uint32_t>(&driver_projection));
            original_driver_projection_=camera_projection_call_->original();
        }
    }
    if(track_weapon_ && (!fixture_sites || image.symbol("v01_rigid_node_table"))) {
        rigid_binding_=std::make_unique<weapon_placement::NativeRigidDrawBinding>(image,fixture_sites!=nullptr);
        rigid_model_return_=fixture_sites ? image.symbol("v01_rigid_model_return") : image.address(0x8ddb1c,1);
        const auto draw_entry=fixture_sites ? image.symbol("v01_rigid_draw_entry") : image.address(0x8ddad0,1);
        const auto call=fixture_sites ? image.symbol("v01_rigid_model_call") : image.address(0x8ddb19,3);
        constexpr uint8_t call_bytes[]={0xff,0x52,0x44};
        if(!draw_entry || !call || call<draw_entry || rigid_model_return_!=call+3 ||
           (!fixture_sites && call-draw_entry!=0x49) || !image.executable(call) ||
           !image.executable(rigid_model_return_) ||
           std::memcmp(reinterpret_cast<const void*>(call),call_bytes,sizeof(call_bytes)))
            throw std::runtime_error("Rigid model supplied-call boundary mismatch");
        original_rigid_model_=hook("_ZN6glitch7collada5IMesh12setTransformEPNS_5video12IVideoDriverERKNS_4core8CMatrix4IfEE",
            {0x55,0x89,0xe5,0x53,0x83,0xec,0x14},&rigid_model);
    }
    instance_=this;
}
NativeWorldFov::~NativeWorldFov() {
    if (!installed_) return;
    // Installation/removal requires guest quiescence, like every GuestHook.
    rigid_binding_.reset();
    camera_projection_call_.reset(); eligibility_.reset();
    weapon_diff_call_.reset(); fast_node_call_.reset(); hooks_.clear(); instance_=nullptr;
    auto *event=events_.exchange(nullptr);
    while (event) { auto *next=event->next; delete event; event=next; }
}
bool NativeWorldFov::lane() const { return lane_.load(std::memory_order_relaxed)==GetCurrentThreadId(); }
bool NativeWorldFov::native_weapon_mode() const {
    return regular_frame()!=nullptr || (track_weapon_ && weapon_setting_ && weapon_frame_ && !weapon_frame_->restored);
}
NativeWorldFov::WeaponFrame *NativeWorldFov::regular_frame() const {
    for (auto *frame=weapon_frame_;frame;frame=frame->parent)
        if (frame->regular && !frame->restored) return frame;
    return nullptr;
}
void NativeWorldFov::bind_render_thread() {
    if (!installed_) return;
    lane_.store(GetCurrentThreadId(),std::memory_order_relaxed);
    if(eligibility_) eligibility_->bind_render_thread(GetCurrentThreadId());
    drain();
}
bool NativeWorldFov::set_rigid_draw_request(weapon_placement::Position value) {
    if(!installed_ || !lane() || guest_depth_ || !weapon_placement::valid(value)) return false;
    rigid_request_=weapon_placement::canonical(value);
    rigid_request_active_.store(rigid_request_.x!=0 || rigid_request_.y!=0,std::memory_order_relaxed); return true;
}
__declspec(noinline) void __cdecl NativeWorldFov::rigid_model(uint32_t mesh,uint32_t driver,uint32_t model) noexcept {
    try {
        auto &s=*instance_;
        // Required default bypass BEFORE lane/owners/pose sampling and counters.
        if(!s.rigid_requested()) { invoke(s.original_rigid_model_,{mesh,driver,model}); return; }
        if(s.lane() && s.weapon_frame_ && s.rigid_binding_ && model>=0x10 &&
           reinterpret_cast<uint32_t>(_ReturnAddress())==s.rigid_model_return_) {
            ++s.rigid_status_.samples;
            // Scoped result expires before the original native continuation.
            const auto sample=s.rigid_binding_->sample(s.weapon_frame_->rigid_owners,model-0x10,mesh,driver,model);
            if(sample.decision.permits(sample.boundary)) ++s.rigid_status_.admitted;
        }
        // Structural installation only. Every path forwards exact arguments
        // once. No pose/driver mutation, pending batch, or cleanup write exists.
        invoke(s.original_rigid_model_,{mesh,driver,model});
    } catch(...) { std::fputs("V01i rigid model callback failure\n",stderr); std::abort(); }
}
bool NativeWorldFov::request_options(Options next) {
    auto valid=[](std::optional<float> value,float maximum) {
        return !value || (std::isfinite(*value) && *value>=60 && *value<=maximum);
    };
    if (!prepared_ || !valid(next.world,120) || !valid(next.weapon,104)) return false;
    std::lock_guard lock(options_mutex_);
    if (status_.requested==next) return true;
    // Cancel the preceding completed edit atomically with a new request,
    // including a render observer holding an older copied status.
    trace_remaining_=0;
    status_.requested=next; ++status_.requested_sequence;
    return true;
}
NativeWorldFov::LiveStatus NativeWorldFov::live_status() const {
    std::lock_guard lock(options_mutex_); return status_;
}
void NativeWorldFov::trace_completed_edit(Options completed) {
    uint64_t edit,sequence;
    bool queued;
    {
        std::lock_guard lock(options_mutex_);
        edit=++trace_edit_; sequence=status_.requested_sequence;
        queued=status_.requested==completed;
        trace_sequence_=sequence; trace_target_=completed;
        trace_remaining_=4; trace_observed_=false; trace_last_ms_=0;
        trace_until_=GetTickCount64()+5000;
    }
    std::printf("fov-edit id=%llu sequence=%llu prepared=%d queued=%d world=%s%.6g weapon=%s%.6g\n",
        edit,sequence,prepared_,queued,completed.world ? "" : "native/",completed.world.value_or(0),
        completed.weapon ? "" : "native/",completed.weapon.value_or(0));
}
void NativeWorldFov::trace_status(const LiveStatus &current) {
    uint64_t edit;
    {
        std::lock_guard lock(options_mutex_);
        const auto now=GetTickCount64();
        if(current.requested_sequence!=trace_sequence_ || current.requested!=trace_target_) {
            // A later drag supersedes the completed action; never trace drag
            // updates under the preceding action's diagnostic budget.
            if(current.requested_sequence>trace_sequence_) trace_remaining_=0;
            return;
        }
        if(!trace_remaining_ || now>trace_until_ || (trace_last_ms_ && now-trace_last_ms_<250)) return;
        if(trace_observed_ && current.requested_sequence==trace_last_.requested_sequence &&
           current.committed_sequence==trace_last_.committed_sequence &&
           current.world_application==trace_last_.world_application &&
           current.weapon_application==trace_last_.weapon_application &&
           current.world_gate==trace_last_.world_gate && current.weapon_gate==trace_last_.weapon_gate &&
           current.world_evidence.leaf==trace_last_.world_evidence.leaf &&
           current.world_evidence.additive==trace_last_.world_evidence.additive &&
           current.world_evidence.delta_reason==trace_last_.world_evidence.delta_reason &&
           current.world_evidence.upstream==trace_last_.world_evidence.upstream && current.weapon_leaf==trace_last_.weapon_leaf) return;
        trace_last_=current; trace_observed_=true; trace_last_ms_=now;
        --trace_remaining_; edit=trace_edit_;
    }
    const auto &w=current.world_evidence;
    std::printf("fov-state id=%llu requested=%llu committed=%llu epoch=%llu world=%s weapon=%s published=%llu/%llu "
        "worldLeaf=%s phase=%s node=%08x lens=%d rawSeen=%d raw=%08x reference=%08x "
        "establish=%s@%08x/lane%u revoke=%s@%08x additive=%s refD=%s/life%llu/order%llu upstream=%s@%08x/lane%u/origin%llu weaponLeaf=%s stages=%u\n",
        edit,current.requested_sequence,current.committed_sequence,current.option_epoch,
        gate_name(current.world_gate),gate_name(current.weapon_gate),
        current.world_applied_frame,current.weapon_applied_frame,w.leaf,w.phase,w.node,w.lens_present,w.raw_observed,
        w.raw,w.reference,w.established,w.establish_site,w.establish_lane,w.revoked,w.revoke_site,
        w.additive,w.delta_reason,w.delta_lifetime,w.delta_order,w.upstream,w.upstream_site,w.upstream_lane,w.upstream_origin,
        current.weapon_leaf,unsigned(current.weapon_stages));
}
void NativeWorldFov::established(uint32_t p,const char *kind,uint32_t caller) {
    auto &h=lens_history_[p]; h.established=kind; h.establish_site=caller;
    h.establish_lane=GetCurrentThreadId(); h.revoked="none"; h.revoke_site=0;
    h.additive="not-observed"; h.delta={}; h.last={}; h.upstream={};
}
void NativeWorldFov::revoked(uint32_t p,const char *kind,uint32_t caller) {
    auto &h=lens_history_[p]; h.revoked=kind; h.revoke_site=caller;
    const auto n=nodes_.find(p);
    h.last={kind,caller,GetCurrentThreadId(),n==nodes_.end() ? 0 : n->second.origin};
}
void NativeWorldFov::world_rejected(const char *leaf,uint32_t p,const Node &n,std::optional<uint32_t> raw) {
    world_gate_=Gate::unproved_lens;
    if(std::strcmp(world_evidence_.leaf,"not-observed")!=0) return;
    world_evidence_={}; auto &w=world_evidence_;
    w.leaf=leaf; w.phase=service_depth_ ? "service" : weapon_frame_ ? "weapon-return" : "consumer";
    w.node=p; w.lens_present=bool(n.lens); w.raw_observed=bool(raw); w.raw=raw.value_or(0);
    if(n.lens) w.reference=bits(n.lens->native_vertical);
    const auto h=lens_history_.find(p);
    if(h!=lens_history_.end()) {
        w.established=h->second.established; w.establish_site=h->second.establish_site;
        w.establish_lane=h->second.establish_lane; w.revoked=h->second.revoked; w.revoke_site=h->second.revoke_site;
        w.additive=h->second.additive; w.delta_reason=h->second.delta.reason;
        w.delta_lifetime=h->second.delta.lifetime; w.delta_order=h->second.delta.order;
        w.upstream=h->second.upstream.kind; w.upstream_site=h->second.upstream.site;
        w.upstream_lane=h->second.upstream.lane; w.upstream_origin=h->second.upstream.origin;
    }
}
void NativeWorldFov::weapon_observed(const char *leaf) {
    // Explicit diagnostic only: bounded distinct reasons, no periodic hot-path log.
    static const bool trace=std::getenv("NOVA_TRACE_VIEWMODEL")!=nullptr;
    if(trace) {
        static thread_local std::array<const char *,16> seen{};
        for(auto &entry:seen) {
            if(entry && std::strcmp(entry,leaf)==0) break;
            if(!entry) { entry=leaf; std::printf("viewmodel-reason %s\n",leaf); break; }
        }
    }
    if(std::strcmp(weapon_leaf_,"not-observed")!=0) return;
    weapon_leaf_=leaf;
    if(auto *f=weapon_frame_) weapon_stages_=uint8_t(bool(f->before)|(bool(f->current)<<1)|
        (bool(f->source)<<2)|(bool(f->sample)<<3));
    else weapon_stages_=0;
}
void NativeWorldFov::invalidate_application() {
    if (!prepared_) return;
    world_applied_epoch_=weapon_applied_epoch_=UINT64_MAX;
    std::lock_guard lock(options_mutex_);
    status_.application=status_.world_application=status_.weapon_application=Application::deferred;
}
void NativeWorldFov::publish_status(uint64_t frame,bool eligible) {
    LiveStatus copy;
    {
    std::lock_guard lock(options_mutex_);
    status_.committed=Options{setting_,weapon_setting_}; status_.committed_sequence=committed_sequence_;
    status_.option_epoch=option_epoch_;
    if (eligible) status_.eligible_frame=frame;
    status_.world_applied_frame=world_applied_frame_; status_.weapon_applied_frame=weapon_applied_frame_;
    status_.world_application=world_applied_epoch_==option_epoch_ ? Application::applied : Application::deferred;
    status_.weapon_application=weapon_applied_epoch_==option_epoch_ ? Application::applied : Application::deferred;
    status_.application=status_.world_application==Application::applied &&
        status_.weapon_application==Application::applied ? Application::applied : Application::deferred;
    status_.world_gate=status_.world_application==Application::applied ? Gate::applied : world_gate_;
    status_.weapon_gate=status_.weapon_application==Application::applied ? Gate::applied : weapon_gate_;
    status_.world_evidence=world_evidence_; status_.weapon_leaf=weapon_leaf_; status_.weapon_stages=weapon_stages_;
    copy=status_;
    }
    trace_status(copy);
}
void NativeWorldFov::service_options(uint64_t frame) {
    if (!prepared_ || !lane() || guest_depth_ || service_depth_ || weapon_frame_ ||
        jeep_scope_ || scoped_component_ || matrix_depth_ || stage_depth_) return;
    Options next; uint64_t sequence;
    {
        std::lock_guard lock(options_mutex_);
        next=status_.requested; sequence=status_.requested_sequence;
    } // Never carry the mailbox lock into guest queries or native rebuilds.
    const bool changed=next!=Options{setting_,weapon_setting_};
    if (changed) { setting_=next.world; weapon_setting_=next.weapon; ++option_epoch_; }
    committed_sequence_=sequence;
    if (!sequence && !setting_ && !weapon_setting_) {
        // Only an explicitly completed repeated Reset may need a host-only
        // observation; unchanged prepared Native still adds no guest work.
        trace_status(live_status()); return;
    }
    service_frame_=frame;
    world_gate_=Gate::unobserved; weapon_gate_=Gate::unobserved;
    world_evidence_={}; weapon_leaf_="not-observed"; weapon_stages_=0;
    const bool awaiting=world_applied_epoch_!=option_epoch_;
    world_applied_epoch_=UINT64_MAX;
    ++service_depth_;
    try {
        // No cached receiver: R1 reacquires the supported active lifetime and
        // publishes its checked native standard-builder projection AND view.
        reconcile_weapons(changed || awaiting,true);
    } catch (...) { --service_depth_; throw; }
    --service_depth_;
    publish_status(frame,true);
}
bool NativeWorldFov::prepare_render_extent(int ow,int oh,int nw,int nh) {
    if(ow<=0 || oh<=0 || nw<=0 || nh<=0 || int64_t(ow)*nh!=int64_t(nw)*oh ||
       double(oh)/ow!=double(nh)/nw) return false;
    if(!installed_) return true; // No custom adapter or retained lens exists.
    if(!lane() || guest_depth_ || service_depth_ || weapon_frame_ || jeep_scope_ ||
       scoped_component_ || matrix_depth_ || stage_depth_ || render_extent_proof_) return false;
    drain(); finish_pending();
    if(read<int32_t>(screen_w_)!=ow || read<int32_t>(screen_h_)!=oh ||
       events_.load(std::memory_order_acquire)) return false;
    const auto current=root();
    const auto main=current ? read(current.camera,0x26c) : 0;
    RenderExtentProof proof{current,ow,oh,nw,nh,publication_serial_.load(std::memory_order_relaxed),{}};
    bool main_lens=false;
    for(const auto &[p,n]:nodes_) {
        if(n.root!=current || !n.lens || !owns(current,p)) continue;
        const auto raw=read(p,0x128);
        if(raw!=bits(n.lens->native_vertical)) return false;
        proof.witnesses.push_back({p,raw,n.generation,n.origin,n.write_epoch,n.raw_epoch,*n.lens,parameters(p),{}});
        std::memcpy(proof.witnesses.back().matrix.data(),reinterpret_cast<const void *>(p+0x200),64);
        if(p==main) main_lens=true;
    }
    // In a scene with custom optics, an observed current lens is required.
    // Menus/native fallback do not manufacture camera provenance.
    if(current && (setting_ || weapon_setting_) && !main_lens) return false;
    if(events_.load(std::memory_order_acquire) ||
       proof.serial!=publication_serial_.load(std::memory_order_relaxed)) return false;
    {
        std::lock_guard lock(foreign_mutex_);
        for(const auto &[p,state]:foreign_state_) {
            (void)p; if(state.active || state.builders) return false;
        }
    }
    render_extent_proof_=std::move(proof);
    return true;
}
bool NativeWorldFov::verify_render_extent(bool changed) {
    if(!installed_) return true;
    if(!lane() || !render_extent_proof_) return false;
    const auto &proof=*render_extent_proof_;
    if(read<int32_t>(screen_w_)!=(changed ? proof.new_width : proof.old_width) ||
       read<int32_t>(screen_h_)!=(changed ? proof.new_height : proof.old_height) ||
       events_.load(std::memory_order_acquire) ||
       proof.serial!=publication_serial_.load(std::memory_order_relaxed) || root()!=proof.root) return false;
    for(const auto &w:proof.witnesses) {
        // Root identifies the camera controller; its active child may change
        // without changing Root. Reacquire ownership before touching that child.
        if(!owns(proof.root,w.node)) return false;
        const auto found=nodes_.find(w.node);
        if(found==nodes_.end()) return false;
        const auto &n=found->second;
        if(n.root!=proof.root || n.generation!=w.generation || n.origin!=w.origin ||
           n.write_epoch!=w.write_epoch || n.raw_epoch!=w.raw_epoch || !n.lens ||
           bits(n.lens->native_vertical)!=bits(w.lens.native_vertical) ||
           n.lens->reference_vertical!=w.lens.reference_vertical || read(w.node,0x128)!=w.raw ||
           parameters(w.node)!=w.parameters ||
           std::memcmp(reinterpret_cast<const void *>(w.node+0x200),w.matrix.data(),64)) return false;
    }
    return proof.serial==publication_serial_.load(std::memory_order_relaxed) &&
        !events_.load(std::memory_order_acquire);
}
void NativeWorldFov::finish_render_extent(bool changed) {
    if(!installed_) return;
    if(!verify_render_extent(changed)) throw std::runtime_error("Render extent lens proof changed");
    if(changed) {
        // ReferenceLens is already normalized to16:9; equality of both exact
        // aspect and native ratio proves that its numerical authority survives.
        // Rebuild via the original standard projection/view path on this lane.
        for(const auto &w:render_extent_proof_->witnesses) {
            auto &n=nodes_.at(w.node); n.refresh=true; n.stamp.reset();
            write<uint16_t>(w.node,0x300,uint16_t(read<uint16_t>(w.node,0x300)|2));
        }
        invalidate_application();
    }
    render_extent_proof_.reset();
}
void NativeWorldFov::publish(uint32_t address,EventKind kind,uint64_t epoch,uint32_t caller,const ForeignWriter *writer) noexcept {
    if(kind==EventKind::node) invalidate_foreign(address,true);
    publication_serial_.fetch_add(1,std::memory_order_relaxed);
    auto *event=fixture_fail_lifecycle_allocation_ && fixture_fail_lifecycle_allocation_() ?
        nullptr : new(std::nothrow) Event{address,kind,epoch,nullptr,caller,GetCurrentThreadId(),{}};
    if (!event) {
        static constexpr char message[]="world_fov: fatal lifecycle event allocation failure\n";
        DWORD written=0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE),message,sizeof(message)-1,&written,nullptr);
        TerminateProcess(GetCurrentProcess(),74); __assume(false);
    }
    auto *head=events_.load(std::memory_order_relaxed);
    if(writer) event->writer=*writer;
    do { event->next=head; } while (!events_.compare_exchange_weak(head,event,std::memory_order_release,std::memory_order_relaxed));
}
NativeWorldFov::ForeignWriter NativeWorldFov::begin_foreign(uint32_t p,uint32_t caller) {
    ForeignWriter w;
    {
        std::lock_guard lock(foreign_mutex_); auto &s=foreign_state_[p];
        if(!s.active && !s.builders) s.overlap=false; else s.overlap=true;
        ++s.active; s.token=++foreign_serial_;
        w.token=s.token; w.lifetime=s.lifetime; w.caller=caller; w.lane=GetCurrentThreadId();
    }
    publish(p,EventKind::raw_begin,0,caller,&w); return w;
}
bool NativeWorldFov::end_foreign(uint32_t p,const ForeignWriter &w) {
    std::lock_guard lock(foreign_mutex_); auto &s=foreign_state_[p];
    const bool valid=s.active==1 && !s.builders && !s.overlap && s.token==w.token && s.lifetime==w.lifetime;
    --s.active; return valid;
}
void NativeWorldFov::invalidate_foreign(uint32_t p,bool lifetime) {
    std::lock_guard lock(foreign_mutex_); auto &s=foreign_state_[p];
    s.token=++foreign_serial_; if(lifetime) s.lifetime=s.token;
    if(s.active || s.builders) s.overlap=true;
}
void NativeWorldFov::foreign_builder(uint32_t p,bool begin) {
    {
        std::lock_guard lock(foreign_mutex_); auto &s=foreign_state_[p];
        if(begin) { ++s.builders; if(s.active) s.overlap=true; }
        else --s.builders;
    }
    publish(p,begin ? EventKind::standard_begin : EventKind::standard_external);
}
bool NativeWorldFov::valid_foreign(uint32_t p,const ForeignWriter &w,bool *busy) {
    std::lock_guard lock(foreign_mutex_);
    const auto it=foreign_state_.find(p);
    const bool current=it!=foreign_state_.end() && !it->second.overlap &&
        it->second.token==w.token && it->second.lifetime==w.lifetime;
    const bool pending=current && (it->second.active || it->second.builders);
    if(busy) *busy=pending;
    return current && !pending;
}
void NativeWorldFov::consume_foreign(uint32_t p,const Root &current) {
    const auto found=foreign_pseudo_.find(p); if(found==foreign_pseudo_.end()) return;
    const auto pending=found->second;
    const auto &w=pending.writer;
    bool busy=false; const bool valid=valid_foreign(p,w,&busy);
    if(busy) return;
    foreign_pseudo_.erase(found);
    const auto before=nodes_.find(p);
    const Node snapshot=before==nodes_.end() ? Node{} : before->second;
    auto reject=[&](const char *reason,std::optional<uint32_t> raw_value={}) {
        revoked(p,reason,w.caller); world_rejected(reason,p,snapshot,raw_value);
    };
    if(!w.coherent) { reject("foreign-dimensions-unproved"); return; }
    if(!valid) { reject("foreign-order-lifetime"); return; }
    if(pending.root && pending.root!=current) { reject("foreign-root"); return; }
    if(pending.generation && (before==nodes_.end() || before->second.generation!=pending.generation)) {
        reject("foreign-generation"); return;
    }
    if(!owns(current,p)) { reject("foreign-owner"); return; }
    const auto raw=read(p,0x128);
    if(raw!=w.raw) { reject("foreign-raw-mismatch",raw); return; }
    // Two scalar exception only. A changed writer pair is conservatively
    // deferred; never reinterpret historical raw radians with a later ratio.
    if(read<int32_t>(screen_w_)!=w.width || read<int32_t>(screen_h_)!=w.height) {
        reject("foreign-dimensions-changed",raw); return;
    }
    const auto lens=pseudo_horizontal_lens(std::bit_cast<float>(w.raw),w.width,w.height);
    if(!lens || !valid_foreign(p,w) || events_.load(std::memory_order_acquire)) {
        reject("foreign-final-fence",raw); return;
    }
    if(!pending.standard && !(read<uint16_t>(p,0x300)&2)) {
        reject("foreign-standard-unproved",raw); return;
    }
    auto &n=node(p); n.root=current; n.lens=lens; ++n.origin;
    // Numerical authority comes from the attested writer, never this matrix.
    // Its checked native standard builder (or still-dirty native setter) permits
    // rebuilding through the normal original full-geometry path on this lane.
    write<uint16_t>(p,0x300,uint16_t(read<uint16_t>(p,0x300)|2)); n.refresh=true;
    established(p,"foreign-known-pseudo",w.caller); lens_history_[p].establish_lane=w.lane;
}
void NativeWorldFov::drain() {
    auto *event=events_.exchange(nullptr,std::memory_order_acquire);
    // Publish is a stack. Apply begin/end lifecycle records in publication order.
    Event *ordered=nullptr;
    while(event) { auto *next=event->next; event->next=ordered; ordered=event; event=next; }
    event=ordered;
    while (event) {
        invalidate_application();
        if (event->kind==EventKind::level_seed) {
            if (level_epoch_[event->node]==event->epoch) {
                delta_seed_[event->node]=true;
                auto &h=delta_history_[event->node]; h.reason="level-zero-seed"; h.order=event->epoch;
            }
        } else if (event->kind==EventKind::level) {
            level_epoch_[event->node]=event->epoch;
            delta_history_[event->node]={"level-begin",event->epoch,event->epoch};
            delta_seed_[event->node]=false;
            if (!observed_ || event->node==observed_root_.level) {
                delta_.reset(); delta_pending_.reset(); add_pending_.reset();
                saved_.clear(); pending_.reset();
                for (auto &[address,n]:nodes_) { revoked(address,"level"); n.lens.reset(); ++n.origin; }
            }
        } else if (event->kind==EventKind::component) {
            saved_.erase(event->node);
            if (pending_ && pending_->component==event->node) pending_.reset();
        } else if (event->kind==EventKind::external || event->kind==EventKind::raw_external ||
                   event->kind==EventKind::raw_begin || event->kind==EventKind::pseudo_external ||
                   event->kind==EventKind::standard_begin || event->kind==EventKind::standard_external) {
            if(event->kind==EventKind::external || event->kind==EventKind::raw_external || event->kind==EventKind::raw_begin)
                foreign_pseudo_.erase(event->node);
            if(event->kind==EventKind::pseudo_external && event->writer.coherent) {
                bool busy=false;
                if(valid_foreign(event->node,event->writer,&busy) || busy) {
                    const auto n=nodes_.find(event->node);
                    foreign_pseudo_[event->node]=PendingForeign{event->writer,n==nodes_.end() ? 0 : n->second.generation,observed_root_,false};
                }
            }
            if(event->kind==EventKind::standard_external) {
                const auto f=foreign_pseudo_.find(event->node);
                if(f!=foreign_pseudo_.end() && valid_foreign(event->node,f->second.writer)) f->second.standard=true;
            }
            revoked(event->node,event->kind==EventKind::raw_external ?
                (site(event->caller)==LensSite::pseudo_write ? "foreign-known-pseudo" : "foreign-raw") : "foreign-builder",event->caller);
            auto found=nodes_.find(event->node);
            if (found!=nodes_.end()) {
                found->second.lens.reset(); ++found->second.origin; found->second.refresh=true;
                ++found->second.write_epoch;
                if (event->kind==EventKind::raw_external) ++found->second.raw_epoch;
            }
            if (pending_ && pending_->node==event->node) pending_.reset();
            if (add_pending_ && add_pending_->node==event->node) add_pending_.reset();
        } else {
            foreign_pseudo_.erase(event->node);
            revoked(event->node,"lifecycle");
            lens_history_[event->node].established="none";
            lens_history_[event->node].additive="not-observed"; lens_history_[event->node].delta={};
            lens_history_[event->node].last={}; lens_history_[event->node].upstream={};
            lens_history_[event->node].establish_site=lens_history_[event->node].establish_lane=0;
            nodes_.erase(event->node);
            if (pending_ && pending_->node==event->node) pending_.reset();
            if (add_pending_ && add_pending_->node==event->node) add_pending_.reset();
        }
        auto *next=event->next; delete event; event=next;
    }
}
NativeWorldFov::Root NativeWorldFov::root() {
    // Inside the native camera continuation, never reenter component virtuals.
    // This capsule was acquired before admission; eligibility still samples
    // current fields and original pure liveness at each actual consumption.
    if(camera_scope_) return camera_scope_->owners;
    Root r; r.level=invoke(get_level_);
    if (r.level) r.player=invoke(get_player_,{r.level});
    if (r.player) { r.actor=read(r.player,4); r.camera=read(r.player,0x178); }
    if (!r) r={};
    const auto component=r ? invoke(get_jeep_,{r.level}) : 0;
    if (observed_ && (r!=observed_root_ || component!=observed_component_)) {
        invalidate_application();
        saved_.clear(); pending_.reset(); add_pending_.reset();
        if (r!=observed_root_) for (auto &[address,n]:nodes_) {
            revoked(address,"root-change"); n.lens.reset(); ++n.origin;
        }
        if (r!=observed_root_) {
            delta_seed_[observed_root_.level]=false; level_epoch_[observed_root_.level]=++event_epoch_;
            auto &h=delta_history_[observed_root_.level]; h.reason="root-change"; h.order=level_epoch_[observed_root_.level];
            delta_.reset(); delta_pending_.reset();
        }
    }
    observed_=true; observed_root_=r; observed_component_=component; return r;
}
bool NativeWorldFov::owns(const Root &r,uint32_t address) const {
    return r && address && read(r.camera,0x26c)==address && read(address)==vtable_;
}
NativeWorldFov::Node &NativeWorldFov::node(uint32_t address) {
    auto &n=nodes_[address]; if (!n.generation) n.generation=++generation_; return n;
}
LensSite NativeWorldFov::site(uint32_t caller) const {
    auto it=sites_.find(caller); return it==sites_.end() ? LensSite::consumer : it->second;
}
std::array<uint32_t,6> NativeWorldFov::parameters(uint32_t p) const {
    return {read(p,0x128),read(p,0x12c),read(p,0x130),read(p,0x134),read<uint8_t>(p,0x138),read<uint8_t>(p,0x109)};
}
bool NativeWorldFov::matches(uint32_t p,const Stamp &s) const {
    return parameters(p)==s.parameters && !std::memcmp(reinterpret_cast<void *>(p+0x200),s.matrix.data(),64);
}
bool NativeWorldFov::valid_reference(WeaponReference reference,uint32_t p,const Root *acquired) {
    const auto serial=publication_serial_.load(std::memory_order_relaxed);
    const auto current=acquired ? *acquired : root();
    if (publication_serial_.load(std::memory_order_relaxed)!=serial || events_.load(std::memory_order_acquire)) return false;
    const auto found=nodes_.find(p);
    const auto manager=read(scene_manager_);
    return manager && read(manager,0xdc)==p && reference.node==p && reference.root==current && owns(current,p) &&
        found!=nodes_.end() && found->second.generation==reference.generation &&
        found->second.origin==reference.origin &&
        bits(reference.lens.native_vertical)==read(p,0x128);
}
std::optional<NativeWorldFov::WeaponReference> NativeWorldFov::incoming_reference(uint32_t p,float raw) {
    // An unknown regular outer context is a barrier even when its raw bits
    // happen to equal a formerly known world lens.
    if (auto *outer=regular_frame()) {
        const auto reference=outer->current;
        const auto current=root();
        if (reference && source_matches(*outer,&current) && valid_reference(*reference,p,&current) &&
            bits(reference->lens.native_vertical)==bits(raw)) return reference;
        outer->current.reset();
        return {};
    }
    const auto current=root();
    if (events_.load(std::memory_order_acquire)) return {};
    consume_foreign(p,current);
    const auto found=nodes_.find(p);
    if (!owns(current,p) || found==nodes_.end() || !found->second.lens ||
        found->second.root!=current || bits(found->second.lens->native_vertical)!=bits(raw)) return {};
    const auto &n=found->second;
    return WeaponReference{current,p,n.generation,n.origin,*n.lens};
}
std::optional<NativeWorldFov::WeaponSource> NativeWorldFov::weapon_source(uint32_t manager,const Root *acquired) {
    const auto serial=publication_serial_.load(std::memory_order_relaxed);
    const auto current=acquired ? *acquired : root();
    if (publication_serial_.load(std::memory_order_relaxed)!=serial || events_.load(std::memory_order_acquire)) { weapon_observed("source-publication"); return {}; }
    if (!current) { weapon_observed("source-root"); return {}; }
    if (!manager || read(current.player,0x170)!=manager) { weapon_observed("source-manager"); return {}; }
    WeaponSource source; source.root=current; source.manager=manager;
    // Observe only standard nearest-rounding normal arithmetic, never change it.
    source.mode=(_mm_getcsr()&0xe040u) | ((_controlfp(0,0)&_MCW_RC)<<16);
    if (source.mode) { weapon_observed("source-floating-mode"); return {}; }
    source.base=read(base_);
    if (source.base!=bits(70.f)) { weapon_observed("source-default"); return {}; }
    source.owner=read(tweak_);
    source.flag=source.owner ? read<uint8_t>(source.owner,0x58) : 0;
    source.tweak=source.flag!=0;
    if (source.tweak) {
        source.value=read(source.owner,0x5c);
        source.width=read<int32_t>(screen_w_); source.height=read<int32_t>(screen_h_);
        if (source.width<=0 || source.height<=0 || !std::isfinite(std::bit_cast<float>(source.value))) { weapon_observed("source-tweak-dimensions-value"); return {}; }
        source.ratio=double(source.height)/source.width;
        return source;
    }
    source.inventory=read(manager,0x148); source.end=read(manager,0x14c); source.index=read(manager,0x160);
    if (!source.inventory || source.end<source.inventory || (source.end-source.inventory)%24 ||
        source.index>=(source.end-source.inventory)/24 ||
        uint64_t(source.inventory)+uint64_t(source.index)*24+16>source.end) { weapon_observed("source-inventory-range"); return {}; }
    source.component=read(source.inventory,source.index*24+12);
    if (!source.component) { weapon_observed("source-component"); return {}; }
    source.config=read(source.component,0x14);
    if (!source.config) { weapon_observed("source-config"); return {}; }
    source.endpoints={read(source.config,0x11c),read(source.config,0x120),
                      read(source.component,0x298),read(source.component,0x29c)};
    double ratios[2]{}; unsigned count=0;
    const float constant=std::bit_cast<float>(0x3c8efa35u);
    for (unsigned i=0;i<2;++i) {
        const float config=std::bit_cast<float>(source.endpoints[i]);
        const float delta=std::bit_cast<float>(source.endpoints[i+2]);
        volatile float difference=config-70.f;
        if (!std::isfinite(config) || !std::isfinite(delta)) { weapon_observed("source-endpoint-nonfinite"); return {}; }
        if (difference==0) { if (delta!=0) { weapon_observed("source-endpoint-zero"); return {}; } continue; }
        if (!std::isnormal(float(difference)) || !std::isnormal(delta) ||
            std::signbit(float(difference))!=std::signbit(delta)) { weapon_observed("source-endpoint-sign-normal"); return {}; }
        const double ratio=double(delta)/(double(difference)*double(constant));
        if (!std::isfinite(ratio) || ratio<=0) { weapon_observed("source-ratio"); return {}; }
        ratios[count++]=ratio;
    }
    if (count==2 && std::abs(ratios[0]-ratios[1])>1e-6*std::fmax(ratios[0],ratios[1])) { weapon_observed("source-endpoint-ratios-disagree"); return {}; }
    source.ratio_free_zero=count==0;
    source.ratio=count==2 ? (ratios[0]+ratios[1])*.5 : ratios[0];
    return source;
}
bool NativeWorldFov::source_matches(const WeaponFrame &frame,const Root *acquired) {
    if (!frame.source) { weapon_observed("source-missing"); return false; }
    if (!frame.sample || !std::isfinite(*frame.sample)) { weapon_observed("sample-missing-nonfinite"); return false; }
    const auto source=*frame.source; const float sample=*frame.sample;
    const auto fresh=weapon_source(source.manager,acquired);
    if (!fresh || *fresh!=source) { weapon_observed("source-changed"); return false; }
    if (fresh->ratio_free_zero) { if(sample!=0) weapon_observed("sample-zero-mismatch"); return sample==0; }
    if (fresh->tweak) {
        volatile float difference=std::bit_cast<float>(fresh->value)-70.f;
        volatile float product=difference*float(fresh->height);
        volatile float quotient=product/float(fresh->width);
        volatile float result=quotient*std::bit_cast<float>(0x3c8efa35u);
        if ((difference!=0 && (!std::isnormal(float(difference)) || !std::isnormal(float(product)) ||
             !std::isnormal(float(quotient)) || !std::isnormal(float(result)))) ||
            bits(result)!=bits(sample)) { weapon_observed("sample-tweak-output"); return false; }
    }
    return true;
}
float __cdecl NativeWorldFov::weapon_diff(uint32_t manager) {
    auto &s=*instance_; CallbackScope callback(s);
    if (!s.lane() || !s.weapon_frame_) return call_float(s.original_weapon_diff_,manager);
    s.drain(); auto *frame=s.weapon_frame_;
    frame->source=s.weapon_source(manager); frame->sample.reset();
    const float sample=call_float(s.original_weapon_diff_,manager);
    frame->sample=sample;
    if (!s.source_matches(*frame)) frame->source.reset();
    return sample;
}
void NativeWorldFov::weapon_write(uint32_t p,float value,bool restore) {
    if (!track_weapon_) return; // Legacy world-only retains the exact R1 cleanup scope.
    auto &frame=*weapon_frame_;
    if (restore) {
        frame.current.reset(); frame.restored=true;
        // Retire the inner reference before cleanup and before the wrapper pop.
        if (frame.before && !valid_reference(*frame.before,p)) frame.before.reset();
        return;
    }
    frame.current.reset();
    if (!frame.regular || frame.restored || !frame.before || !frame.sample) {
        weapon_observed(!frame.regular ? "pass-not-regular" : frame.restored ? "pass-restored" : !frame.before ? "world-reference-before-missing" : "sample-missing"); return;
    }
    const auto before=*frame.before;
    const auto serial=publication_serial_.load(std::memory_order_relaxed);
    const auto current=root();
    if (publication_serial_.load(std::memory_order_relaxed)!=serial || events_.load(std::memory_order_acquire)) { drain(); return; }
    auto found=nodes_.find(p);
    const auto manager=read(scene_manager_);
    if (!source_matches(frame,&current) || before.node!=p || before.root!=current || !owns(current,p) || !manager || read(manager,0xdc)!=p ||
        found==nodes_.end() || found->second.generation!=before.generation || found->second.origin!=before.origin ||
        bits(float(before.lens.native_vertical+*frame.sample))!=bits(value) || read(p,0x128)!=bits(value)) return;
    const double delta=frame.source->ratio_free_zero ? 0 :
        double(*frame.sample)/(frame.source->ratio*reference_aspect);
    ReferenceLens lens{value,before.lens.reference_vertical+delta};
    if (!vertical_lens(lens.native_vertical) || !std::isfinite(lens.reference_vertical) ||
        lens.reference_vertical<=0 || lens.reference_vertical>=3.141592653589793) return;
    frame.current=WeaponReference{current,p,before.generation,before.origin,lens};
    if (weapon_setting_) ensure(p);
}
std::optional<float> NativeWorldFov::effective(uint32_t p,Node n,const Root *acquired) {
    eligibility_rejected_=false;
    Root eligibility_root{};
    if(track_weapon_ && weapon_setting_ && weapon_frame_ && !weapon_frame_->restored) {
        // Custom weapon scope is independent of regular lens provenance.
        // Explicit weapon Native retains the original no-lens inheritance;
        // rejected Custom consumption must never fall through to world Custom.
        eligibility_root=acquired ? *acquired : root(); acquired=&eligibility_root;
        if(!regular_frame() || !eligible_weapon(p,eligibility_root)) {
            eligibility_rejected_=true; weapon_observed(eligibility_reason_); return {};
        }
    }
    if (auto *frame=regular_frame()) {
        const float aspect=read<float>(p,0x12c),near_plane=read<float>(p,0x130),far_plane=read<float>(p,0x134);
        if (read<uint8_t>(p,0x109) || !std::isnormal(aspect) || aspect<=0 ||
            !std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane<=0 || far_plane<=near_plane) {
            weapon_observed("projection-parameters"); return {};
        }
        if (!weapon_setting_ || !frame->current) {
            weapon_gate_=Gate::unproved_weapon_source;
            weapon_observed(!weapon_setting_ ? "native-option" : !frame->before ? "world-reference-before-missing" : "current-reference-missing");
            return {};
        }
        const auto current=acquired ? *acquired : root();
        if (!source_matches(*frame,&current)) {
            weapon_gate_=Gate::unproved_weapon_source; frame->current.reset(); return {};
        }
        if (!valid_reference(*frame->current,p,&current)) {
            weapon_gate_=Gate::unproved_weapon_source; weapon_observed("reference-lifetime-owner-raw"); frame->current.reset(); return {};
        }
        auto angle=weapon_vertical_fov(frame->current->lens,weapon_setting_);
        if (angle) {
            const float y=1.0f/std::tan(*angle*.5f),x=y/aspect;
            if (!std::isnormal(x) || x<=0 || !std::isnormal(y) || y<=0) return {};
        }
        return angle;
    }
    if (!n.lens) { world_rejected("lens-missing",p,n); return {}; }
    const auto raw=read(p,0x128);
    if(bits(n.lens->native_vertical)!=raw) { world_rejected("raw-mismatch",p,n,raw); return {}; }
    if(read<int32_t>(screen_w_)<=0) { world_rejected("screen-width",p,n,raw); return {}; }
    if(read<int32_t>(screen_h_)<=0) { world_rejected("screen-height",p,n,raw); return {}; }
    if(!std::isfinite(read<float>(p,0x12c))) { world_rejected("aspect-nonfinite",p,n,raw); return {}; }
    if(read<float>(p,0x12c)<=0) { world_rejected("aspect-nonpositive",p,n,raw); return {}; }
    if(read<uint8_t>(p,0x109)) { world_rejected("orthographic",p,n,raw); return {}; }
    const auto current=acquired ? *acquired : root();
    const auto fresh=nodes_.find(p);
    if(events_.load(std::memory_order_acquire)) { world_rejected("pending-event",p,n,raw); return {}; }
    if(fresh==nodes_.end()) { world_rejected("node-missing",p,n,raw); return {}; }
    if(fresh->second.generation!=n.generation) { world_rejected("generation-changed",p,n,raw); return {}; }
    if(fresh->second.origin!=n.origin) { world_rejected("origin-changed",p,n,raw); return {}; }
    if(!fresh->second.lens || !n.lens) { world_rejected("fresh-lens-missing",p,n,raw); return {}; }
    if(current!=n.root) { world_rejected("root-changed",p,n,raw); return {}; }
    if(!owns(current,p)) { world_rejected("owner-changed",p,n,raw); return {}; }
    return world_vertical_fov(*n.lens,read<float>(base_),setting_);
}
viewmodel_eligibility::NativeSample NativeWorldFov::sample_weapon_eligibility(uint32_t p,const Root &current) {
    eligibility_reason_="native-fps-owner-scope";
    if(force_native_weapon_ || !eligibility_ || !current || events_.load(std::memory_order_acquire)) return {};
    if(camera_scope_ && camera_scope_->node==p && !camera_alive(*camera_scope_)) return {};
    if(!owns(current,p)) return {};
    const auto serial=publication_serial_.load(std::memory_order_acquire);
    const auto found=nodes_.find(p);
    const auto manager=read(scene_manager_);
    if(events_.load(std::memory_order_acquire) || found==nodes_.end() || !manager || read(manager,0xdc)!=p) return {};
    const auto generation=found->second.generation;
    const auto source=weapon_source(read(current.player,0x170),&current);
    // A tweak scalar alone does not prove a current local weapon owner.
    eligibility_reason_=source && source->tweak ? "native-fps-tweak-source" : "native-fps-weapon-source";
    if(!source || source->tweak || !source->component) return {};
    viewmodel_eligibility::NativeOwners owners{current.level,current.player,current.actor,current.camera,p,
        source->manager,source->component,source->inventory,source->end,source->index,generation};
    const auto sample=eligibility_->sample(owners);
    eligibility_reason_=eligibility_->last_rejection();
    // No guest helper follows the pure native liveness query. These are current
    // source/lifecycle fences, not reused positive results from an earlier pass.
    const auto after=nodes_.find(p);
    const bool valid=!events_.load(std::memory_order_acquire) && publication_serial_.load(std::memory_order_acquire)==serial &&
        after!=nodes_.end() && after->second.generation==generation && owns(current,p) &&
        read(scene_manager_)==manager && read(manager,0xdc)==p &&
        weapon_source(source->manager,&current)==source && sample.decision.permits(sample.boundary);
    return valid ? sample : viewmodel_eligibility::NativeSample{};
}
bool NativeWorldFov::eligible_weapon(uint32_t p,const Root &current) {
    const auto sample=sample_weapon_eligibility(p,current);
    return sample.decision.permits(sample.boundary);
}
NativeWorldFov::ViewmodelOwnerCapsule NativeWorldFov::acquire_viewmodel_owners(uint32_t p) {
    ViewmodelOwnerCapsule capsule; auto *s=instance_;
    if(!s || !s->lane() || s->camera_scope_ || !s->regular_frame()) return capsule;
    CallbackScope callback(*s); s->drain();
    const auto before=s->nodes_.find(p);
    if(before==s->nodes_.end()) return capsule;
    const auto generation=before->second.generation;
    const auto current=s->root(); // Callback-capable PRE-acquisition only.
    const auto after=s->nodes_.find(p);
    if(s->events_.load(std::memory_order_acquire) || after==s->nodes_.end() ||
       after->second.generation!=generation || !s->owns(current,p)) return capsule;
    capsule.level_=current.level; capsule.player_=current.player;
    capsule.actor_=current.actor; capsule.camera_=current.camera; capsule.node_=p;
    capsule.generation_=generation; capsule.publication_=s->publication_serial_.load(std::memory_order_acquire);
    capsule.weapon_pass_=s->weapon_frame_; capsule.pass_serial_=s->weapon_frame_->eligibility_pass; return capsule;
}
viewmodel_eligibility::NativeSample NativeWorldFov::sample_viewmodel_eligibility(const ViewmodelOwnerCapsule &capsule) {
    auto *s=instance_;
    if(!s || !s->lane() || !capsule.node_ || !s->regular_frame() ||
       s->weapon_frame_!=capsule.weapon_pass_ || !capsule.pass_serial_ ||
       s->weapon_frame_->eligibility_pass!=capsule.pass_serial_ || s->events_.load(std::memory_order_acquire) ||
       s->publication_serial_.load(std::memory_order_acquire)!=capsule.publication_) return {};
    const auto found=s->nodes_.find(capsule.node_);
    if(found==s->nodes_.end() || found->second.generation!=capsule.generation_) return {};
    return s->sample_weapon_eligibility(capsule.node_,
        Root{capsule.level_,capsule.player_,capsule.actor_,capsule.camera_});
}
NativeWorldFov::ViewmodelWeaponSample NativeWorldFov::sample_viewmodel_weapon(const ViewmodelOwnerCapsule &capsule) {
    const auto sample=sample_viewmodel_eligibility(capsule);
    if(!sample.decision.permits(sample.boundary)) return {};
    auto *s=instance_;
    const Root current{capsule.level_,capsule.player_,capsule.actor_,capsule.camera_};
    // sample_viewmodel_eligibility fenced the capsule before these field reads.
    // Acquired-root overload performs no callback-capable ownership query.
    const auto source=s->weapon_source(read(current.player,0x170),&current);
    const auto manager=read(s->scene_manager_);
    if(!source||source->tweak||!source->component||!manager||
       read(manager,0xdc)!=capsule.node_||s->events_.load(std::memory_order_acquire)||
       s->publication_serial_.load(std::memory_order_acquire)!=capsule.publication_) return {};
    return {sample,source->component,manager};
}
bool NativeWorldFov::camera_alive(const CameraScope &scope) const {
    // Check lifecycle metadata BEFORE dereferencing supplied native storage.
    if(events_.load(std::memory_order_acquire)) return false;
    const auto found=nodes_.find(scope.node);
    return found!=nodes_.end() && scope.generation && found->second.generation==scope.generation &&
        read(scope.node)==vtable_;
}
bool NativeWorldFov::retire_weapon_stamp(uint32_t p,uint64_t generation) {
    auto found=nodes_.find(p);
    if(events_.load(std::memory_order_acquire) || found==nodes_.end() || found->second.generation!=generation ||
       !found->second.stamp || !found->second.stamp->effective || !matches(p,*found->second.stamp) || read(p)!=vtable_) return false;
    const auto manager=read(scene_manager_);
    const bool scoped=camera_scope_ && camera_scope_->node==p && camera_scope_->generation==generation && camera_alive(*camera_scope_);
    if(!scoped && (!manager || read(manager,0xdc)!=p)) return false;
    const auto raw=read(p,0x128); const auto raw_epoch=found->second.raw_epoch;
    auto owned=[&] {
        if(events_.load(std::memory_order_acquire)) return false;
        const auto fresh=nodes_.find(p);
        return fresh!=nodes_.end() && fresh->second.generation==generation && fresh->second.raw_epoch==raw_epoch &&
            read(p)==vtable_ && read(p,0x128)==raw &&
            (scoped ? camera_alive(*camera_scope_) : read(scene_manager_)==manager && read(manager,0xdc)==p);
    };
    const auto saved=force_native_weapon_; force_native_weapon_=true; ++stage_depth_;
    try {
        invoke(original_projection_,{p});
        // A callback can destroy or reconstruct p. Never chain a helper on the
        // pre-call receiver without this exact lifetime/raw/owner fence.
        if(owned()) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p);
    } catch(...) { --stage_depth_; force_native_weapon_=saved; throw; }
    --stage_depth_; force_native_weapon_=saved;
    if(!owned()) return true; // The original ran; never retry an obsolete receiver.
    found=nodes_.find(p);
    Stamp stamp; stamp.option_epoch=option_epoch_; stamp.parameters=parameters(p); stamp.angle=read<float>(p,0x128);
    std::memcpy(stamp.matrix.data(),reinterpret_cast<void *>(p+0x200),64); found->second.stamp=stamp;
    if(weapon_frame_) weapon_frame_->changed=true;
    return true;
}
void __cdecl NativeWorldFov::camera_render(uint32_t p,uint32_t context) {
    auto &s=*instance_; CallbackScope callback(s);
    if(!s.lane() || !s.weapon_frame_) { invoke(s.original_camera_render_,{p,context}); return; }
    s.drain(); auto found=s.nodes_.find(p);
    if(s.rigid_requested() && found!=s.nodes_.end()) {
        auto *frame=s.weapon_frame_;
        const CameraScope borrowed{p,found->second.generation,{},nullptr};
        ++s.rigid_status_.acquisitions;
        // Existing callback-capable two-stage API, before original camera body.
        // No borrowed mesh/tree is read across acquisition callbacks.
        const auto owners=acquire_viewmodel_owners(p);
        if(!s.camera_alive(borrowed)) return;
        if(s.weapon_frame_==frame) frame->rigid_owners=owners;
        found=s.nodes_.find(p); // Acquisition may drain/invalidate prior map iterators.
    }
    const bool retire_output=found!=s.nodes_.end() && found->second.stamp && found->second.stamp->effective &&
        (!s.setting_ || found->second.stamp->context==Stamp::Context::weapon);
    // Native no-lens passes inherit world output without extra queries/return
    // publication. Still fence retained weapon Custom or deferred Reset output.
    if(!s.weapon_setting_ && !retire_output) { invoke(s.original_camera_render_,{p,context}); return; }
    // Unknown camera ownership never licenses a new callback-capable query.
    if(found==s.nodes_.end()) { invoke(s.original_camera_render_,{p,context}); return; }
    s.weapon_frame_->changed=true;
    CameraScope scope{p,found->second.generation,{},s.camera_scope_};
    // Do not adopt a new generation after admission callbacks at the same address.
    scope.owners=s.root();
    if(!s.camera_alive(scope)) return;
    s.camera_scope_=&scope;
    try {
        s.ensure(p);
        if(s.camera_alive(scope)) invoke(s.original_camera_render_,{p,context});
    }
    catch(...) { s.camera_scope_=scope.parent; throw; }
    s.camera_scope_=scope.parent;
}
__declspec(noinline) void __cdecl NativeWorldFov::driver_projection(uint32_t driver,uint32_t type,uint32_t matrix,uint32_t flags) {
    // This intrinsic refers to THIS non-inlined cdecl wrapper's return slot.
    // Never touch another frame. Its exact original CALL return and the native
    // epilogue bytes are authenticated at installation, before code patching.
    auto *return_slot=reinterpret_cast<uint32_t *>(_AddressOfReturnAddress());
    auto &s=*instance_; CallbackScope callback(s);
    if(s.lane() && s.weapon_frame_ && s.camera_scope_ && type==1 &&
       matrix==s.camera_scope_->node+0x200 && *return_slot==s.projection_return_) {
        const auto &scope=*s.camera_scope_; const auto p=scope.node;
        auto cancel=[&] { *return_slot=s.camera_epilogue_; };
        if(!s.camera_alive(scope)) { cancel(); return; }
        const auto found=s.nodes_.find(p);
        // root() is intentionally absent: original builders have returned;
        // only pure original SP liveness plus field/token fences follow.
        auto desired=s.effective(p,found->second,&scope.owners);
        if(!s.camera_alive(scope)) { cancel(); return; }
        auto fresh=s.nodes_.find(p);
        if(desired && fresh->second.stamp && s.matches(p,*fresh->second.stamp) &&
           (!fresh->second.stamp->effective || fresh->second.stamp->context!=Stamp::Context::weapon ||
            bits(fresh->second.stamp->angle)!=bits(*desired))) {
            // A legitimate return after entry admission still restores on this
            // sink. Only exact known output is rebuilt; authored matrices stay
            // outside our ownership. Builders may invalidate the capsule.
            s.native_projection(p);
            if(!s.camera_alive(scope)) { cancel(); return; }
            fresh=s.nodes_.find(p);
            desired=s.effective(p,fresh->second,&scope.owners);
        }
        if(!s.camera_alive(scope)) { cancel(); return; }
        fresh=s.nodes_.find(p);
        if(fresh->second.stamp) {
            const auto stamp=*fresh->second.stamp;
            if(!desired || !stamp.effective || stamp.context!=Stamp::Context::weapon ||
               bits(stamp.angle)!=bits(*desired)) s.retire_weapon_stamp(p,scope.generation);
        }
        if(!s.camera_alive(scope)) { cancel(); return; }
        invoke(s.original_driver_projection_,{driver,type,matrix,flags});
        // Do not resume the native VIEW copy if a driver callback retired p.
        if(!s.camera_alive(scope)) cancel();
        return;
    }
    invoke(s.original_driver_projection_,{driver,type,matrix,flags});
}
void NativeWorldFov::capture_jeep(uint32_t p,float raw,LensSite kind) {
    const auto current=root();
    const uint32_t component=kind==LensSite::jeep_update ? (jeep_scope_ ? jeep_scope_->component_ : 0) : scoped_component_;
    if (!current || !p || read(current.camera,0x26c)!=p || !component ||
        invoke(get_jeep_,{current.level})!=component || read(component,0x11c)!=p) return;
    auto saved=saved_.find(component);
    if (saved!=saved_.end() && (saved->second.root!=current || bits(saved->second.raw)!=read(component,0x178))) {
        saved_.erase(saved); saved=saved_.end();
    }
    if (!owns(current,p)) { if(kind==LensSite::jeep_save) saved_.erase(component); return; }
    auto &n=node(p);
    if (!n.lens || n.root!=current || bits(n.lens->native_vertical)!=bits(raw)) {
        if (kind==LensSite::jeep_save) saved_.erase(component);
        return;
    }
    if (kind==LensSite::jeep_save) {
        saved_.insert_or_assign(component,Saved{current,raw,n.lens->reference_vertical}); return;
    }
    Pending next; next.root=current; next.node=p; next.component=component;
    next.generation=n.generation; next.origin=n.origin; next.before=*n.lens;
    next.config=read(component,0x108); next.branch=read<uint8_t>(component,0x1ac);
    if (!next.config) return;
    next.coefficient=read<float>(next.config,0x74);
    next.lower=read<float>(next.config,0x6c); next.upper=read<float>(next.config,0x70);
    if (!std::isfinite(next.lower) || !std::isfinite(next.upper) || !std::isfinite(next.coefficient)) {
        revoked(p,"jeep-parameters"); n.lens.reset(); ++n.origin; return;
    }
    next.speed=next.branch && next.lower>0 && next.upper>0;
    const uint32_t dt=kind==LensSite::jeep_update ? jeep_scope_->dt_ : scoped_dt_;
    volatile float rate=.001f*next.coefficient;
    volatile float alpha=rate*float(static_cast<int32_t>(dt));
    next.alpha=kind==LensSite::jeep_enable || (kind==LensSite::jeep_helper && scoped_force_) ? 1.f : alpha;
    if (saved!=saved_.end()) {
        if (saved->second.root==current && bits(saved->second.raw)==read(component,0x178)) next.saved=saved->second.reference;
        else saved_.erase(saved);
    }
    pending_=next;
}
void NativeWorldFov::capture_delta(uint32_t p,float raw,bool accumulate) {
    finish_delta();
    const auto current=root(); auto found=nodes_.find(p);
    if (!owns(current,p) || found==nodes_.end() || !found->second.lens ||
        found->second.root!=current || bits(found->second.lens->native_vertical)!=bits(raw)) {
        delta_.reset(); delta_pending_.reset();
        if (current) {
            delta_seed_[current.level]=false; level_epoch_[current.level]=++event_epoch_;
            auto &h=delta_history_[current.level]; h.reason="delta-source-unproved"; h.order=level_epoch_[current.level];
        }
        return;
    }
    const int32_t width=read<int32_t>(screen_w_),height=read<int32_t>(screen_h_);
    const float old_delta=read<float>(current.level,0x2dc);
    std::optional<double> old_reference;
    if (delta_ && delta_->root==current && bits(delta_->native)==bits(old_delta)) old_reference=delta_->reference;
    else if (delta_seed_[current.level] && old_delta==0) old_reference=0;
    delta_seed_[current.level]=false;
    const auto epoch=++event_epoch_; level_epoch_[current.level]=epoch;
    auto &history=delta_history_[current.level]; history.reason="delta-pending"; history.order=epoch;
    if (width<=0 || height<=0 || !std::isfinite(old_delta) || (accumulate && !old_reference)) {
        delta_.reset(); history.reason="delta-input-unproved"; return;
    }
    delta_pending_=DeltaPending{current,raw,old_delta,found->second.lens->reference_vertical,
        old_reference.value_or(0),double(width)/height/reference_aspect,accumulate,epoch};
}
void NativeWorldFov::finish_delta() {
    if (!delta_pending_) return;
    const auto p=*delta_pending_; delta_pending_.reset();
    const auto current=root();
    if (current!=p.root || level_epoch_[current.level]!=p.epoch) {
        delta_.reset(); auto &h=delta_history_[p.root.level]; h.reason=current!=p.root ? "delta-completion-root-change" : "delta-completion-order"; h.order=level_epoch_[p.root.level]; return;
    }
    const float native=read<float>(current.level,0x2dc);
    // Invert the rounded native result; the reference error is bounded by
    // native float cancellation, not a new evaluation of its interpolator.
    const double target=double(native)+p.raw-(p.accumulate ? p.old_delta : 0);
    const double reference=target*p.scale-p.reference+(p.accumulate ? p.old_reference_delta : 0);
    if (!std::isfinite(native) || !vertical_lens(float(target)) || !std::isfinite(reference)) {
        delta_.reset(); auto &h=delta_history_[current.level]; h.reason="delta-output-unproved"; h.order=p.epoch; return;
    }
    delta_=Delta{current,native,reference};
    auto &h=delta_history_[current.level]; h.reason="delta-established"; h.order=p.epoch;
}
void NativeWorldFov::capture_additive(uint32_t p,float raw) {
    add_pending_.reset();
    additive_reject_="capture-root";
    additive_capture_node_=p; additive_delta_history_={}; additive_upstream_={};
    finish_delta();
    const auto current=root(); auto found=nodes_.find(p);
    const auto history=lens_history_.find(p);
    if(history!=lens_history_.end()) additive_upstream_=history->second.last;
    if (!current) return;
    if (!p) { additive_reject_="capture-null-node"; return; }
    if (read(current.camera,0x26c)!=p) { additive_reject_="capture-active-node"; return; }
    additive_delta_history_=delta_history_[current.level];
    const float native_delta=read<float>(current.level,0x2dc);
    std::optional<double> reference_delta;
    const char *delta_failure="delta-unseeded";
    if (delta_ && delta_->root==current && bits(delta_->native)==bits(native_delta)) reference_delta=delta_->reference;
    else if (delta_seed_[current.level] && native_delta==0) reference_delta=0;
    else if (delta_) delta_failure=delta_->root!=current ? "delta-root-mismatch" : "delta-value-mismatch";
    else if (delta_seed_[current.level]) delta_failure="delta-nonzero-unproved";
    if (!reference_delta) {
        additive_reject_=delta_failure;
        delta_.reset(); delta_pending_.reset(); delta_seed_[current.level]=false;
        level_epoch_[current.level]=++event_epoch_;
        if (std::strcmp(delta_failure,"delta-unseeded")!=0) { auto &h=delta_history_[current.level]; h.reason="additive-delta-unproved"; h.order=level_epoch_[current.level]; } return;
    }
    if (!owns(current,p)) { additive_reject_="capture-owner"; return; }
    if (found==nodes_.end()) { additive_reject_="capture-node"; return; }
    if (!found->second.lens) { additive_reject_="capture-lens"; return; }
    if (found->second.root!=current) { additive_reject_="capture-origin-root"; return; }
    if (bits(found->second.lens->native_vertical)!=bits(raw)) { additive_reject_="capture-raw"; return; }
    const auto &n=found->second;
    add_pending_=AddPending{current,p,n.generation,n.origin,raw,native_delta,n.lens->reference_vertical,*reference_delta};
    additive_reject_="pending-invalidated";
}
void NativeWorldFov::finish_additive(uint32_t p,float output) {
    auto found=nodes_.find(p); if (found==nodes_.end()) return;
    auto &n=found->second;
    const auto a=add_pending_; add_pending_.reset();
    const auto current=root();
    const char *failure=nullptr;
    if (!a) failure=additive_capture_node_==p ? additive_reject_ : "capture-not-current";
    else if (a->node!=p) failure="finish-node";
    else if (a->root!=current) failure="finish-root";
    else if (!owns(current,p)) failure="finish-owner";
    else if (n.generation!=a->generation) failure="finish-generation";
    else if (n.origin!=a->origin) failure="finish-origin";
    else if (read(current.level,0x2dc)!=bits(a->delta)) failure="finish-delta-bits";
    else if (bits(float(a->raw+a->delta))!=bits(output)) failure="finish-output-sum";
    else if (read(p,0x128)!=bits(output)) failure="finish-output-raw";
    if (failure) {
        auto &history=lens_history_[p];
        // Retain the first failure after a known establishment. Later frames
        // with the already-revoked lens must not hide the causal predicate.
        if (std::strcmp(history.additive,"not-observed")==0) {
            history.additive=failure;
            history.delta=additive_capture_node_==p ? additive_delta_history_ : DeltaHistory{};
            history.upstream=additive_capture_node_==p ? additive_upstream_ : Revocation{};
        }
        revoked(p,"additive-proof"); n.lens.reset(); ++n.origin; return;
    }
    ReferenceLens result{output,a->reference+a->reference_delta};
    n.lens=vertical_lens(result.native_vertical) && std::isfinite(result.reference_vertical) &&
        result.reference_vertical>0 && result.reference_vertical<3.141592653589793 ? std::optional(result) : std::nullopt;
    if(n.lens) established(p,"known-additive"); else revoked(p,"additive-parameters");
    ++n.origin;
}
void NativeWorldFov::finish_pending(std::optional<float> native_output) {
    if (!pending_) return;
    auto p=*pending_; pending_.reset();
    auto it=nodes_.find(p.node);
    if (it==nodes_.end()) return;
    auto &n=it->second;
    const auto current=root();
    const bool valid=current==p.root && owns(current,p.node) && invoke(get_jeep_,{current.level})==p.component &&
        read(p.component,0x11c)==p.node && n.generation==p.generation && n.origin==p.origin &&
        read(p.component,0x108)==p.config && read<uint8_t>(p.component,0x1ac)==p.branch &&
        bits(read<float>(p.config,0x74))==bits(p.coefficient) &&
        bits(read<float>(p.config,0x6c))==bits(p.lower) && bits(read<float>(p.config,0x70))==bits(p.upper) &&
        read(p.node,0x128)==bits(native_output.value_or(p.before.native_vertical));
    if (!valid) { revoked(p.node,"jeep-proof"); n.lens.reset(); ++n.origin; return; }
    const float output=read<float>(p.node,0x128);
    if (p.speed) n.lens=affine_reference_lens(p.before,p.before.native_vertical,output,p.alpha);
    else if (p.saved && vertical_lens(output)) n.lens=ReferenceLens{output,*p.saved};
    else n.lens.reset();
    if(n.lens) established(p.node,"known-jeep"); else revoked(p.node,"jeep-origin");
    ++n.origin;
}
void NativeWorldFov::native_projection(uint32_t p) {
    if(camera_scope_ && camera_scope_->node==p && !camera_alive(*camera_scope_)) return;
    auto forward_native=[&] {
        if(!camera_scope_ || camera_scope_->node!=p || camera_alive(*camera_scope_)) invoke(original_projection_,{p});
    };
    const auto serial=publication_serial_.load(std::memory_order_relaxed);
    auto found=nodes_.find(p);
    if (found==nodes_.end() && track_weapon_ && native_weapon_mode()) {
        const auto current=root();
        if (publication_serial_.load(std::memory_order_relaxed)!=serial || events_.load(std::memory_order_acquire)) { drain(); return; }
        // This actual known native builder may establish standard output in a
        // fresh lifetime. It creates no numerical lens/reference provenance.
        if (owns(current,p)) { node(p).root=current; found=nodes_.find(p); }
    }
    if (found==nodes_.end()) { forward_native(); return; }
    const auto generation=found->second.generation;
    auto forward_unowned=[&] {
        // Losing the actor/root does not revoke exact current-camera cleanup
        // authority. Preserve the Native output stamp so a later same-value
        // known lens writer can restore Custom after fresh eligibility.
        if(weapon_setting_ && weapon_frame_ && !weapon_frame_->restored && retire_weapon_stamp(p,generation)) return;
        forward_native();
    };
    Root current{};
    std::optional<float> angle;
    if (track_weapon_) {
        current=root();
        if (publication_serial_.load(std::memory_order_relaxed)!=serial || events_.load(std::memory_order_acquire)) { drain(); return; }
        if (!owns(current,p)) { forward_unowned(); return; }
        found=nodes_.find(p);
        if (found==nodes_.end() || found->second.generation!=generation) return;
        std::array<uint32_t,16> before_query{};
        std::memcpy(before_query.data(),reinterpret_cast<void *>(p+0x200),64);
        // Complete every guest ownership query BEFORE any temporary effective
        // raw write. The final fence below reads fields/tokens only.
        const auto fresh=root();
        if (publication_serial_.load(std::memory_order_relaxed)!=serial || events_.load(std::memory_order_acquire)) { drain(); return; }
        if (fresh!=current || !owns(fresh,p)) { forward_unowned(); return; }
        found=nodes_.find(p);
        if (found==nodes_.end() || found->second.generation!=generation) return;
        if (!(read<uint16_t>(p,0x300)&2) &&
            std::memcmp(before_query.data(),reinterpret_cast<void *>(p+0x200),64)) return;
        current=fresh;
        angle=effective(p,found->second,&current);
    } else {
        // Preserve R1's world-only regular/cleanup ordering and query scope.
        const auto snapshot=found->second;
        angle=effective(p,snapshot);
        current=native_weapon_mode() ? root() : Root{};
    }
    if (events_.load(std::memory_order_acquire)) { drain(); forward_native(); return; }
    found=nodes_.find(p);
    if (found==nodes_.end() || found->second.generation!=generation) { forward_native(); return; }
    const bool native_branch=owns(current,p);
    const bool own_output=found->second.stamp && found->second.stamp->effective &&
        !std::memcmp(reinterpret_cast<void *>(p+0x200),found->second.stamp->matrix.data(),64);
    if (!angle && !own_output && !found->second.refresh && !native_branch) { forward_native(); return; }
    const auto origin=found->second.origin,write_epoch=found->second.write_epoch,raw_epoch=found->second.raw_epoch;
    const float raw=read<float>(p,0x128);
    auto restore_raw=[&] {
        if (events_.load(std::memory_order_acquire)) drain();
        const auto fresh=nodes_.find(p);
        if (fresh==nodes_.end() || fresh->second.generation!=generation || fresh->second.raw_epoch!=raw_epoch ||
            read(p,0x128)!=bits(angle.value_or(raw))) return false;
        write(p,0x128,raw); return true;
    };
    if (angle) write(p,0x128,*angle);
    ++stage_depth_;
    try { forward_native(); }
    catch (...) { --stage_depth_; if (track_weapon_) (void)restore_raw(); else write(p,0x128,raw); throw; }
    --stage_depth_;
    if (track_weapon_) {
        if (!restore_raw()) { if (events_.load(std::memory_order_acquire)) drain(); return; }
    } else write(p,0x128,raw);
    found=nodes_.find(p);
    if (track_weapon_ && (found==nodes_.end() || found->second.generation!=generation ||
        found->second.raw_epoch!=raw_epoch || found->second.write_epoch!=write_epoch)) return;
    if (track_weapon_ && angle) {
        const auto final_angle=effective(p,found->second,&current);
        if (found->second.origin!=origin || !final_angle || bits(*final_angle)!=bits(*angle)) {
            if (!eligibility_rejected_) if (auto *frame=regular_frame()) frame->current.reset();
            angle.reset();
            // The exact supplied receiver/lifetime and standard output remain
            // owned. Retire that output through the original builder only;
            // no new Root query or effective mapping follows this native cleanup.
            const auto cleanup_origin=found->second.origin;
            ++stage_depth_;
            try { forward_native(); }
            catch (...) { --stage_depth_; throw; }
            --stage_depth_;
            if (events_.load(std::memory_order_acquire)) drain();
            found=nodes_.find(p);
            if (found==nodes_.end() || found->second.generation!=generation || found->second.origin!=cleanup_origin ||
                found->second.raw_epoch!=raw_epoch ||
                found->second.write_epoch!=write_epoch) return;
        }
    }
    if (native_branch && !track_weapon_) {
        const auto fresh=root();
        if (events_.load(std::memory_order_acquire)) { drain(); return; }
        if (fresh!=current || !owns(fresh,p)) return;
        found=nodes_.find(p);
        if (found==nodes_.end() || found->second.generation!=generation) return;
    }
    found=nodes_.find(p);
    if (found==nodes_.end() || found->second.generation!=generation) return;
    Stamp stamp; stamp.option_epoch=option_epoch_; stamp.parameters=parameters(p); stamp.angle=angle.value_or(read<float>(p,0x128)); stamp.effective=bool(angle);
    stamp.context=angle ? (regular_frame() ? Stamp::Context::weapon : Stamp::Context::world) : Stamp::Context::native;
    std::memcpy(stamp.matrix.data(),reinterpret_cast<void *>(p+0x200),64); found->second.stamp=stamp;
    if (prepared_) if (auto *frame=regular_frame()) {
        if ((angle && stamp.context==Stamp::Context::weapon) || !weapon_setting_)
            frame->applied_epoch=option_epoch_;
    }
    if (!matrix_depth_) rebuild(p);
}
void NativeWorldFov::rebuild(uint32_t p) {
    if(camera_scope_ && camera_scope_->node==p && !camera_alive(*camera_scope_)) return;
    ++matrix_depth_;
    try { movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); }
    catch (...) { --matrix_depth_; throw; }
    --matrix_depth_;
}
void NativeWorldFov::ensure(uint32_t p,bool forward_matrices) {
    if(camera_scope_ && camera_scope_->node==p && !camera_alive(*camera_scope_)) return;
    drain(); finish_pending();
    auto it=nodes_.find(p);
    if (it==nodes_.end()) { if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return; }
    const auto generation=it->second.generation;
    const auto snapshot=it->second;
    bool local_lens=false;
    if (snapshot.lens) {
        const auto current=root();
        it=nodes_.find(p);
        if (it==nodes_.end() || it->second.generation!=generation) {
            if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return;
        }
        local_lens=it->second.lens && it->second.root==current && owns(current,p);
    }
    if (events_.load(std::memory_order_acquire)) {
        drain(); if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return;
    }
    it=nodes_.find(p);
    if (it==nodes_.end() || it->second.generation!=generation) {
        if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return;
    }
    const bool own_output=it->second.stamp && it->second.stamp->effective &&
        !std::memcmp(reinterpret_cast<void *>(p+0x200),it->second.stamp->matrix.data(),64);
    if (!own_output && !it->second.refresh && !local_lens) {
        if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return;
    }
    if (it!=nodes_.end() && it->second.stamp) {
        const auto stamp=*it->second.stamp;
        if (!std::memcmp(reinterpret_cast<void *>(p+0x200),stamp.matrix.data(),64)) {
            const auto desired=effective(p,it->second);
            if (events_.load(std::memory_order_acquire)) {
                drain(); if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return;
            }
            it=nodes_.find(p);
            if (it==nodes_.end() || it->second.generation!=generation) {
                if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return;
            }
            if (!it->second.stamp || it->second.stamp->matrix!=stamp.matrix ||
                it->second.stamp->parameters!=stamp.parameters ||
                std::memcmp(reinterpret_cast<void *>(p+0x200),stamp.matrix.data(),64)) {
                if (forward_matrices) movement_wobble::NativeCameraBob::recalculate(original_matrices_,p); return;
            }
            const auto context=desired ? (regular_frame() ? Stamp::Context::weapon : Stamp::Context::world) : Stamp::Context::native;
            if ((prepared_ && stamp.option_epoch!=option_epoch_) || parameters(p)!=stamp.parameters || stamp.effective!=bool(desired) || stamp.context!=context ||
                bits(stamp.angle)!=bits(desired.value_or(read<float>(p,0x128))))
                write<uint16_t>(p,0x300,uint16_t(read<uint16_t>(p,0x300)|2));
        } else if (!(read<uint16_t>(p,0x300)&2)) {
            // Unknown clean output is authored/custom until a native writer rebuilds it.
            it->second.stamp.reset();
        }
    }
    if (forward_matrices || it->second.refresh || (read<uint16_t>(p,0x300)&2)) {
        const auto generation=it->second.generation;
        rebuild(p);
        // A nested callback may have drained destruction/reconstruction events.
        // Never write through the pre-call iterator into an obsolete lifetime.
        const auto fresh=nodes_.find(p);
        if (fresh!=nodes_.end() && fresh->second.generation==generation) fresh->second.refresh=false;
    }
}
float __cdecl NativeWorldFov::fov(uint32_t p) {
    auto &s=*instance_; CallbackScope callback(s);
    const auto caller=reinterpret_cast<uint32_t>(_ReturnAddress());
    const auto kind=s.site(caller);
    if (!s.lane() || s.stage_depth_) return call_float(s.original_fov_,p);
    s.drain();
    if (kind!=LensSite::consumer) {
        const float raw=call_float(s.original_fov_,p);
        if (kind==LensSite::weapon_begin && s.weapon_frame_) {
            if (s.track_weapon_) s.weapon_frame_->before=s.incoming_reference(p,raw);
            s.weapon_frame_->regular=true; s.weapon_frame_->changed=true;
            s.ensure(p);
        } else if (kind==LensSite::jeep_save || kind==LensSite::jeep_enable || kind==LensSite::jeep_update || kind==LensSite::jeep_helper)
            s.capture_jeep(p,raw,kind);
        else if (kind==LensSite::level_first || kind==LensSite::level_accumulate)
            s.capture_delta(p,raw,kind==LensSite::level_accumulate);
        else if (kind==LensSite::render_read) s.capture_additive(p,raw);
        return raw;
    }
    s.ensure(p);
    const auto found=s.nodes_.find(p);
    if (found!=s.nodes_.end() && found->second.stamp && found->second.stamp->effective && s.matches(p,*found->second.stamp))
        return found->second.stamp->angle;
    return call_float(s.original_fov_,p);
}
void __cdecl NativeWorldFov::set_fov(uint32_t p,float value) {
    auto &s=*instance_; CallbackScope callback(s);
    const auto caller=reinterpret_cast<uint32_t>(_ReturnAddress());
    const auto kind=s.site(caller);
    if (!s.lane()) {
        auto w=s.begin_foreign(p,caller); w.raw=bits(value);
        try {
            invoke(s.original_set_,{p,bits(value)});
            if(kind==LensSite::pseudo_write) {
                // Root's exact two-scalar exception. Stable repeated observations
                // do NOT claim an atomic pair or prove a pre-callback resize.
                const auto width=read<int32_t>(s.screen_w_),height=read<int32_t>(s.screen_h_);
                const auto width_again=read<int32_t>(s.screen_w_),height_again=read<int32_t>(s.screen_h_);
                w.width=width; w.height=height;
                w.coherent=width>0 && height>0 && width==width_again && height==height_again;
            }
        } catch(...) {
            (void)s.end_foreign(p,w); s.publish(p,EventKind::raw_external,0,caller); throw;
        }
        const bool exclusive=s.end_foreign(p,w); w.coherent=w.coherent && exclusive;
        s.publish(p,kind==LensSite::pseudo_write ? EventKind::pseudo_external : EventKind::raw_external,0,caller,&w);
        return;
    }
    s.invalidate_foreign(p);
    invoke(s.original_set_,{p,bits(value)});
    s.drain(); s.foreign_pseudo_.erase(p);
    if (const auto found=s.nodes_.find(p);found!=s.nodes_.end()) { ++found->second.write_epoch; ++found->second.raw_epoch; }
    if ((kind==LensSite::weapon_write || kind==LensSite::weapon_restore) && s.weapon_frame_) {
        s.weapon_write(p,value,kind==LensSite::weapon_restore); return;
    }
    if (kind==LensSite::jeep_write) { s.finish_pending(value); return; }
    if (kind==LensSite::additive_write) { s.finish_additive(p,value); return; }
    if (kind!=LensSite::pseudo_write && kind!=LensSite::vertical_write && s.nodes_.find(p)==s.nodes_.end()) return;
    const auto current=s.root();
    if (!s.owns(current,p) && s.nodes_.find(p)==s.nodes_.end()) return;
    auto &n=s.node(p); ++n.origin;
    if ((kind==LensSite::pseudo_write || kind==LensSite::vertical_write) && s.owns(current,p)) {
        n.root=current;
        n.lens=kind==LensSite::vertical_write ? vertical_lens(value) :
            pseudo_horizontal_lens(value,read<int32_t>(s.screen_w_),read<int32_t>(s.screen_h_));
        if(n.lens) s.established(p,kind==LensSite::vertical_write ? "known-vertical" : "known-pseudo",caller);
        else s.revoked(p,kind==LensSite::vertical_write ? "invalid-vertical" : "invalid-pseudo",caller);
    } else { s.revoked(p,kind==LensSite::raw ? "known-raw-writer" : "unknown-writer",caller); n.lens.reset(); s.invalidate_application(); }
    if (s.pending_ && s.pending_->node==p) s.pending_.reset();
}
void __cdecl NativeWorldFov::projection(uint32_t p) {
    auto &s=*instance_; CallbackScope callback(s);
    if (!s.lane()) {
        s.foreign_builder(p,true);
        try { invoke(s.original_projection_,{p}); }
        catch(...) { s.foreign_builder(p,false); s.invalidate_foreign(p); s.publish(p,EventKind::external); throw; }
        s.foreign_builder(p,false); return;
    }
    s.drain(); s.finish_pending(); s.native_projection(p);
}
void __cdecl NativeWorldFov::matrices(uint32_t p) {
    auto &s=*instance_; CallbackScope callback(s);
    if (s.lane()) s.ensure(p,true);
    else {
        s.foreign_builder(p,true);
        try { movement_wobble::NativeCameraBob::recalculate(s.original_matrices_,p); }
        catch(...) { s.foreign_builder(p,false); s.invalidate_foreign(p); s.publish(p,EventKind::external); throw; }
        s.foreign_builder(p,false);
    }
}
uint32_t __cdecl NativeWorldFov::get_projection(uint32_t p) {
    auto &s=*instance_; CallbackScope callback(s); if (s.lane()) s.ensure(p); return invoke(s.original_get_projection_,{p});
}
uint32_t __cdecl NativeWorldFov::frustum(uint32_t p) {
    auto &s=*instance_; CallbackScope callback(s); if (s.lane()) s.ensure(p); return invoke(s.original_frustum_,{p});
}
void __cdecl NativeWorldFov::custom(uint32_t p,uint32_t matrix,uint32_t orthogonal) {
    auto &s=*instance_; CallbackScope callback(s);
    ForeignWriter ticket;
    if(!s.lane()) ticket=s.begin_foreign(p,0); else { s.invalidate_foreign(p); s.foreign_pseudo_.erase(p); }
    try { invoke(s.original_custom_,{p,matrix,orthogonal}); }
    catch(...) { if(!s.lane()) (void)s.end_foreign(p,ticket); throw; }
    if(!s.lane()) (void)s.end_foreign(p,ticket);
    if (s.lane()) { s.drain(); s.invalidate_application(); auto found=s.nodes_.find(p); if(found!=s.nodes_.end()) { found->second.stamp.reset(); ++found->second.write_epoch; } }
    else s.publish(p,EventKind::external);
}
void NativeWorldFov::reconcile_weapons(bool publish_parent,bool live_service) {
    drain();
    const auto current=root();
    if (!current) { world_gate_=weapon_gate_=Gate::no_local_camera; return; }
    const auto p=read(current.camera,0x26c);
    if (!owns(current,p)) { world_gate_=weapon_gate_=Gate::unsupported_owner; return; }
    consume_foreign(p,current);
    std::optional<Stamp> prior;
    if (live_service) {
        const auto before=nodes_.find(p);
        if (before!=nodes_.end()) prior=before->second.stamp;
    }
    ensure(p);
    if (!publish_parent && !live_service) return;
    const auto before=nodes_.find(p);
    if (before==nodes_.end()) {
        world_rejected("node-record-absent",p,Node{}); weapon_gate_=Gate::unproved_lens; return;
    }
    const auto generation=before->second.generation;
    if (live_service) publish_parent=(publish_parent && before->second.stamp &&
        (!setting_ || before->second.stamp->effective)) || p!=published_node_ || generation!=published_generation_ ||
        !prior || !before->second.stamp || prior->option_epoch!=option_epoch_ ||
        prior->matrix!=before->second.stamp->matrix || prior->parameters!=before->second.stamp->parameters;
    // The original nested pass uploaded its child lens. Republish only a fresh
    // supported active receiver with exact standard-builder output, never a
    // saved parent node or opaque clean custom matrix.
    const auto serial=publication_serial_.load(std::memory_order_relaxed);
    const auto fresh=root();
    if ((track_weapon_ && publication_serial_.load(std::memory_order_relaxed)!=serial) || events_.load(std::memory_order_acquire)) { drain(); return; }
    if (fresh!=current || !owns(fresh,p)) return;
    const auto manager=read(scene_manager_);
    if (!manager || read(manager,0xdc)!=p) { world_gate_=weapon_gate_=Gate::inactive_camera; return; }
    auto found=nodes_.find(p);
    if (found==nodes_.end() || found->second.generation!=generation ||
        !found->second.stamp || !matches(p,*found->second.stamp) ||
        (read<uint16_t>(p,0x300)&2)) { world_gate_=Gate::unproved_standard_output; return; }
    if (track_weapon_) {
        const auto desired=effective(p,found->second,&fresh);
        const auto stamp=*found->second.stamp;
        const auto context=desired ? (regular_frame() ? Stamp::Context::weapon : Stamp::Context::world) : Stamp::Context::native;
        if (stamp.effective!=bool(desired) || stamp.context!=context ||
            bits(stamp.angle)!=bits(desired.value_or(read<float>(p,0x128)))) {
            if (!stamp.effective) return;
            // No guest query may follow this final source fence. Retire exact
            // owned output with the same native projection/full-geometry path,
            // then publish only this still-current active receiver as R1 allows.
            retire_weapon_stamp(p,generation);
            if(events_.load(std::memory_order_acquire)) { drain(); return; }
            found=nodes_.find(p);
            if(found==nodes_.end() || found->second.generation!=generation || !owns(fresh,p) ||
               read(scene_manager_)!=manager || read(manager,0xdc)!=p || !found->second.stamp ||
               found->second.stamp->effective || !matches(p,*found->second.stamp)) return;
            publish_parent=true; // Actual retirement also owns driver projection/view repair.
        }
    }
    if (publish_parent) invoke(render_camera_,{p,0});
    if (prepared_ && !regular_frame() && !events_.load(std::memory_order_acquire)) {
        found=nodes_.find(p);
        if (found!=nodes_.end() && found->second.generation==generation && owns(fresh,p) &&
            read(scene_manager_)==manager && read(manager,0xdc)==p && found->second.stamp &&
            matches(p,*found->second.stamp) && found->second.stamp->option_epoch==option_epoch_) {
            if (publish_parent) { published_node_=p; published_generation_=generation; }
            if (!setting_ || (found->second.stamp->effective && found->second.stamp->context==Stamp::Context::world)) {
                world_applied_epoch_=option_epoch_;
                if (publish_parent) world_applied_frame_=service_frame_;
                if (!weapon_setting_) { weapon_applied_epoch_=option_epoch_; weapon_applied_frame_=world_applied_frame_; }
            }
        }
    }
}
void __cdecl NativeWorldFov::weapons(uint32_t manager) {
    auto &s=*instance_; CallbackScope callback(s);
    if (!s.lane()) { invoke(s.original_weapons_,{manager}); return; }
    WeaponFrame frame{}; frame.parent=s.weapon_frame_;
    if(s.eligibility_pass_serial_!=UINT64_MAX) frame.eligibility_pass=++s.eligibility_pass_serial_;
    s.weapon_frame_=&frame;
    s.weapon_leaf_="not-observed"; s.weapon_stages_=0;
    auto leave=[&] {
        s.weapon_frame_=frame.parent;
        if (!frame.changed) return;
        if (frame.parent) frame.parent->changed=true;
        s.reconcile_weapons(frame.parent!=nullptr);
    };
    try { invoke(s.original_weapons_,{manager}); }
    catch (...) { leave(); throw; }
    if (s.prepared_ && frame.applied_epoch==s.option_epoch_) {
        s.weapon_applied_epoch_=s.option_epoch_; s.weapon_applied_frame_=s.service_frame_;
    }
    leave();
    if (s.prepared_ && s.committed_sequence_) s.publish_status(s.service_frame_,false);
}
void __cdecl NativeWorldFov::ctor1(uint32_t p,uint32_t db,uint32_t camera) {
    auto &s=*instance_; CallbackScope callback(s); s.publish(p); invoke(s.original_ctor1_,{p,db,camera});
}
void __cdecl NativeWorldFov::ctor2(uint32_t p,uint32_t vtt,uint32_t db,uint32_t camera) {
    auto &s=*instance_; CallbackScope callback(s); s.publish(p); invoke(s.original_ctor2_,{p,vtt,db,camera});
}
void __cdecl NativeWorldFov::dtor(uint32_t p,uint32_t vtt) {
    auto &s=*instance_; CallbackScope callback(s); s.publish(p); invoke(s.original_dtor_,{p,vtt});
}
void __cdecl NativeWorldFov::enable(uint32_t component,uint32_t value) {
    auto &s=*instance_; CallbackScope callback(s);
    if (!s.lane()) { invoke(s.original_enable_,{component,value}); return; }
    const auto saved_component=s.scoped_component_; const auto saved_dt=s.scoped_dt_; const bool saved_force=s.scoped_force_;
    s.scoped_component_=component; s.scoped_dt_=0; s.scoped_force_=true;
    try { invoke(s.original_enable_,{component,value}); s.finish_pending(); }
    catch (...) { s.scoped_component_=saved_component; s.scoped_dt_=saved_dt; s.scoped_force_=saved_force; throw; }
    s.scoped_component_=saved_component; s.scoped_dt_=saved_dt; s.scoped_force_=saved_force;
}
void __cdecl NativeWorldFov::helper(uint32_t component,uint32_t dt,uint32_t force) {
    auto &s=*instance_; CallbackScope callback(s);
    if (!s.lane()) { invoke(s.original_helper_,{component,dt,force}); return; }
    const auto saved_component=s.scoped_component_; const auto saved_dt=s.scoped_dt_; const bool saved_force=s.scoped_force_;
    s.scoped_component_=component; s.scoped_dt_=dt; s.scoped_force_=bool(force&0xff);
    try { invoke(s.original_helper_,{component,dt,force}); s.finish_pending(); }
    catch (...) { s.scoped_component_=saved_component; s.scoped_dt_=saved_dt; s.scoped_force_=saved_force; throw; }
    s.scoped_component_=saved_component; s.scoped_dt_=saved_dt; s.scoped_force_=saved_force;
}
NativeWorldFov::JeepUpdateScope::JeepUpdateScope(uint32_t component,uint32_t dt) {
    if (!instance_) return;
    if (!instance_->lane()) return;
    owner_=instance_; component_=component; dt_=dt; previous_=jeep_scope_; jeep_scope_=this;
}
NativeWorldFov::JeepUpdateScope::~JeepUpdateScope() {
    if (!owner_) return;
    owner_->finish_pending(); jeep_scope_=previous_;
}
void __cdecl NativeWorldFov::fast_node(uint32_t pointer) {
    auto &s=*instance_; CallbackScope callback(s); if (s.lane()) { s.drain(); s.finish_pending(); }
    invoke(s.original_fast_node_,{pointer});
}
void __cdecl NativeWorldFov::jeep_ctor1(uint32_t component,uint32_t actor) {
    auto &s=*instance_; CallbackScope callback(s); s.publish(component,EventKind::component); invoke(s.original_jeep_ctor1_,{component,actor});
}
void __cdecl NativeWorldFov::jeep_ctor2(uint32_t component,uint32_t actor) {
    auto &s=*instance_; CallbackScope callback(s); s.publish(component,EventKind::component); invoke(s.original_jeep_ctor2_,{component,actor});
}
void __cdecl NativeWorldFov::level_ctor1(uint32_t level) {
    auto &s=*instance_; CallbackScope callback(s); const auto epoch=++s.event_epoch_;
    s.publish(level,EventKind::level,epoch); invoke(s.original_level_ctor1_,{level}); s.publish(level,EventKind::level_seed,epoch);
}
void __cdecl NativeWorldFov::level_ctor2(uint32_t level) {
    auto &s=*instance_; CallbackScope callback(s); const auto epoch=++s.event_epoch_;
    s.publish(level,EventKind::level,epoch); invoke(s.original_level_ctor2_,{level}); s.publish(level,EventKind::level_seed,epoch);
}
void __cdecl NativeWorldFov::level_reset(uint32_t level) {
    auto &s=*instance_; CallbackScope callback(s); const auto epoch=++s.event_epoch_;
    s.publish(level,EventKind::level,epoch); invoke(s.original_level_reset_,{level}); s.publish(level,EventKind::level_seed,epoch);
}
NativeWorldFov::PreMixOwnerCapsule NativeWorldFov::acquire_premix_owners() {
    PreMixOwnerCapsule out; auto *s=instance_;
    if(!s || !s->lane() || !s->eligibility_ || s->camera_scope_) return out;
    CallbackScope callback(*s); s->drain();
    const auto current=s->root(); // The only callback-capable acquisition.
    if(!current || s->events_.load(std::memory_order_acquire)) return out;
    const auto p=read(current.camera,0x26c);
    const auto found=s->nodes_.find(p);
    if(found==s->nodes_.end() || !s->owns(current,p)) return out;
    const auto manager=read(current.player,0x170);
    if(!manager) return out;
    const auto begin=read(manager,0x148),end=read(manager,0x14c),index=read(manager,0x160);
    if(!begin || end<begin || (end-begin)%24 || index>=(end-begin)/24) return out;
    const auto weapon=read(begin,index*24+12);
    if(!weapon) return out;
    out.owners_={current.level,current.player,current.actor,current.camera,p,manager,weapon,
        begin,end,index,found->second.generation};
    out.publication_=s->publication_serial_.load(std::memory_order_acquire);
    return out;
}
viewmodel_eligibility::NativeSample NativeWorldFov::sample_premix_eligibility(const PreMixOwnerCapsule &c) {
    auto *s=instance_; const auto &o=c.owners_;
    if(!s || !s->lane() || !s->eligibility_ || !o.node || s->camera_scope_) return {};
    auto alive=[&] {
        if(s->events_.load(std::memory_order_acquire) ||
           s->publication_serial_.load(std::memory_order_acquire)!=c.publication_) return false;
        const auto it=s->nodes_.find(o.node);
        return it!=s->nodes_.end() && it->second.generation==o.camera_generation;
    };
    if(!alive()) return {};
    // Same sole native positive producer. No scene-render receipt, cached
    // Decision, FOV scalar, optical mode or duplicate FPS predicate.
    const auto sample=s->eligibility_->sample(o);
    return alive() && sample.decision.permits(sample.boundary) ? sample : viewmodel_eligibility::NativeSample{};
}
bool NativeWorldFov::premix_camera_alive(const PreMixOwnerCapsule &c) {
    auto* s=instance_;const auto& o=c.owners_;
    if(!s || !s->lane() || !o.node || s->events_.load(std::memory_order_acquire) ||
       s->publication_serial_.load(std::memory_order_acquire)!=c.publication_)return false;
    const auto found=s->nodes_.find(o.node);
    return found!=s->nodes_.end() && found->second.generation==o.camera_generation && read(o.node)==s->vtable_;
}
} // namespace nova::camera
