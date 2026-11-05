// SOURCE-ONLY acceptance extension for tests/native_world_fov_tests.cpp.
// Include inside its anonymous namespace after Harness and weapon_expected.
// Instantiated with the authored native ELF binding below. The production
// NativeProducer/NativeWorldFov path is unchanged by these fixture state writes.
// Binding changes authored native fixture state; it cannot mint Decisions.
// ordinary() restores synthetic native links/state; native(Reason) makes them
// ineligible; replace_owner/lose_owner alter fixture ownership. Owner indices:
// root0, actor1, health2, camera3, weapon4. No real game/profile is accessed.
// It must feed the SAME final consumer seam as NativeProducer, not patch matrices
// or replace NativeWorldFov's projection/cache logic. Camera storage replacement
// still uses the existing authored fixture ctor/destructor below.

// Every write below targets authored synthetic ELF storage only. No copied
// positive Decision is injected: production NativeProducer must run the original
// authored actor->health chain and all current owner/context checks itself.
class O09NativeBinding {
    Harness &h_; bool refresh_lens_=false;
  public:
    O09NativeBinding(NativeWorldFov &,Harness &h):h_(h) {}
    void restore_links() {
        auto &s=h_.scene;
        at(h_.image.symbol("fov_root_present"))=1;
        at(h_.image.symbol("_ZN6CLevel10m_instanceE"))=ptr(s.level);
        at(ptr(s.level),0x24c)=ptr(s.eligibility_actor);
        at(ptr(s.player),4)=ptr(s.eligibility_actor);
        at(ptr(s.camera),0x290)=ptr(s.eligibility_actor);
        at(ptr(s.eligibility_actor),0x118)=ptr(s.eligibility_health);
        at(ptr(s.player),0x170)=ptr(s.weapon_manager);
    }
    void ordinary() {
        restore_links(); auto &s=h_.scene;
        at<float>(ptr(s.eligibility_health),0x14)=100;
        at<uint8_t>(ptr(s.player),0xd)=1; at(ptr(s.player),0x130)=0;
        if(refresh_lens_) { h_.pseudo(at(ptr(s.camera),0x26c),70); refresh_lens_=false; }
    }
    void native(viewmodel_eligibility::Reason reason) {
        auto &s=h_.scene;
        if(reason==viewmodel_eligibility::Reason::unproved) at(ptr(s.eligibility_actor),0x118)=0;
        else if(reason==viewmodel_eligibility::Reason::lifetime_changed) at(ptr(s.player),0x130)=3;
        else at<float>(ptr(s.eligibility_health),0x14)=0;
    }
    void lose_owner(unsigned owner) {
        auto &s=h_.scene;
        if(owner==0) { at(h_.image.symbol("fov_root_present"))=0; refresh_lens_=true; }
        if(owner==1) { at(ptr(s.player),4)=0; refresh_lens_=true; }
        if(owner==2) at(ptr(s.eligibility_actor),0x118)=0;
        if(owner==4) at(ptr(s.player),0x170)=0;
    }
    void replace_owner(unsigned) { restore_links(); }
};

struct O09FirstSink {
    std::array<uint32_t,16> cpu{},driver{},view{};
    std::array<uint32_t,4> geometry{};
    uint32_t raw=0;
    bool operator==(const O09FirstSink &) const = default;
};
O09FirstSink o09_first_sink(Harness &h,uint32_t p) {
    O09FirstSink s;
    // Direct memory only at phase1, immediately after the FIRST driver sink.
    // No getProjectionMatrix/getViewFrustum/getFOV repair can hide failure.
    for(unsigned i=0;i<16;++i) {
        s.cpu[i]=at(p,0x200+i*4); s.driver[i]=bits(h.scene.driver_projection[i]);
        s.view[i]=bits(h.scene.driver_view[i]);
    }
    for(unsigned i=0;i<4;++i) s.geometry[i]=at(p,0x13c+i*4);
    s.raw=at(p,0x128); return s;
}
struct O09CallbackReset {
    ~O09CallbackReset() { weapon_callback={}; }
};
template<class Binding>
void viewmodel_eligibility_connected(const nova::ElfImage &image,const std::vector<LensCallSite> &sites) {
    namespace ve=nova::camera::viewmodel_eligibility;
    using Options=NativeWorldFov::Options;
    CodeSnapshot code(image);
    std::array<O09FirstSink,3> native;
    // Original Native baseline, including the two no-lens branches. All data is
    // authored synthetic fixture data, not extracted game content or a profile.
    {
        NativeWorldFov live(image,{},&sites,nullptr,{},true); live.bind_render_thread();
        Harness h(image); h.reset(); Binding binding(live,h); binding.ordinary();
        h.scene.diff_sample=0; h.scene.noop_set=1; live.service_options(1);
        for(unsigned mode=0;mode<3;++mode) {
            bool seen=false; h.scene.weapon_mode[0]=mode; O09CallbackReset reset;
            weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t p) {
                if(!depth && phase==1) { native[mode]=o09_first_sink(h,p); seen=true; }
            };
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
            check(seen,"O09 baseline reaches original first sink");
        }
    }
    {
        NativeWorldFov live(image,94.f,&sites,nullptr,104.f,true); live.bind_render_thread();
        Harness h(image); h.reset(); Binding binding(live,h); binding.ordinary();
        const auto p=h.node(); h.scene.diff_sample=0; h.scene.noop_set=1; live.service_options(1);
        const auto saved=live.live_status().committed;
        const auto preference_sequence=live.live_status().requested_sequence;
        check(saved==Options{94.f,104.f},"O09 fixture uses authorized maximum104 and independent world Custom");
        auto pass=[&](unsigned mode,bool late_revoke) {
            O09FirstSink observed; bool seen=false; h.scene.weapon_mode[0]=mode;
            const auto raw=at(p,0x128); O09CallbackReset reset;
            weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t node) {
                if(depth) return;
                if(phase==6 && late_revoke) binding.native(ve::Reason::native_non_fps);
                if(phase==1) { observed=o09_first_sink(h,node); seen=true; }
            };
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
            check(seen && at(p,0x128)==raw,"O09 original raw restore and first sink");
            check(live.live_status().committed==saved &&
                  live.live_status().requested_sequence==preference_sequence,
                  "O09 eligibility never rewrites requested/committed settings");
            return observed;
        };
        check(!live.request_options({94.f,104.1f}),"O09 preserves weapon maximum104");
        const auto custom=pass(0,false);
        check(custom.cpu!=native[0].cpu && custom.cpu==custom.driver,"O09 positive fixture applies actual Custom matrices");
        // This is the critical counterexample: raw setter is a no-op, previous
        // owner pointers are unchanged, and revocation happens AFTER that setter.
        const auto death=pass(0,true);
        check(death==native[0],"O09 first late-death CPU/full geometry/DRIVER must be original Native");
        binding.ordinary();
        check(pass(0,false)==custom,"O09 legitimate return restores Custom without settings edit");
        {
            // Entry admission has already run. The original camera body can
            // reenter before its clean direct driver publication (audit5).
            bool late=false;
            struct QueryReset { ~QueryReset() { query_callback={}; } } reset;
            query_callback=[&](uint32_t kind) {
                if(kind==5 && !late) { late=true; binding.native(ve::Reason::native_non_fps); }
            };
            check(pass(0,false)==native[0],"O09 first original-body death sink needs final direct-call fence");
            check(late,"O09 late original camera-body mutation was reached");
        }
        binding.ordinary(); check(pass(0,false)==custom,"O09 restores after original-body late death");
        {
            binding.native(ve::Reason::native_non_fps); bool returned=false;
            struct QueryReset { ~QueryReset() { query_callback={}; } } reset;
            query_callback=[&](uint32_t kind) { if(kind==5 && !returned) { returned=true; binding.ordinary(); } };
            check(pass(0,false)==custom,"O09 late legitimate FPS return restores before this first sink");
            check(returned,"O09 late return callback exercised");
        }
        // Retire the supplied camera in a callback AFTER entry admission, either
        // at the original-body return or inside the Native cleanup projector.
        // Poison witnesses reject an old helper, projection DRIVER, or native
        // VIEW continuation. Repair happens only AFTER phase1 assertions so
        // unrelated synthetic caller bookkeeping can complete safely.
        for(unsigned mutation:{0u,1u,2u}) {
            binding.ordinary(); (void)pass(0,false);
            bool retired=false,checked=false,armed_cleanup=false;
            unsigned drivers=0,views=0,matrices=0;
            struct QueryReset { ~QueryReset() { query_callback={}; } } query_reset;
            O09CallbackReset weapon_reset;
            query_callback=[&](uint32_t kind) {
                if(retired) return;
                if(kind==5) {
                    drivers=h.scene.eligibility_driver_calls; views=h.scene.eligibility_view_calls;
                    if(mutation==2) { armed_cleanup=true; binding.native(ve::Reason::native_non_fps); return; }
                }
                if((mutation<2 && kind==5) || (mutation==2 && kind==7 && armed_cleanup)) {
                    retired=true; matrices=h.scene.matrices;
                    invoke(h.fn(dtor_name),{p,0});
                    if(mutation==1) invoke(h.fn(ctor1_name),{p,91,92});
                    // New-lifetime or retired storage must never be repaired by
                    // the obsolete frame, even when its address is unchanged.
                    for(unsigned i=0;i<16;++i) at(p,0x200+i*4)=0x3f012345u+i;
                    h.scene.eligibility_poison_node=p;
                }
            };
            weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t) {
                if(depth || phase!=1) return;
                check(retired,"O09 late lifecycle callback reached");
                check(h.scene.eligibility_poison_reads==0,"O09 no retired helper/driver/view read");
                check(h.scene.eligibility_driver_calls==drivers && h.scene.eligibility_view_calls==views,
                      "O09 authenticated epilogue cancels both old publications");
                if(mutation==2) check(h.scene.matrices==matrices,"O09 fence between projector and matrices");
                for(unsigned i=0;i<16;++i) check(at(p,0x200+i*4)==0x3f012345u+i,
                    "O09 obsolete frame never overwrites replacement/poison cache");
                checked=true; h.scene.eligibility_poison_node=0;
                invoke(h.fn(ctor1_name),{p,101,102}); h.owner(p);
                at(ptr(h.scene.manager),0xdc)=p; binding.ordinary(); h.pseudo(p,70); h.matrices(p);
            };
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
            check(checked,"O09 late lifecycle cancellation first observation");
            query_callback={}; weapon_callback={};
            binding.ordinary(); check(pass(0,false)==custom,"O09 fresh legitimate lifetime restores Custom");
        }
        for(auto reason:{ve::Reason::native_non_fps,ve::Reason::unproved,ve::Reason::lifetime_changed}) {
            binding.native(reason);
            for(unsigned mode=0;mode<3;++mode)
                check(pass(mode,false)==native[mode],"O09 every ineligible weapon context stays Native, never falls through to world94");
            binding.ordinary(); check(pass(0,false)==custom,"O09 restoration after cinematic/unknown/owner revocation");
        }
        for(unsigned owner:{0u,1u,2u,4u}) {
            binding.lose_owner(owner);
            check(pass(0,false)==native[0],"O09 root/actor/health/weapon loss retains current-camera cleanup authority");
            binding.replace_owner(owner);
            if(owner==0 || owner==1)
                check(pass(0,false)==native[0],"O09 owner return alone cannot revive revoked optical provenance");
            const auto raw_before=at(p,0x128),setters_before=h.scene.setters;
            binding.ordinary();
            if(owner==0 || owner==1)
                check(at(p,0x128)==raw_before && h.scene.setters==setters_before+1 && h.scene.noop_set,
                      "O09 restoration uses the recognized same-value no-op lens writer");
            const auto queries_before=h.scene.eligibility_dead_calls;
            check(pass(0,false)==custom,"O09 restored current owner needs a fresh native query");
            check(h.scene.eligibility_dead_calls>queries_before,"O09 restored sink queries original liveness again");
        }
        // Replace the synthetic camera lifetime at the SAME address. The binding
        // must not manufacture proof from address equality or repair the old node.
        binding.native(ve::Reason::owner_lost);
        invoke(h.fn(dtor_name),{p,0});
        invoke(h.fn(ctor1_name),{p,111,222}); h.owner(p);
        at(ptr(h.scene.manager),0xdc)=p; h.pseudo(p,70); h.matrices(p);
        binding.replace_owner(3);
        check(pass(0,false)==native[0],"O09 same-address new camera starts Native without fresh proof");
        binding.ordinary(); check(pass(0,false)==custom,"O09 replacement camera restores only after fresh native proof");
        {
            O09CallbackReset reset; bool parent_seen=false,child_seen=false,parent_return=false;
            h.scene.nested_weapon=1; h.scene.weapon_limit=1;
            h.scene.weapon_mode[0]=0; h.scene.weapon_mode[1]=1;
            weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t node) {
                if(depth==1 && phase==0) binding.native(ve::Reason::native_non_fps);
                if(phase==1) {
                    check(o09_first_sink(h,node)==(depth ? native[1] : custom),
                          "O09 nested first sink uses the current child/parent scope");
                    if(depth) child_seen=true; else parent_seen=true;
                }
                if(depth==1 && phase==2) binding.ordinary(); // Fresh return proof, not old parent token.
                if(!depth && phase==3) {
                    check(o09_first_sink(h,node)==custom,"O09 child return republishes full parent CPU/DRIVER output");
                    parent_return=true;
                }
            };
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
            h.scene.nested_weapon=0;
            check(parent_seen && child_seen && parent_return,"O09 connected nested consumption reached all sinks");
            check(live.live_status().requested_sequence==preference_sequence,"O09 nested transitions retain request");
        }
        for(unsigned player_state:{0u,1u}) for(unsigned camera_state:{0u,1u,3u}) {
            binding.ordinary();
            at(ptr(h.scene.player),0x130)=player_state;
            at(ptr(h.scene.camera),0x348)=camera_state;
            check(pass(0,false)==custom,"O09 native ordinary states0/1 and camera LookTo/transition do not blacklist optics");
        }
        for(unsigned condition:{0u,1u,2u,3u}) {
            binding.ordinary();
            if(condition==0) at<uint8_t>(ptr(h.scene.player),0xd)=0;
            if(condition==1) at(ptr(h.scene.level),0x178)=0;
            if(condition==2) at(ptr(h.scene.eligibility_state),0)=0;
            if(condition==3) at(ptr(h.scene.player),0x130)=2;
            check(pass(0,false)==native[0],"O09 enabled/normal-interface/current-InGame/ordinary-state conjunction is required");
            at<uint8_t>(ptr(h.scene.player),0xd)=1;
            at(ptr(h.scene.level),0x178)=at(ptr(h.scene.level),0x17c);
            at(ptr(h.scene.eligibility_state),0)=image.symbol("_ZTV9GS_InGame")+8;
        }
        binding.ordinary();
        // O10: multiplayer resolves its own local row, never the SP actor slot.
        at(ptr(h.scene.eligibility_mp),4)=1;
        const auto before_mp=h.scene.eligibility_dead_calls;
        check(pass(0,false)==native[0],"O10 MP without a proved local player remains Native");
        check(h.scene.eligibility_dead_calls==before_mp,"O09 does not enter unproved MP health helper chain");
        const auto mp=ptr(h.scene.eligibility_mp),health=ptr(h.scene.eligibility_health);
        at(mp,0x3a04)=0;
        at(mp,0x3bdc)=ptr(h.scene.eligibility_mp_rows);
        at(mp,0x3be0)=ptr(h.scene.eligibility_mp_rows)+4;
        h.scene.eligibility_mp_rows[0]=ptr(h.scene.eligibility_mp_player);
        h.scene.eligibility_mp_player[0]=ptr(h.scene.eligibility_actor);
        at(ptr(h.scene.level),0x24c)=0;
        check(pass(0,false)==custom,"O10 local MP applies Custom without an SP actor");
        check(h.scene.eligibility_dead_calls==before_mp,"O10 MP consumer performs no component virtual query");
        for(unsigned mode:{0u,1u,2u,3u}) for(unsigned dead:{0u,1u}) for(unsigned frozen:{0u,1u}) {
            at(mp,0x3c00)=mode; at<float>(health,0x14)=dead ? 0.f : 100.f;
            at<uint8_t>(health,0xe)=static_cast<uint8_t>(frozen);
            const bool native_dead=nova::abi::invoke_bool(h.fn("_ZNK16CHealthComponent6IsDeadEv"),{health});
            check(pass(0,false)==(native_dead ? native[0] : custom),"O10 MP death/freeze first sink agrees with original health semantics");
        }
        at(mp,0x3c00)=0; at<uint8_t>(health,0xe)=0; at<float>(health,0x14)=100;
        check(pass(0,true)==native[0],"O10 late MP death restores Native before first driver sink");
        at<float>(health,0x14)=100;
        for(unsigned invalid:{0u,1u,2u,3u}) {
            if(invalid==0) at(mp,0x3a04)=13;
            if(invalid==1) at(mp,0x3be0)=at(mp,0x3bdc);
            if(invalid==2) h.scene.eligibility_mp_player[0]=0;
            if(invalid==3) at(health,4)=0;
            check(pass(0,false)==native[0],"O10 remote/missing local MP owner cannot inherit Custom optics");
            at(mp,0x3a04)=0; at(mp,0x3be0)=at(mp,0x3bdc)+4;
            h.scene.eligibility_mp_player[0]=ptr(h.scene.eligibility_actor);
            at(health,4)=ptr(h.scene.eligibility_actor);
        }
        at(ptr(h.scene.eligibility_mp),4)=0;
        binding.ordinary();
        // Manager lifetime must fail closed even with a non-null singleton.
        invoke(h.fn("_ZN13GameMpManagerD1Ev"),{ptr(h.scene.eligibility_mp)});
        check(at(image.symbol("_ZN13GameMpManager9SingletonE"))!=0,"fixture preserves non-null teardown counterexample");
        check(pass(0,false)==native[0],"O09 observed manager teardown defeats non-null singleton");
        invoke(h.fn("_ZN13GameMpManagerC1Ev"),{ptr(h.scene.eligibility_mp)});
        binding.ordinary(); check(pass(0,false)==custom,"O09 completed manager replacement restores fresh SP eligibility");
        // Melee/secondary layers can use any of the native Fovy applicators.
        // A same-value animation write must preserve the world mapping even
        // while weapon eligibility requests Native for the action.
        for(const char* writer:{"fov_site_vertical_direct","fov_site_vertical_blended",
                               "fov_site_vertical_key","fov_site_vertical_interpolated"}) {
            const float raw_angle=at<float>(p,0x128);
            invoke(h.fn(writer),{p,bits(raw_angle)});
            binding.native(ve::Reason::lifetime_changed);
            check(pass(0,false)==native[0],"O10b action fallback is still native for the weapon");
            h.geometry(p,2*std::atan(std::tan(94*pi/360)/reference_aspect));
            binding.ordinary();
            check(pass(0,false)==custom,"O10b action completion restores configured weapon optics");
        }
        // Preserve custom world outside a weapon scope. This check is separate
        // from all first-sink observations above and may use the normal getter.
        h.geometry(p,2*std::atan(std::tan(94*pi/360)/reference_aspect));
        check(live.request_options({}),"O09 explicit Reset queues Native"); live.service_options(2);
        bool seen=false; O09CallbackReset reset;
        weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t node) {
            if(!depth && phase==1) { check(o09_first_sink(h,node)==native[0],"O09 Reset first Native sink"); seen=true; }
        };
        h.scene.weapon_mode[0]=0;
        invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{ptr(h.scene.manager)});
        check(seen && live.live_status().committed==Options{},"O09 Reset remains original Native default");
    }
    code.unchanged();
}
// Source-only remaining Release acceptance: actual compiler/ABI lowering of the
// checked return-slot cancellation; foreign event sequencing; full existing FOV
// regressions including O04 forward/reverse trajectories. No run is claimed.
