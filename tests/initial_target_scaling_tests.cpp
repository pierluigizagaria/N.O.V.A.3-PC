// T09j2 connected HOST MOCK constructor/factory fixture.
// Exercises production hooks/RET4 landing and private lease. It does not execute
// the original game constructor and cannot establish real GL or visual success.
#include "menu/initial_target_scaling.h"
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "ui/menu_surfaces.h"
#include "abi/guest_hook.h"
#include "menu/native_menu_observer.h"
#include "fixtures/menu_observer_contract.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <stdexcept>
#include <thread>
#include <windows.h>
using Scale=nova::menu::InitialTargetScaling;
using nova::menu::menu_call_sret4;
namespace {
struct Resource { uint32_t refs=0,w=0,h=0; };
struct Target { Resource* color=nullptr; Resource* depth=nullptr; };
std::array<Resource,64> textures{},depths{};
std::array<Target,3> targets{};
std::array<std::array<uint32_t,30>,3> rtts{};
std::array<uint32_t,24> texture_manager{},app{},scene{};
uint32_t scene_global=0,application_global=0;
const char* push_case=nullptr;
std::array<unsigned,3> constructor_phase{};
std::map<std::string,Resource*> cache;
std::array<std::string,3> keys;
Resource foreign{7,1024,1024}; Resource* global=&foreign;
unsigned nt=0,nd=0,slot=0,releases=0,pushes=0,mode_now=0;
unsigned scaled_colors=0,native_colors=0; DWORD owner_lane=0;
bool refuse_depth=false,refuse_attach=false,null_color=false;
Scale* active_scale=nullptr;
nova::ElfImage* lifecycle_image=nullptr; uint32_t manager_id=41,generation_id=1;
uint32_t raw(const void* p) { return reinterpret_cast<uint32_t>(p); }
[[noreturn]] void fail(const char* name) noexcept { std::fprintf(stderr,"scaling fixture FAIL %s\n",name); std::fflush(stderr); std::abort(); }
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void send_lifecycle(uint32_t manager,uint32_t epoch) {
    if(epoch!=generation_id) { active_scale->lifecycle(manager,epoch); return; }
    const uint32_t args[]={manager};
    const auto result=nova::abi::call_guest_u64(nova::abi::require_function(*lifecycle_image,"obs_unload"),args,1);
    check(result==0xed000004ac000004ull,"actual lifecycle wrapper forwards original registers");
}
void cleanup() { for(auto& t:targets) { if(t.depth) --t.depth->refs; if(t.color) --t.color->refs; t={}; } }
}
extern "C" uint64_t __cdecl color_body(uint32_t out,uint32_t manager,uint32_t dimensions,uint32_t name,uint32_t format,uint32_t option) noexcept {
    try {
        check(manager==raw(texture_manager.data()) && format==14 && option==0xab000001,"factory arguments");
        auto* dims=reinterpret_cast<uint32_t*>(dimensions); auto* result=reinterpret_cast<uint32_t*>(out);
        const std::string key=reinterpret_cast<const char*>(name);
        auto& entry=cache[key];
        const bool scaled=dims[0]>1024;
        if(scaled) ++scaled_colors; else ++native_colors;
        if(null_color && !scaled) {
            check(scaled_colors==1 && native_colors==1 && releases==1,"genuine native retry before fatal null");
            std::fprintf(stderr,"scaling-fixture original-null-after-fallback\n");
        }
        if(null_color || (scaled && (mode_now==4 || (mode_now==5 && slot==1)))) {
            result[0]=0; return (uint64_t(0xaabbccdd)<<32)|out;
        }
        if(!entry) { check(nt<textures.size(),"texture bound"); entry=&textures[nt++]; *entry={1,dims[0],dims[1]}; }
        ++entry->refs; result[0]=raw(entry); result[1]=0; keys[slot]=key;
        return (uint64_t(0xaabbccdd)<<32)|out;
    } catch(...) { fail("color_body"); }
}
extern "C" uint64_t __cdecl depth_body(uint32_t out,uint32_t driver,uint32_t dimensions,uint32_t format) noexcept {
    try {
        check(driver==0x12345678 && format==0x2a,"depth factory args");
        auto* p=reinterpret_cast<uint32_t*>(out); auto* d=reinterpret_cast<uint32_t*>(dimensions);
        if(refuse_depth) { *p=0; return out; }
        check(nd<depths.size(),"depth bound"); auto& value=depths[nd++]; value={1,d[0],d[1]}; *p=raw(&value); return out;
    } catch(...) { fail("depth_body"); }
}
extern "C" __declspec(naked) void color_factory() {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,24
        and esp,-16
        mov edi,esp
        lea esi,[ebp+8]
        mov ecx,6
        cld
        rep movsd
        call color_body
        lea esp,[ebp-12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}
extern "C" __declspec(naked) void depth_factory() {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,16
        and esp,-16
        mov edi,esp
        lea esi,[ebp+8]
        mov ecx,4
        cld
        rep movsd
        call depth_body
        lea esp,[ebp-12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}
uint64_t __cdecl attach(uint32_t target,uint32_t kind,uint32_t reference) noexcept {
    try {
        check(kind==1,"depth attachment kind"); auto* d=*reinterpret_cast<Resource**>(reference);
        if(!d || refuse_attach) return 0xbbee0000ull;
        auto& t=*reinterpret_cast<Target*>(target); check(t.color && d->w==t.color->w && d->h==t.color->h,"physical metadata match");
        t.depth=d; ++d->refs; return 0xbbee0001ull;
    } catch(...) { fail("attach"); }
}
uint64_t __cdecl push(uint32_t application,uint32_t target_reference) noexcept {
    try { check(application==raw(app.data()) && target_reference==raw(rtts[slot].data())+8,"supplied receiver push"); check(constructor_phase[slot]==3,"push follows attachment and logical-field initialization");
        ++pushes; return 0x1122334455667788ull; }
    catch(...) { fail("push"); }
}
void __cdecl release(uint32_t pointer) noexcept {
    try { check(GetCurrentThreadId()==owner_lane,"guest release only on owner lane"); auto& r=*reinterpret_cast<Resource*>(pointer); check(r.refs>0,"release underflow"); --r.refs; ++releases; }
    catch(...) { fail("release"); }
}
uint64_t __cdecl original_c1(uint32_t receiver,uint32_t w,uint32_t h) noexcept {
    try {
        check(w==960 && h==640,"logical C1 arguments unchanged");
        auto* r=reinterpret_cast<uint32_t*>(receiver); std::memset(r,0,120);
        r[1]=raw(scene.data()); r[2]=raw(&targets[slot]);
        constructor_phase[slot]=1;

        const uint32_t dimensions[]={1024,1024}; uint32_t result[2]={0,0};
        const uint32_t a[]={raw(result),raw(texture_manager.data()),raw(dimensions),raw("native_four_name0"),14,0xab000001};
        const auto registers=menu_call_sret4(Scale::fixture_color_entry(),a,6);
        check(uint32_t(registers)==raw(result) && uint32_t(registers>>32)==0xaabbccdd,"SRET result and EDX preserved");
        check(result[0]!=0,"non-null constructor color"); r[3]=result[0]; targets[slot].color=reinterpret_cast<Resource*>(result[0]);
        if(slot==0 && mode_now>=6) {
            const unsigned before_release=releases, before_depth=nd;
            if(mode_now==6 || mode_now==7) {
                // Actual rejected constructor path and explicit observer fallback
                // path, both after the outer scaled color and before attachment.
                const unsigned before_scaled=scaled_colors,before_native=native_colors;
                slot=2;
                const auto nested=mode_now==6 ?
                    active_scale->construct(999,9,2,raw(rtts[2].data()),960,640,raw(reinterpret_cast<void*>(&original_c1))) :
                    active_scale->native_constructor(raw(rtts[2].data()),960,640,raw(reinterpret_cast<void*>(&original_c1)));
                check(nested==0x123456789abcdef0ull,"nested original result");
                check(rtts[2][0x48/4]==960 && targets[2].color->w==1024 && targets[2].depth==global,"all nested landings remain Native");
                check(scaled_colors==before_scaled && native_colors==before_native+1 && nd==before_depth,"no nested private allocation");
                --targets[2].color->refs; --targets[2].depth->refs; targets[2]={}; slot=0;
            } else if(mode_now==8) {
                send_lifecycle(999,generation_id); send_lifecycle(manager_id,generation_id+1);
                std::thread worker([]() noexcept { try { send_lifecycle(999,generation_id); send_lifecycle(manager_id,generation_id+1); } catch(...) { fail("unrelated_lifecycle"); } });
                worker.join();
            } else if(mode_now==9) {
                std::thread worker([]() noexcept { try { send_lifecycle(manager_id,generation_id); } catch(...) { fail("foreign_matched_lifecycle"); } });
                worker.join();
            } else if(mode_now==11) send_lifecycle(manager_id,generation_id);
            check(releases==before_release && nd==before_depth,"in-flight lease remains owned until original C1 returns");
        }
        if(push_case && !std::strcmp(push_case,"early-push"))
            Scale::fixture_push(raw(app.data()),receiver+8); // Must fail before value/UV checks.
        const auto attached=Scale::fixture_depth(raw(&targets[slot]),1,raw(&global));
        check(attached==(refuse_attach ? 0xbbee0000ull : 0xbbee0001ull),"attachment result preserved");
        constructor_phase[slot]=2;
        // Native 2b075b..2b0819 writes these only AFTER attachment.
        r[0x28/4]=r[0x38/4]=r[0x48/4]=960;
        r[0x2c/4]=r[0x3c/4]=r[0x4c/4]=640;
        r[0x60/4]=0x44700000; r[0x64/4]=0x44200000;
        r[0x70/4]=0x3f700000; r[0x74/4]=0x3f200000;
        constructor_phase[slot]=3;
        check(r[1]!=raw(app.data()) && r[1]==scene_global,"distinct native owner counterexample");
        uint32_t push_application=application_global, push_reference=receiver+8;
        if(push_case) {
            if(!std::strcmp(push_case,"wrong-app")) push_application=scene_global;
            if(!std::strcmp(push_case,"wrong-scene")) r[1]=application_global;
            if(!std::strcmp(push_case,"wrong-reference")) push_reference=receiver+12;
            if(!std::strcmp(push_case,"bad-viewport")) r[0x48/4]=1024;
            if(!std::strcmp(push_case,"bad-uv")) r[0x70/4]=0x3f800000;
        }
        // The EAX-receiver clone makes a nested ordinary two-word cdecl CALL.
        check(Scale::fixture_push(push_application,push_reference)==0x1122334455667788ull,"original push result");
        if(push_case && !std::strcmp(push_case,"repeat-push")) Scale::fixture_push(push_application,push_reference);
        constructor_phase[slot]=4; // Original clone applies +40 viewport then clear(3).
        check(r[0x48/4]<=targets[slot].color->w && r[0x4c/4]<=targets[slot].color->h,"clear viewport fits backing");
        constructor_phase[slot]=5; // C1 immediately invokes EndRTT; no menu draw here.
        // Original SafePop/release remains native; this mock records ordering only.
        check(r[0x28/4]==960 && r[0x2c/4]==640 && r[0x38/4]==960 && r[0x3c/4]==640 &&
              r[0x60/4]==0x44700000 && r[0x64/4]==0x44200000 && r[0x70/4]==0x3f700000 && r[0x74/4]==0x3f200000,"logical/hit/UV words unchanged");
        return 0x123456789abcdef0ull;
    } catch(...) { fail("original_c1"); }
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && !std::strcmp(argv[1],"--real-hooks")) {
            if(!std::filesystem::exists(argv[2])) { std::puts("real library missing; skipped"); return 77; }
            nova::ImportRegistry imports;
            nova::bionic::register_boot(imports);
            nova::bionic::CxaRegistry cxa(imports);
            nova::ElfImage original(argv[2],imports);
            std::vector<std::pair<uint32_t,std::vector<uint8_t>>> code;
            for(const auto& segment:original.segments()) if(segment.flags&1) {
                const auto at=original.address(segment.vaddr,segment.memsz);
                const auto* bytes=reinterpret_cast<const uint8_t*>(at);
                code.emplace_back(at,std::vector<uint8_t>(bytes,bytes+segment.memsz));
            }
            {
                nova::menu::NativeMenuObserver observer(original,false,true);
                check(!code.empty(),"original executable segments present");
                bool changed=false;
                for(const auto& saved:code)
                    changed|=std::memcmp(reinterpret_cast<const void*>(saved.first),saved.second.data(),saved.second.size())!=0;
                check(changed,"production menu hooks actually installed");
            }
            for(const auto& saved:code)
                check(!std::memcmp(reinterpret_cast<const void*>(saved.first),saved.second.data(),saved.second.size()),
                      "production menu hooks restore every executable byte");
            std::puts("original menu scaling hooks installed/restored; no initializers or gameplay executed");
            return 0;
        }
        const bool fatal_case=argc>1 && !std::strcmp(argv[1],"null-color");
        const bool push_failure=argc>1 && !std::strncmp(argv[1],"push-",5);
        if(push_failure) push_case=argv[1]+5;
        check(argc==((fatal_case || push_failure) ? 3 : 2),"synthetic observer ELF path required");
        nova::ImportRegistry imports;
        nova::ElfImage lifecycle_elf(argv[(fatal_case || push_failure) ? 2 : 1],imports); lifecycle_image=&lifecycle_elf;
        texture_manager[0x5c/4]=scene[0x10/4]=0x12345678;
        scene_global=raw(scene.data()); application_global=raw(app.data());
        const nova::platform::RenderLimits limits{4096,4096,4096,4096};
        auto p=nova::menu::initial_target_plan(1920,1080,limits);
        check(p.side==2048 && p.width==1920 && p.height==1280,"uniform1920");
        check(nova::menu::initial_target_plan(1280,720,limits).side==2048,"uniform720 coverage");
        check(!nova::menu::initial_target_plan(3840,2160,{2048,2048,4096,4096}).scaled,"cap fallback");
        check(!nova::menu::initial_target_plan(-1,1080,limits).scaled,"invalid extent");
        textures[nt]={1,1024,1024}; cache["native_four_name0"]=&textures[nt++];
        const auto old=cache["native_four_name0"];
        owner_lane=GetCurrentThreadId();
        for(unsigned mode=0;mode<12;++mode) {
            mode_now=mode; refuse_depth=mode==1; refuse_attach=mode==2;
            null_color=fatal_case;
            const auto before_releases=releases; const unsigned first_depth=nd;
            const auto before_scaled=scaled_colors,before_native=native_colors;
            Scale s({raw(reinterpret_cast<void*>(&color_factory)),raw(reinterpret_cast<void*>(&depth_factory)),raw(reinterpret_cast<void*>(&attach)),raw(reinterpret_cast<void*>(&push)),raw(reinterpret_cast<void*>(&release)),raw(&global),raw(&scene_global),raw(&application_global)});
            active_scale=&s;
            MenuObserverFixture lifecycle_state{};
            *reinterpret_cast<MenuObserverFixture**>(lifecycle_elf.symbol("obs_state"))=&lifecycle_state;
            using Observer=nova::menu::NativeMenuObserver;
            Observer observer(lifecycle_elf,Observer::FixtureBindings{raw(&lifecycle_state.native_width),raw(&lifecycle_state.flag),0x114,Observer::record_limit,&s});
            manager_id=raw(lifecycle_state.manager); generation_id=1;
            const uint32_t owner_args[]={manager_id};
            check(nova::abi::call_guest_u64(nova::abi::require_function(lifecycle_elf,"obs_c1"),owner_args,1)==0xed000000ac000000ull,"actual manager constructor wrapper");
            s.configure({1920,1080},limits);
            Target foreign_target{old,nullptr};
            check(Scale::fixture_depth(raw(&foreign_target),1,raw(&global))==(refuse_attach ? 0xbbee0000ull : 0xbbee0001ull),"foreign attachment result");
            if(foreign_target.depth) --foreign_target.depth->refs;
            const unsigned count=(mode==3 || mode==10) ? 1u : 2u;
            for(slot=0;slot<count;++slot) {
                check(s.construct(manager_id,generation_id,slot,raw(rtts[slot].data()),960,640,raw(reinterpret_cast<void*>(&original_c1)))==0x123456789abcdef0ull,"C1 result preserved");
                const bool native=refuse_depth || mode==4 || ((mode==5 || mode==9 || mode==11) && slot==1);
                check(constructor_phase[slot]==5,"constructor clear/restore completed before publication");
                check(rtts[slot][0x48/4]==960u,"native viewport remains logical for projection and layout");
                const auto surface=nova::ui::MenuSurfaces::receiver(raw(rtts[slot].data()));
                check(native ? !surface.receiver : surface.density==2 && surface.width==960 && surface.height==640,
                      "only enlarged menu surfaces publish GPU raster density");
                check(targets[slot].color->w==(native ? 1024u : 2048u),"native factory metadata scaled");
                if(mode==10) {
                    const auto before=releases;
                    std::thread worker([]() noexcept { try { send_lifecycle(manager_id,generation_id); } catch(...) { fail("foreign_gap_lifecycle"); } });
                    worker.join();
                    check(releases==before && depths[first_depth].refs==2,"foreign gap callback publishes without releasing");
                }
            }
            s.finish();
            check(global==&foreign,"global identity unchanged");
            if(!refuse_depth) {
                check(releases==before_releases+1,"factory lease released once");
                const unsigned retained=(refuse_attach || mode==4) ? 0u :
                    (mode==3 || mode==5 || mode==9 || mode==10 || mode==11) ? 1u : 2u;
                check(depths[first_depth].refs==retained,"target reference balance");
                if(mode==0 || mode==6 || mode==7 || mode==8)
                    check(targets[0].depth==targets[1].depth && keys[0]!=keys[1],"private shared pair / unique slots");
            }
            if(mode==4) check(scaled_colors==before_scaled+1 && native_colors==before_native+2,"scaled refusal retries original then keeps pair Native");
            if(mode==5) check(scaled_colors==before_scaled+2 && native_colors==before_native+1,"partial pair retries only failed slot, preserves successful target");
            check(old->w==1024 && old->h==1024,"old cached texture unchanged");
            cleanup(); check(foreign.refs==7,"foreign global references preserved after native cleanup");
            if(!refuse_depth) check(depths[first_depth].refs==0,"no private leak after cleanup");
            active_scale=nullptr;
        }
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
