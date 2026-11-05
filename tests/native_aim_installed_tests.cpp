#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include "input/native_intents.h"
#include "fixtures/aim_intent_contract.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>
using nova::abi::invoke;
namespace {
void check(bool ok,const char *why) { if(!ok) throw std::runtime_error(why); }
uint32_t raw(void *p) { return reinterpret_cast<uint32_t>(p); }
nova::input::NativeAimIntent intent;
uint32_t gate_original=0,exit_original=0,toggle_original=0,touch_original=0,hud_original=0,hud_query=0,main_original=0,main_query=0,restore_original=0;
uint32_t watched_receiver=0; unsigned watched_epoch=0; bool omit_reload_guard=false; unsigned invocation_depth=0,adapter_samples=0,snapshot_reads=0,main_queries=0;
uint32_t current=0; unsigned epoch=1,scoped_epoch=0,depth=0,attempts=0;
bool scoped=false,attempted=false,replace_after_query=false,replace_during_hud=false,invalidate_during_main=false; 
bool local(uint32_t receiver) { return invocation_depth==1 && scoped && depth==1 && receiver==current && epoch==scoped_epoch; }
void invalidate(uint32_t receiver) {
    if(watched_receiver==receiver) { watched_receiver=0; ++epoch; }
}
uint32_t __cdecl query_touch(uint32_t receiver,uint32_t a,uint32_t b) {
    if(replace_after_query) { replace_after_query=false; invalidate(receiver); }
    return invoke(touch_original,{receiver,a,b});
}
uint32_t __cdecl hud(uint32_t receiver,uint32_t a,uint32_t b) {
    if(replace_during_hud) { replace_during_hud=false; invalidate(receiver); }
    return invoke(hud_original,{receiver,a,b});
}
uint32_t __cdecl main_state(uint32_t receiver,uint32_t a,uint32_t b) {
    ++main_queries;
    if(invalidate_during_main) { invalidate_during_main=false; invalidate(receiver); }
    return invoke(main_original,{receiver,a,b});
}
enum class ReloadState { unknown, free, busy };
ReloadState native_reload(uint32_t receiver) {
    if(omit_reload_guard) return ReloadState::free;
    if(!local(receiver) || watched_receiver!=receiver) return ReloadState::unknown;
    auto &s=*reinterpret_cast<HeldIntentFixture *>(receiver);
    if(s.reload_pending) return ReloadState::busy;
    const int index=static_cast<int>(invoke(main_query,{receiver,0,0}));
    if(!local(receiver) || watched_receiver!=receiver || index<0) return ReloadState::unknown;
    return nova::input::native_reload_blocks_aim(s.reload_pending!=0,index,s.configured_reload) ? ReloadState::busy : ReloadState::free;
}
uint32_t __cdecl gate(uint32_t receiver,uint32_t a,uint32_t b) {
    if(!local(receiver)) return invoke(gate_original,{receiver,a,b});
    watched_receiver=receiver; watched_epoch=epoch;
    const auto hud_button=invoke(hud_query,{receiver,0,0});
    if(!local(receiver) || watched_receiver!=receiver) return 0;
    const auto reload=native_reload(receiver);
    if(!local(receiver) || watched_receiver!=receiver || reload!=ReloadState::free) return 0;
    const auto result=invoke(gate_original,{receiver,a,b});
    if(!local(receiver) || watched_receiver!=receiver) return result&~255u;
    ++snapshot_reads;
    auto &s=*reinterpret_cast<HeldIntentFixture *>(receiver);
    const bool hud_enabled=reinterpret_cast<HeldIntentFixture *>(hud_button)->hud_enabled!=0;
    return intent.permit_start((result&255)!=0,s.starting!=0,s.enabled!=0,hud_enabled) ? result : result&~255u;
}
uint32_t __cdecl exit_aim(uint32_t receiver,uint32_t animated,uint32_t extra) {
    if(!local(receiver)) return invoke(exit_original,{receiver,animated,extra});
    watched_receiver=receiver; watched_epoch=epoch;
    auto &s=*reinterpret_cast<HeldIntentFixture *>(receiver);
    return s.enabled && intent.permit_exit() ? invoke(exit_original,{receiver,animated,extra}) : 0;
}
uint32_t __cdecl reload_restore(uint32_t receiver,uint32_t a,uint32_t b) {
    if(!omit_reload_guard && receiver==watched_receiver && watched_epoch==epoch) return 0;
    return invoke(restore_original,{receiver,a,b});
}
uint32_t __cdecl toggle(uint32_t receiver,uint32_t a,uint32_t b) {
    if(invocation_depth!=1 || !scoped || receiver!=current || scoped_epoch!=epoch || depth) return invoke(toggle_original,{receiver,a,b});
    if(attempted) return 0;
    attempted=true; ++attempts; ++depth;
    const auto result=invoke(toggle_original,{receiver,a,b}); --depth; return result;
}
void boundary(uint32_t update,uint32_t toggle_call,HeldIntentFixture &s,bool replace_after=false,
              bool reenter_pre=false,bool reenter_post=false) {
    nova::input::NativeInputInvocation invocation(invocation_depth);
    if(!invocation.outer()) { invoke(update,{raw(&s),intent.button() && s.hud_enabled}); return; }
    ++adapter_samples;
    // These recursive native consumers run before any published action scope
    // and after it has been removed, exactly the two former production gaps.
    if(reenter_pre) boundary(update,toggle_call,s);
    current=raw(&s); scoped_epoch=epoch; scoped=true; attempted=false;
    check(invoke(update,{current,intent.button() && s.hud_enabled})==0xdeed0000u,"native update original return survives");
    scoped=false;
    if(reenter_post) boundary(update,toggle_call,s);
    if(replace_after) ++epoch;
    scoped=true;
    if(!attempted && epoch==scoped_epoch) invoke(toggle_call,{current,0,0});
    scoped=false;
}
}
int main(int argc,char **argv) {
    try {
        check(argc==2 || (argc==3 && std::string_view(argv[2])=="--omit-reload-guard"),"fixture path / omission control");
        omit_reload_guard=argc==3; nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        auto fn=[&](const char *name) { return nova::abi::require_function(image,name); };
        nova::abi::GuestCall gate_call(image,"held_can_start",image.symbol("held_can_start_site")-fn("held_can_start"),"held_can_start_body",raw(reinterpret_cast<void *>(&gate)));
        gate_original=gate_call.original();
        nova::abi::GuestCall main_call(image,"held_main_state",image.symbol("held_main_state_site")-fn("held_main_state"),"held_main_state_body",raw(reinterpret_cast<void *>(&main_state)));
        main_original=main_call.original(); main_query=fn("held_main_state");
        nova::abi::GuestCall restore_call(image,"held_reload_restore",image.symbol("held_reload_restore_site")-fn("held_reload_restore"),"held_reload_restore_body",raw(reinterpret_cast<void *>(&reload_restore)));
        restore_original=restore_call.original();
        nova::abi::GuestCall touch_call(image,"held_query_touch",image.symbol("held_query_touch_site")-fn("held_query_touch"),"held_query_touch_body",raw(reinterpret_cast<void *>(&query_touch)));
        touch_original=touch_call.original();
        nova::abi::GuestCall hud_call(image,"held_hud_query",image.symbol("held_hud_query_site")-fn("held_hud_query"),"held_hud_body",raw(reinterpret_cast<void *>(&hud)));
        hud_original=hud_call.original(); hud_query=fn("held_hud_query");
        nova::abi::GuestCall exit_call(image,"held_exit",image.symbol("held_exit_site")-fn("held_exit"),"held_exit_body",raw(reinterpret_cast<void *>(&exit_aim)));
        exit_original=exit_call.original();
        nova::abi::GuestCall toggle_call(image,"held_toggle",image.symbol("held_toggle_site")-fn("held_toggle"),"held_switch",raw(reinterpret_cast<void *>(&toggle)));
        toggle_original=toggle_call.original();
        HeldIntentFixture s{0,0,1,0,1,0,0,0,0,0,1,0,0,0,3,7,4,10,0};
        intent.sample(true); // No native consumer for several host frames.
        for(unsigned host=0;host<17;++host) intent.sample(true);
        boundary(fn("held_update"),fn("held_toggle"),s);
        check(s.starts==0 && s.tutorial==1 && attempts==1,"refused initial native press keeps tutorial and one attempt");
        s.can_start=1; boundary(fn("held_update"),fn("held_toggle"),s); boundary(fn("held_update"),fn("held_toggle"),s);
        check(s.starts==1 && s.tutorial==1 && attempts==3,"zero/two native consumers per host frame cannot lose held retry or reset submission");
        check(s.starting && !s.active,"native submission remains opaque when its animation setter did not establish active ADS");
        check(invoke(fn("held_can_start"),{raw(&s),0,0})==0xa5000001u,"unscoped original query preserves full EAX");
        invoke(fn("held_interrupt"),{raw(&s),0}); boundary(fn("held_update"),fn("held_toggle"),s);
        check(s.starts==2,"native direct flag clear needs no host cancellation observer");
        invoke(fn("held_interrupt"),{raw(&s),0}); invoke(fn("held_enable"),{raw(&s),0});
        boundary(fn("held_update"),fn("held_toggle"),s); check(s.starts==2 && intent.wanted(),"disabled native permission does not erase physical hold");
        invoke(fn("held_enable"),{raw(&s),1}); boundary(fn("held_update"),fn("held_toggle"),s); check(s.starts==3,"native enable restores eligibility without physical repress");
        intent.sample(false); boundary(fn("held_update"),fn("held_toggle"),s); check(s.exits==0,"release while native entry is pending never forces exit");
        s.starting=0; s.active=1; boundary(fn("held_update"),fn("held_toggle"),s); boundary(fn("held_update"),fn("held_toggle"),s);
        check(s.exits==1 && s.animated==1,"ordinary release preserves original animated true argument once");
        intent.sample(true); boundary(fn("held_update"),fn("held_toggle"),s); check(s.starts==3,"native exit gate rejects reentry without animation churn");
        s.can_start=1; boundary(fn("held_update"),fn("held_toggle"),s); check(s.starts==4,"held desire survives native exit transition");
        invoke(fn("held_interrupt"),{raw(&s),0}); replace_after_query=true;
        boundary(fn("held_update"),fn("held_toggle"),s); check(s.starts==4,"changed token during original query cannot grant a new action");
        intent.cancel_input(); boundary(fn("held_update"),fn("held_toggle"),s); check(s.starts==4,"context loss cancels latest desire");
        const auto before=attempts; boundary(fn("held_update"),fn("held_toggle"),s,true);
        check(attempts==before,"owner change after native input return defers tail reconciliation");
        HeldIntentFixture foreign{0,0,1,1,1,0,0,0,0,0,1,0,0,0,3,7,4,10,0};
        scoped=true; current=raw(&s); scoped_epoch=epoch; depth=1; invocation_depth=1;
        check(invoke(fn("held_can_start"),{raw(&foreign),0,0})==0xa5000001u,"wrong receiver preserves original complete native query result");
        depth=2; s.active=1;
        check(invoke(fn("held_exit"),{raw(&s),1,0})==0xface0001u,"nested native exit always forwards original arguments and complete result");
        depth=0; scoped=false; invocation_depth=0;
        HeldIntentFixture denied{0,0,1,1,1,0,0,0,0,0,0,0,0,0,3,7,4,10,0};
        intent.cancel(); intent.sample(true);
        boundary(fn("held_update"),fn("held_toggle"),denied);
        check(!denied.starts && intent.wanted(),"disabled HUD independently blocks tail start without clearing physical desire");
        denied.hud_enabled=1; boundary(fn("held_update"),fn("held_toggle"),denied);
        check(denied.starts==1,"HUD reenable permits the still-held action");
        denied.hud_enabled=0; denied.starting=0; denied.active=1; intent.sample(false);
        boundary(fn("held_update"),fn("held_toggle"),denied);
        check(denied.exits==1 && denied.animated==1,"disabled aim HUD cannot suppress ordinary animated release");
        // No receiver is watched before this FIRST query. A native callback
        // inside that original query invalidates it before host snapshot reads.
        HeldIntentFixture first{0,0,1,1,1,0,0,0,0,0,1,0,0,0,3,7,4,10,0};
        intent.sample(true); watched_receiver=0; replace_after_query=true;
        auto before_reads=snapshot_reads; auto before_epoch=epoch;
        boundary(fn("held_update"),fn("held_toggle"),first);
        check(epoch!=before_epoch && !first.starts && snapshot_reads==before_reads,
              "first original query lifetime invalidation is watched before any post-query byte read");
        // A different previously watched address must not hide retirement of
        // the newly supplied receiver during the preceding native HUD getter.
        watched_receiver=raw(&foreign); replace_during_hud=true;
        before_reads=snapshot_reads; before_epoch=epoch; const auto before_queries=first.queries;
        boundary(fn("held_update"),fn("held_toggle"),first);
        check(epoch!=before_epoch && !first.starts && snapshot_reads==before_reads && first.queries==before_queries,
              "HUD getter invalidation prevents invoking original CanStartAim on a retired receiver");
        HeldIntentFixture closing{0,0,1,1,1,0,0,0,0,0,1,0,1,0,3,7,4,10,0};
        const auto same_epoch=epoch;
        boundary(fn("held_update"),fn("held_toggle"),closing);
        check(epoch==same_epoch && !closing.starting && !closing.active && !closing.can_start && closing.queries==1 && !closing.starts,
              "HUD getter closes native gate with identical receiver/epoch/entry; LAST original admission rejects without duplicate queries");
        HeldIntentFixture recursive{0,0,1,0,1,0,0,0,0,0,1,0,0,0,3,7,4,10,0};
        const auto before_samples=adapter_samples,before_attempts=attempts;
        boundary(fn("held_update"),fn("held_toggle"),recursive,false,true,true);
        check(recursive.updates==3 && adapter_samples==before_samples+1 && attempts==before_attempts+1 && invocation_depth==0,
              "pre-apply and post-refresh reentry forward native originals but cannot create another host adapter scope");
        HeldIntentFixture reload_case{0,1,1,1,1,0,0,0,0,0,1,0,0,0,3,7,4,10,0};
        intent.cancel(); intent.sample(true);
        check(invoke(fn("held_reload_begin"),{raw(&reload_case),0})==0xabcd0000u,"native full/no-ammo reload refusal is forwarded unchanged");
        boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(reload_case.active && !reload_case.exits && !reload_case.reload_pending,"refused reload cannot exit held ADS or manufacture a busy shadow");
        check(invoke(fn("held_reload_begin"),{raw(&reload_case),1})==0xabcd0001u,"accepted native reload owns original ordinary exit");
        check(!reload_case.active && reload_case.exits==1 && reload_case.animated==1 && reload_case.reload_pending && reload_case.main_state==5,
              "native StartReload path reaches animated UnAim without scoped host suppression");
        boundary(fn("held_update"),fn("held_toggle"),reload_case); boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(!reload_case.starts && intent.wanted() && reload_case.magazine==4 && reload_case.reserve==10,
              "native pending reload excludes otherwise eligible ADS without erasing held input or changing ammo");
        invoke(fn("held_reload_publish"),{raw(&reload_case),0}); invoke(fn("held_reload_commit"),{raw(&reload_case),0});
        boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(!reload_case.reload_pending && !reload_case.starts && reload_case.main_state==reload_case.configured_reload &&
              reload_case.magazine==10 && reload_case.reserve==4 && reload_case.ammo_commits==1,
              "native ammo event alone does not end reload priority; only original native code moves ammo");
        check(!reload_case.starting && reload_case.main_state==reload_case.configured_reload,
              "native OnEvent automatic Aim is deferred BEFORE its body while reload owns the ammo event; no one-frame ADS");
        // A native active flag is not permission to rewrite the reload state.
        // A second animated UnAim would select exit state 5 and abort it.
        reload_case.active=1; const auto before_active_queries=main_queries;
        boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(reload_case.active && reload_case.exits==1 && reload_case.main_state==reload_case.configured_reload && main_queries==before_active_queries,
              "active branch cannot replace native reload with another animated exit or add an unqualified main-slot query");
        reload_case.active=0; // The native fixture owns this deliberately injected flag.
        invoke(fn("held_reload_end"),{raw(&reload_case),0}); reload_case.can_start=1;
        boundary(fn("held_update"),fn("held_toggle"),reload_case); boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(reload_case.starts==1,"actual native reload end resumes one held ADS entry without a new press or timer");
        reload_case.starting=0; reload_case.active=1;
        invoke(fn("held_reload_begin"),{raw(&reload_case),1}); intent.sample(false);
        invoke(fn("held_reload_publish"),{raw(&reload_case),0}); invoke(fn("held_reload_end"),{raw(&reload_case),0}); reload_case.can_start=1;
        boundary(fn("held_update"),fn("held_toggle"),reload_case); check(reload_case.starts==1,"release during native reload prevents later ADS resumption");
        intent.sample(true); intent.cancel_input(); boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(reload_case.starts==1,"context cancellation cannot donate stale reload-completion aim");
        reload_case.main_state=-1; intent.sample(true); const auto invalid_queries=reload_case.queries;
        boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(reload_case.starts==1 && reload_case.queries==invalid_queries,"invalid current state index never reaches unsafe original state-type admission");
        reload_case.main_state=3; invalidate_during_main=true; const auto before_main_reads=snapshot_reads;
        boundary(fn("held_update"),fn("held_toggle"),reload_case);
        check(reload_case.starts==1 && snapshot_reads==before_main_reads,"native main-index query lifetime invalidation cannot grant ADS");
        check(invoke(fn("held_reload_restore"),{raw(&foreign),0,0})==0xdecafbadu && foreign.starts==1,
              "foreign native automatic Aim keeps its complete original call and result");
        std::puts("installed stable held ADS boundary passed");
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
