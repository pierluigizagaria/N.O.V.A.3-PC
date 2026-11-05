#include "menu/native_menu_observer.h"
#include "abi/guest_call.h"
#include "abi/guest_hook.h"
#include "fixtures/menu_observer_contract.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <windows.h>
using Observer=nova::menu::NativeMenuObserver;
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
uint32_t raw(const void *p) { return reinterpret_cast<uint32_t>(p); }
uint64_t original_result(unsigned index) { return (uint64_t(0xed000000u+index)<<32)|(0xac000000u+index); }
nova::ElfImage *image=nullptr;
MenuObserverFixture *fixture=nullptr;
unsigned action=0;
// Registered host callbacks are entered from original synthetic guest frames.
// No C++ exception may cross that ABI boundary, including assertion failures.
[[noreturn]] void callback_fail_fast(const char *boundary) noexcept {
    std::fprintf(stderr,"menu observer fixture callback trap: %s\n",boundary);
    std::fflush(stderr);
    std::abort();
}
uint64_t invoke(const char *name,std::initializer_list<uint32_t> args) {
    return nova::abi::call_guest_u64(nova::abi::require_function(*image,name),args.begin(),args.size());
}
void __cdecl replace() noexcept {
    try {
        // Revoke during an original callback, including same-address reconstruction.
        const char *name=action==0 ? "obs_unload" : action==1 ? "obs_c1" : action==2 ? "obs_d1" : "obs_d2";
        invoke(name,{raw(fixture->manager)});
    } catch(...) { callback_fail_fast("replace"); }
}
void __cdecl reenter() noexcept {
    try { invoke("obs_load",{raw(fixture->manager),0xdecafbad}); }
    catch(...) { callback_fail_fast("reenter"); }
}
void __cdecl check_loading() noexcept {
    if(!Observer::loading())callback_fail_fast("menu load resource scope missing");
}
void phase(MenuObserverFixture &s,unsigned value) { std::memcpy(s.manager+0x114,&value,4); }
MenuObserverFixture fresh() {
    MenuObserverFixture s{};
    s.input_name="menu_elements_nova3.bsprite";
    s.selected_name="menu_elements_nova3_1720x1280.bsprite";
    s.native_width=1920; s.flag=1; s.nominal_width=960; s.nominal_height=640;
    return s;
}
void bind(MenuObserverFixture &s) {
    fixture=&s;
    *reinterpret_cast<MenuObserverFixture **>(image->symbol("obs_state"))=&s;
}
Observer::FixtureBindings bindings(MenuObserverFixture &s,unsigned capacity=Observer::record_limit) {
    return {raw(&s.native_width),raw(&s.flag),0x114,capacity};
}
void construct(MenuObserverFixture &s,const char *which="obs_c1") {
    const unsigned index=std::strcmp(which,"obs_c2")==0 ? 1 : 0;
    check(invoke(which,{raw(s.manager)})==original_result(index),"constructor EDX:EAX unchanged");
}
void load(MenuObserverFixture &s,unsigned value) {
    phase(s,value);
    check(invoke("obs_load",{raw(s.manager),0xdecafbad})==original_result(5),"Load EDX:EAX unchanged");
    check(!Observer::loading(),"menu resource scope is retired after every Load invocation");
}
void initial(MenuObserverFixture &s) { construct(s); load(s,0); }
void reason(const Observer &o,const char *expected) {
    const auto s=o.snapshot();
    check(s.terminal && !s.complete && std::strcmp(s.reason,expected)==0,"exact INCONCLUSIVE reason");
}

using Backing=nova::menu::RttBackingCapture;
using Op=Backing::Op;
unsigned backing_mode=0,provider_calls=0;
void emitted(Backing::Event event) { Backing::forward(event,[] { ++provider_calls; }); }
void __cdecl backing_provider() noexcept {
    try {
        if(backing_mode==4) invoke("obs_unload",{raw(fixture->manager)});
        if(backing_mode==5) invoke("obs_begin",{raw(&fixture->rtt[1]),0x55000001});
        if(backing_mode==6) return;
        if(backing_mode==3) {
            for(unsigned i=0;i<Backing::event_limit+1;++i) emitted({Op::viewport,{0,0,960,640}});
            return;
        }
        emitted({Op::bind_framebuffer,{0x8d40,71,71}});
        emitted({Op::active_texture,{0x84c0}});
        emitted({Op::bind_texture,{0x0de1,81}});
        if(backing_mode!=1) emitted({Op::tex_image_2d,{0x0de1,0,0x1908,1024,1024,0,0x1908,0x1401,0}});
        emitted({Op::framebuffer_texture_2d,{0x8d40,0x8ce0,0x0de1,81,0}});
        emitted({Op::bind_renderbuffer,{0x8d41,91}});
        if(backing_mode!=1) emitted({Op::renderbuffer_storage,{0x8d41,0x81a5,1024,1024}});
        emitted({Op::framebuffer_renderbuffer,{0x8d40,0x8d00,0x8d41,91}});
        const uint32_t status=backing_mode==2 ? 0x8cd6u : 0x8cd5u;
        const auto result=Backing::forward({Op::check_framebuffer,{0x8d40}},[&] { ++provider_calls; return status; });
        check(result==status,"original status result unchanged");
        emitted({Op::viewport,{0,0,960,640}});
    } catch(...) { callback_fail_fast("backing_provider"); }
}
void begin(MenuObserverFixture &s,unsigned slot,unsigned mode=0,uint32_t clear=0xab000001) {
    backing_mode=mode; s.callback=raw(reinterpret_cast<void *>(&backing_provider)); s.callback_at=5;
    check(invoke("obs_begin",{raw(&s.rtt[slot]),clear})==original_result(12),"BeginRTT opaque EDX:EAX unchanged");
}
unsigned events(const Backing::Target &t,Op op) {
    unsigned n=0; for(unsigned i=0;i<t.count;++i) n+=t.events[i].op==op ? 1u : 0u; return n;
}
void backing_ready(MenuObserverFixture &s) { initial(s); load(s,10); load(s,16); }
}
int main(int argc,char **argv) {
    try {
        check(argc==2,"synthetic ELF path required");
        nova::ImportRegistry imports; nova::ElfImage loaded(argv[1],imports); image=&loaded;
        std::array<uint8_t,9> entry_before{}; std::array<uint8_t,5> call_before{};
        const auto load_at=image->symbol("obs_load"),call_at=image->symbol("obs_resource_site");
        std::memcpy(entry_before.data(),reinterpret_cast<const void *>(load_at),entry_before.size());
        std::memcpy(call_before.data(),reinterpret_cast<const void *>(call_at),call_before.size());
        std::array<uint8_t,6> begin_before{},rtt_death_before{};
        const auto begin_at=image->symbol("obs_begin"),rtt_death_at=image->symbol("obs_rtt_d1");
        std::memcpy(begin_before.data(),reinterpret_cast<const void *>(begin_at),begin_before.size());
        std::memcpy(rtt_death_before.data(),reinterpret_cast<const void *>(rtt_death_at),rtt_death_before.size());
        {
            auto s=fresh(); bind(s);
            std::array<uint8_t,9> before{};
            const auto at=image->symbol("obs_load"); std::memcpy(before.data(),reinterpret_cast<void *>(at),before.size());
            Observer disabled(*image); // Wrong production hash must not even be checked.
            initial(s); load(s,10); load(s,16);
            const auto snap=disabled.snapshot();
            check(!snap.enabled && !snap.count && !snap.output_acquisitions,"disabled does not acquire facts");
            check(!std::memcmp(before.data(),reinterpret_cast<void *>(at),before.size()),"disabled installs no entry hook");
            check(!std::memcmp(call_before.data(),reinterpret_cast<const void *>(call_at),call_before.size()),"disabled installs no CALL hook");
            auto *log=std::tmpfile(); check(log!=nullptr,"temporary output"); disabled.report(log,true);
            check(std::ftell(log)==0,"disabled emits no report"); std::fclose(log);
        }
        {
            bool rejected=false;
            try { Observer wrong_image(*image,true); }
            catch(const std::runtime_error &) { rejected=true; }
            check(rejected,"production opt-in rejects synthetic source identity");
            check(!std::memcmp(entry_before.data(),reinterpret_cast<const void *>(load_at),entry_before.size()),"identity refusal precedes entry mutation");
            check(!std::memcmp(call_before.data(),reinterpret_cast<const void *>(call_at),call_before.size()),"identity refusal precedes CALL mutation");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s));
            s.callback=raw(reinterpret_cast<void*>(&check_loading));s.callback_at=4;
            initial(s); load(s,10); load(s,16);
            const auto snap=o.snapshot();
            check(snap.complete && snap.count==6 && snap.output_acquisitions==3,"one complete CPU join");
            check(snap.records[0].kind==Observer::Record::Kind::resource &&
                  !std::strcmp(snap.records[0].name,s.input_name),"actual input name copied");
            check(!std::strcmp(snap.records[1].name,s.selected_name) && snap.records[1].width==1720 && snap.records[1].height==1280,
                  "selected filename/parsed dimensions stay distinct");
            check(snap.records[2].width==960 && snap.records[3].height==640 && snap.records[2].native_width==1920 && snap.records[2].flag==1,
                  "post-override nominal fields are facts, never inferred from filename");
            for(unsigned i=4;i<6;++i) check(snap.records[i].width==960 && snap.records[i].height==640 && snap.records[i].returned_normally,
                  "two original C1 invocations, no fabricated object return");
            check(snap.records[1].generation==snap.records[5].generation && snap.records[1].invocation!=snap.records[5].invocation,
                  "same initialization generation, distinct Load invocations");
            for(unsigned i=6;i<12;++i) check(s.counts[i]==1,"each resource/getter/constructor original once");
            check(s.counts[0]==1 && s.counts[5]==3,"constructor and each Load forwarded once");
            const unsigned expected[]={raw(&s.sprite_manager),raw(s.input_name),0x11000000,0x22000001,0x33000000,0xffffffff};
            check(!std::memcmp(expected,s.arguments[6],sizeof expected),"six argument words unchanged including bool padding");
            check(s.arguments[7][6]==0x33000000 && s.arguments[5][1]==0xdecafbad,"selector seventh argument and Load stream unchanged");
            check(s.last_low[0]==0x12345678 && s.last_high[0]==0xed000006,"resource return EDX:EAX unchanged");
            check(s.last_low[1]==0xac000007 && s.last_high[1]==0xed000007,"selector opaque EDX:EAX unchanged");
            check(s.last_low[2]==raw(s.sprite) && s.last_high[2]==0xed000008 && s.last_low[3]==raw(s.sprite) && s.last_high[3]==0xed000009,
                  "GetSprite pointer and EDX unchanged");
            check(s.last_low[4]==0xac00000a && s.last_high[4]==0xed00000a && s.last_low[5]==0xac00000b && s.last_high[5]==0xed00000b,
                  "void constructor opaque registers unchanged");
            const char *life[]={"obs_c2","obs_d1","obs_d2","obs_unload"};
            for(unsigned i=0;i<4;++i) check(invoke(life[i],{raw(s.manager)})==original_result(i+1),"remaining lifecycle ABI preserved");
            for(unsigned i=1;i<4;++i) check(s.counts[i]==1,"lifecycle forwarding exactly once");
            check(s.counts[4]==2,"direct Unload and D2-owned Unload each forwarded once");
            auto *log=std::tmpfile(); check(log!=nullptr,"temporary output"); o.report(log); const auto size=std::ftell(log);
            check(size>0,"completed report emitted"); o.report(log,true); check(std::ftell(log)==size,"report bounded to once"); std::fclose(log);
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); load(s,0);
            reason(o,"missing_constructed_owner"); check(s.counts[5]==1,"missing owner still forwards");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); construct(s); load(s,10);
            reason(o,"missed_startup_phase"); check(o.snapshot().output_acquisitions==0,"missed startup does not query outputs");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); construct(s,"obs_c2"); load(s,0);
            reason(o,"missing_constructed_owner"); // Base constructor alone is deliberately unsupported.
        }
        for(unsigned boundary=1;boundary<=3;++boundary) for(unsigned replacement=0;replacement<4;++replacement) {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); initial(s);
            action=replacement; s.callback=raw(reinterpret_cast<void *>(&replace)); s.callback_at=boundary;
            load(s,10); if(boundary==3) load(s,16);
            reason(o,"manager_lifecycle_replacement");
            const auto expected=boundary==1 ? 0u : boundary==2 ? 1u : 3u;
            check(o.snapshot().output_acquisitions==expected,"no post-callback output reads after revocation");
            check(s.counts[6]==1 && s.counts[7]==1 && s.counts[8]==1 && s.counts[9]==1,"retirement never suppresses original resource/getters");
            if(boundary==3) check(s.counts[10]==1 && s.counts[11]==1,"retirement never suppresses either original constructor");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s,2)); initial(s); load(s,10); load(s,16);
            reason(o,"record_overflow"); check(o.snapshot().count==2,"fixed buffer never overruns");
            check(s.counts[10]==1 && s.counts[11]==1,"overflow forwards later originals");
        }
        {
            auto s=fresh(); bind(s); char long_name[Observer::name_limit+1]; std::memset(long_name,'x',sizeof long_name); long_name[sizeof long_name-1]=0;
            s.selected_name=long_name; Observer o(*image,bindings(s)); initial(s); load(s,10);
            reason(o,"selector_output_unreadable_or_overflow");
        }
        {
            auto s=fresh(); bind(s); s.selected_name=""; Observer o(*image,bindings(s)); initial(s); load(s,10);
            reason(o,"selected_variant_empty");
        }
        {
            auto s=fresh(); bind(s); s.selected_name=reinterpret_cast<const char *>(1);
            Observer o(*image,bindings(s)); initial(s); load(s,10);
            reason(o,"selector_output_unreadable_or_overflow");
            check(s.counts[8]==1 && s.counts[9]==1,"unreadable selected output does not suppress native callers");
        }
        {
            auto s=fresh(); bind(s); s.input_name="unexpected.bsprite";
            Observer o(*image,bindings(s)); initial(s); load(s,10); reason(o,"unexpected_first_resource");
        }
        {
            auto s=fresh(); bind(s); s.suppress_rtt=1; Observer o(*image,bindings(s)); initial(s); load(s,10); load(s,16);
            reason(o,"two_original_creations_not_observed");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); initial(s);
            s.callback=raw(reinterpret_cast<void *>(&reenter)); s.callback_at=4; load(s,10);
            reason(o,"reentrant_load"); check(s.counts[5]==3,"reentrant original still called exactly once per entry");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); initial(s);
            std::thread other([&] { phase(s,10); invoke("obs_load",{raw(s.manager),0xdecafbad}); }); other.join();
            reason(o,"owner_thread_changed"); check(o.snapshot().output_acquisitions==0,"foreign consumer thread cannot acquire fields");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); auto *log=std::tmpfile(); check(log!=nullptr,"temporary output");
            o.report(log,true); reason(o,"initial_load_not_observed"); std::fclose(log);
        }
        {
            // Corrupt only the owned synthetic last-entry seam. Full preflight
            // must reject before mutating any of the five earlier entries/CALLs.
            auto s=fresh(); bind(s);
            auto *byte=reinterpret_cast<uint8_t *>(rtt_death_at+5);
            DWORD protection=0,ignored=0;
            check(VirtualProtect(byte,1,PAGE_EXECUTE_READWRITE,&protection)!=0,"fixture protection setup");
            const uint8_t saved=*byte; *byte=0x49;
            bool rejected=false;
            try { Observer malformed(*image,bindings(s)); }
            catch(const std::runtime_error &) { rejected=true; }
            *byte=saved;
            check(VirtualProtect(byte,1,protection,&ignored)!=0 && FlushInstructionCache(GetCurrentProcess(),byte,1)!=0,
                  "fixture restores exact synthetic byte and protection");
            check(rejected,"new last-entry mismatch rejects installation");
            check(!std::memcmp(entry_before.data(),reinterpret_cast<const void *>(load_at),entry_before.size()) &&
                  !std::memcmp(begin_before.data(),reinterpret_cast<const void *>(begin_at),begin_before.size()) &&
                  !std::memcmp(call_before.data(),reinterpret_cast<const void *>(call_at),call_before.size()),
                  "constructor preflight failure leaves previous seams untouched");
        }
        // T09f: mocked providers are reached through original synthetic BeginRTT.
        // These expected constants are fixtures, never claims about observed GL.
        {
            auto s=fresh(); bind(s); Observer disabled(*image); backing_ready(s); provider_calls=0; begin(s,0);
            check(provider_calls==10 && !disabled.backing_snapshot().enabled,"default-off forwards without capture");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s);
            auto *log=std::tmpfile(); check(log!=nullptr,"late backing report output");
            o.report(log); const auto cpu_size=std::ftell(log);
            provider_calls=0; begin(s,0); begin(s,1,0,0xab000000);
            auto b=o.backing_snapshot();
            for(unsigned i=0;i<2;++i) {
                const auto &t=b.targets[i];
                check(t.registered && t.returned && !t.invalid && t.count==10,"two bounded first-BeginRTT streams");
                check(t.manager==raw(s.manager) && t.receiver==raw(&s.rtt[i]) && t.generation==1 && t.invocation==3,
                      "exact constructor owner/generation/invocation joins BeginRTT");
                check(t.width==960 && t.height==640 && t.events[3].words[3]==1024 && t.events[3].words[4]==1024,
                      "logical dimensions remain distinct from supplied allocation request");
                check(t.events[8].result==0x8cd5 && t.events[9].words[2]==960 && t.events[9].words[3]==640,
                      "original status and viewport retained without queries");
                check(t.original_registers==original_result(12),"backing stores opaque registers");
            }
            check(b.targets[0].clear_word==0xab000001 && b.targets[1].clear_word==0xab000000 &&
                  s.arguments[12][1]==0xab000000,"full bool argument word preserved; low byte remains native");
            begin(s,0); check(provider_calls==30 && o.backing_snapshot().targets[0].count==10,"repeat begin forwards once but never recollects");
            o.report(log); const auto total=std::ftell(log); check(total>cpu_size,"backing report can follow earlier CPU report");
            o.report(log,true); check(std::ftell(log)==total,"each report emitted once"); std::fclose(log);
            check(invoke("obs_rtt_d1",{raw(&s.rtt[0])})==original_result(13),"RTT destructor registers unchanged");
            check(o.backing_snapshot().targets[0].retired && s.counts[13]==1,"RTT destructor revokes before forwarding");
        }
        for(unsigned mode=1;mode<=2;++mode) {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s); provider_calls=0; begin(s,0,mode);
            const auto t=o.backing_snapshot().targets[0];
            check(t.returned && !t.invalid,"negative provider observation retains normal native return");
            if(mode==1) check(events(t,Op::tex_image_2d)==0 && events(t,Op::renderbuffer_storage)==0 && t.count==8,
                             "cache-like omission never fabricates allocation or dimensions");
            if(mode==2) check(t.events[8].result==0x8cd6,"incomplete framebuffer status is retained as failure, never success");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s); provider_calls=0; begin(s,0,3);
            const auto t=o.backing_snapshot().targets[0];
            check(t.invalid && t.count==Backing::event_limit && !std::strcmp(t.reason,"event_overflow"),"capture overflow bounded and explicit");
            check(provider_calls==Backing::event_limit+1 && s.counts[12]==1,"overflow never suppresses original provider or BeginRTT");
        }
        for(unsigned mode=4;mode<=5;++mode) {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s); provider_calls=0; begin(s,0,mode);
            const auto b=o.backing_snapshot();
            check(b.targets[0].invalid && !b.targets[0].returned && !b.targets[0].count,"retirement/reentry prevents post-callback attribution");
            check(provider_calls==10 && s.counts[12]==(mode==5 ? 2u : 1u),"revocation preserves every original call");
            if(mode==4) check(b.targets[0].retired && b.targets[1].retired,"Unload revokes even after CPU COMPLETE");
            if(mode==5) check(!std::strcmp(b.targets[0].reason,"nested_begin"),"nested receiver has named refusal");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s); begin(s,0,6);
            const auto t=o.backing_snapshot().targets[0];
            check(t.returned && !t.count && !std::strcmp(t.reason,"no_observed_provider_calls"),"empty first begin is explicitly inconclusive for backing");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s);
            invoke("obs_rtt_d1",{raw(&s.rtt[0])}); provider_calls=0; begin(s,0);
            check(o.backing_snapshot().targets[0].retired && !o.backing_snapshot().targets[0].begun && provider_calls==10,
                  "same-address entry after retirement cannot reuse captured identity");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s);
            s.callback=raw(reinterpret_cast<void *>(&backing_provider)); s.callback_at=5; backing_mode=0; provider_calls=0;
            invoke("obs_begin",{0x12345678,0xff000001});
            check(!o.backing_snapshot().targets[0].begun && provider_calls==10,"unrelated RTT forwards without attribution or dereference");
        }
        {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s);
            std::thread other([&] { begin(s,0); }); other.join();
            const auto t=o.backing_snapshot().targets[0];
            check(t.invalid && !t.begun && !std::strcmp(t.reason,"begin_thread_changed"),"foreign-thread first begin rejected");
        }
        for(unsigned replacement=0;replacement<2;++replacement) {
            auto s=fresh(); bind(s); Observer o(*image,bindings(s)); backing_ready(s);
            invoke(replacement ? "obs_d1" : "obs_c1",{raw(s.manager)}); provider_calls=0; begin(s,0);
            const auto t=o.backing_snapshot().targets[0];
            check(t.retired && t.invalid && !t.begun && !t.count && provider_calls==10,
                  "manager destruction/reconstruction revokes after CPU COMPLETE");
        }
        {
            // Host exception control, without throwing across a guest stack.
            Backing c; c.created(0,1,1,1,2,3,960,640); bool caught=false;
            try { Backing::Scope scope(c,2,3,1); Backing::forward({Op::viewport,{}},[] { throw std::runtime_error("provider fault"); }); }
            catch(const std::runtime_error &) { caught=true; }
            check(caught && c.snapshot().targets[0].invalid && !c.snapshot().targets[0].count,
                  "abnormal provider return records no success and releases TLS scope");
        }
        check(!std::memcmp(entry_before.data(),reinterpret_cast<const void *>(load_at),entry_before.size()),"entry bytes restored at quiescent destruction");
        check(!std::memcmp(call_before.data(),reinterpret_cast<const void *>(call_at),call_before.size()),"CALL bytes restored at quiescent destruction");
        check(!std::memcmp(begin_before.data(),reinterpret_cast<const void *>(begin_at),begin_before.size()),"BeginRTT bytes restored at quiescent destruction");
        check(!std::memcmp(rtt_death_before.data(),reinterpret_cast<const void *>(rtt_death_at),rtt_death_before.size()),"RTT D1 bytes restored at quiescent destruction");
        std::puts("menu observer synthetic checks passed"); return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"menu observer fixture: %s\n",error.what()); return 1; }
}
