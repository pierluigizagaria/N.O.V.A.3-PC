// Actual production NativeRigidDrawBinding::sample -> NativeWorldFov wrapper ->
// NativeProducer is exercised. Fixture ELF supplies addresses/native storage,
// never a synthetic Decision or a replacement production predicate.
void rigid_draw_activation(const nova::ElfImage& image,const std::vector<LensCallSite>& sites) {
    CodeSnapshot code(image);
    {
        NativeWorldFov live(image,{},&sites,nullptr,{},true);live.bind_render_thread();
        Harness h(image);h.reset();O09NativeBinding eligibility(live,h);eligibility.ordinary();
        std::array<uint8_t,0x110> object{};
        std::array<uint8_t,0x1b0> states{};std::array<uint8_t,0x180> animation{};
        std::array<uint8_t,32> state_runtime{};std::array<uint8_t,0x38> slot{};std::array<uint8_t,0x4c> timeline{};
        std::array<uint8_t,0x160> root{},draw{},mesh{},driver{},foreign{};
        const auto drawp=ptr(draw),rootp=ptr(root),meshp=ptr(mesh),driverp=ptr(driver);
        const auto manager=ptr(h.scene.manager),weapon=ptr(h.scene.weapon_component);
        at(weapon,4)=ptr(object);at(ptr(object),0x98)=rootp;
        at(weapon,0x10)=ptr(states);at(weapon,0xf4)=0;at(ptr(object),0x100)=ptr(animation);
        at(ptr(states),0xf8)=ptr(state_runtime);at(ptr(states),0x120)=ptr(animation);
        at(ptr(animation),0x20)=ptr(slot);at(ptr(slot),0x28)=ptr(timeline);
        at(ptr(state_runtime))=20;at(ptr(slot))=114;at(ptr(timeline),0x30)=114;
        at(drawp)=image.symbol("v01_rigid_node_table");at(drawp,0xec)=drawp;at(drawp,0xe8)=manager;
        at(drawp,0x108)=meshp;at(drawp,0xd0)=rootp;at(meshp)=image.symbol("v01_rigid_mesh_table");
        at(manager,0xac)=drawp;at(manager,0x10)=driverp;at(manager,0x10c)=4;
        for(unsigned i=0;i<16;++i) at(drawp,0x10+4*i)=0x3f000000u+i;
        const auto calls=image.symbol("v01_rigid_calls"),args=image.symbol("v01_rigid_args");
        auto emit=[&] { invoke(h.fn("v01_rigid_draw_entry"),{drawp}); };
        const auto before=at(calls),queries=h.scene.queries,dead=h.scene.eligibility_dead_calls;
        emit();
        check(at(calls)==before+1 && at(args)==meshp && at(args,4)==driverp && at(args,8)==drawp+0x10,
              "V01i installed default wrapper forwards exact original call once");
        auto status=live.rigid_draw_status();
        check(!status.acquisitions&&!status.samples&&!status.admitted && h.scene.queries==queries &&
              h.scene.eligibility_dead_calls==dead,"V01i default0 bypass before all added owner/pose queries");
        check(live.set_rigid_draw_request({.01f,0}),"V01i render-lane nonzero structural request");
        h.scene.noop_set=1;h.scene.diff_sample=0;h.scene.weapon_mode[0]=0;
        unsigned hits=0;bool revoke=false,replace=false,retire=false,selected_ahead=false;
        { O09CallbackReset reset;
          weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t) {
            if(phase!=1 && phase!=3) return;
            at(manager,0xac)=drawp;at(manager,0x10)=driverp;at(manager,0x10c)=4;
            if(revoke) eligibility.native(viewmodel_eligibility::Reason::native_non_fps);
            if(replace) at(ptr(object),0x98)=ptr(foreign);
            if(retire) invoke(h.fn(dtor_name),{h.node(),0});
            if(selected_ahead) {
                // Authored native-layout replay of the separately proved original
                // successor ordering: selected/controller advance, model has not.
                at(ptr(state_runtime))=1;at(ptr(slot))=68;at(ptr(timeline),0x30)=68;
                at<uint8_t>(weapon,0x60)=0;at<uint8_t>(weapon,0x61)=0;
            }
            const auto stats=live.rigid_draw_status();const auto original_calls=at(calls);
            emit();++hits;const auto after=live.rigid_draw_status();
            check(after.samples==stats.samples+1 && at(calls)==original_calls+1,"V01i actual installed production sink and original continuation reached");
            if(revoke||replace||retire) check(after.admitted==stats.admitted,"V01i actual O09/current-root loss denies receipt");
            else check(after.admitted==stats.admitted+1,"V01i actual binding AND O09 wrapper admit supplied current branch");
            for(unsigned i=0;i<16;++i) check(at(image.symbol("v01_rigid_matrix"),i*4)==at(drawp,0x10+i*4),
                  "V01i structural installation preserves exact native model including selected-ahead case");
            if(retire) {
                // Repair only AFTER stale-capsule rejection; storage is fixture-owned.
                invoke(h.fn(ctor1_name),{h.node(),101,102});h.owner(h.node());
                at(manager,0xdc)=h.node();eligibility.ordinary();h.pseudo(h.node(),70);
            }
            (void)depth;
          };
          auto pass=[&] {invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{manager});};
          pass();const auto admitted=live.rigid_draw_status().admitted;
          check(admitted>0,"V01i positive production seam must be reached, not skipped");
          revoke=true;pass();revoke=false;eligibility.ordinary();
          replace=true;pass();replace=false;at(ptr(object),0x98)=rootp;
          retire=true;pass();retire=false;
          selected_ahead=true;pass();selected_ahead=false;
          h.scene.nested_weapon=1;h.scene.weapon_limit=1;h.scene.weapon_mode[1]=0;pass();h.scene.nested_weapon=0;
        }
        // New-acquisition counterexample: mutate camera lifetime INSIDE the
        // callback-capable root query, before original camera continuation.
        for(unsigned reconstruction:{0u,1u}) {
            bool armed=false,mutated=false,checked=false;
            unsigned setups=0,drivers=0,views=0;uint64_t acquisitions=0;
            struct QueryReset { ~QueryReset(){query_callback={};} } query_reset;
            O09CallbackReset weapon_reset;
            query_callback=[&](uint32_t kind) {
                if(!armed||mutated||kind!=1) return;
                check(live.rigid_draw_status().acquisitions==acquisitions+1,
                      "V01i mutation occurs inside NEW owner acquisition");
                mutated=true;armed=false;
                invoke(h.fn(dtor_name),{h.node(),0});
                if(reconstruction) invoke(h.fn(ctor1_name),{h.node(),201,202});
                for(unsigned i=0;i<16;++i) at(h.node(),0x200+4*i)=0x3e112200u+i;
                h.scene.eligibility_poison_node=h.node();
            };
            weapon_callback=[&](uint32_t depth,uint32_t phase,uint32_t) {
                if(depth) return;
                if(phase==6) {
                    acquisitions=live.rigid_draw_status().acquisitions;
                    setups=h.scene.camera_setups;drivers=h.scene.eligibility_driver_calls;
                    views=h.scene.eligibility_view_calls;armed=true;
                }
                if(phase!=1) return;
                check(mutated,"V01i acquisition lifetime callback reached");
                check(h.scene.camera_setups==setups && h.scene.eligibility_driver_calls==drivers &&
                      h.scene.eligibility_view_calls==views && !h.scene.eligibility_poison_reads,
                      "V01i acquisition retirement cancels stale original camera/driver continuation");
                for(unsigned i=0;i<16;++i) check(at(h.node(),0x200+4*i)==0x3e112200u+i,
                      "V01i obsolete acquisition never changes replacement camera sentinel");
                const auto before_admission=live.rigid_draw_status().admitted;
                at(manager,0xac)=drawp;at(manager,0x10c)=4;at(manager,0x10)=driverp;emit();
                check(live.rigid_draw_status().admitted==before_admission,
                      "V01i retired acquisition produces zero stale draw admission");
                checked=true;h.scene.eligibility_poison_node=0;
                // Repair after all assertions, solely for authored caller bookkeeping.
                invoke(h.fn(ctor1_name),{h.node(),301,302});h.owner(h.node());
                at(manager,0xdc)=h.node();eligibility.ordinary();h.pseudo(h.node(),70);h.matrices(h.node());
            };
            invoke(h.fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{manager});
            check(checked,"V01i retire/reconstruct acquisition gate reached");
        }
        check(hits>=8,"V01i connected current/ownerloss/selected-ahead/nested boundaries reached");
        // Leaving the pass discards its capsule. A later bare sink cannot reuse it.
        const auto old=live.rigid_draw_status();emit();
        check(live.rigid_draw_status().samples==old.samples && live.rigid_draw_status().admitted==old.admitted,
              "V01i scope exit does not retain a pass receipt");
        const auto foreign_before=live.rigid_draw_status();
        std::async(std::launch::async,[&]{emit();}).get();
        check(live.rigid_draw_status().samples==foreign_before.samples,"V01i foreign caller forwards without render owners");
        check(live.set_rigid_draw_request({}),"V01i explicit0 restores immediate bypass");
        const auto reset=live.rigid_draw_status();emit();
        check(live.rigid_draw_status().samples==reset.samples,"V01i reset bypasses owner/pose sampling");
    }
    code.unchanged();
}
