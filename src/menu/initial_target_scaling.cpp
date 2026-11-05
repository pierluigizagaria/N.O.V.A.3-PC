#include "menu/initial_target_scaling.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include "ui/menu_surfaces.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace nova::menu {
static_assert(sizeof(void*)==4,"T09j requires x86");
namespace {
[[noreturn]] void trap(const char* name) noexcept {
    std::fprintf(stderr,"initial-menu-scaling FAIL boundary=%s\n",name);
    std::fflush(stderr); TerminateProcess(GetCurrentProcess(),86); std::abort();
}
uint32_t raw(const void* p) noexcept { return reinterpret_cast<uint32_t>(p); }
uint32_t word(uint32_t p) noexcept { uint32_t v; std::memcpy(&v,reinterpret_cast<const void*>(p),4); return v; }
const char* rtt="_ZN14CRTTMenuObjectC1Eii";
const char* clone="_ZN14CRTTMenuObject8BeginRTTEb.clone.10";
const char* color="_ZN6glitch5video15CTextureManager10addTextureERKNS_4core11dimension2dIiEEPKcNS0_14E_PIXEL_FORMATEb";
}
extern "C" __declspec(naked) uint64_t __cdecl menu_call_sret4(uint32_t,const uint32_t*,uint32_t) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        mov ecx,[ebp+16]
        mov eax,ecx
        shl eax,2
        sub esp,eax
        and esp,-16
        mov edi,esp
        mov esi,[ebp+12]
        cld
        rep movsd
        call dword ptr [ebp+8]
        lea esp,[ebp-12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
extern "C" uint64_t __cdecl menu_color_body(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) noexcept;
extern "C" __declspec(naked) void menu_color_entry() {
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
        call menu_color_body
        lea esp,[ebp-12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}
struct InitialTargetScaling::Impl {
    static Impl* installed;
    static std::atomic<uint32_t> next_install;
    struct Scope { Impl* owner; uint32_t receiver; unsigned slot; bool color=false,depth=false,viewport=false; };
    static thread_local Scope* current;
    uint32_t install=0,thread=0,manager=0,generation=0,slots=0,driver=0;
    std::atomic<uint32_t> owner_lane{0};
    std::atomic<uint64_t> identity{0},revoked{0};
    static uint64_t token(uint32_t owner,uint32_t epoch) noexcept { return (uint64_t(owner)<<32)|epoch; }
    uint32_t depth_factory=0,release=0,depth_return=0,global_depth=0;
    uint32_t color_original=0,depth_original=0,push_original=0;
    uint32_t scene_manager_global=0,application_global=0;
#ifdef NOVA_INITIAL_TARGET_SCALING_FIXTURE
    bool fixture=false;
    explicit Impl(FixtureBindings b):depth_factory(b.depth_factory),release(b.release),global_depth(b.global_depth),color_original(b.color),depth_original(b.attach),push_original(b.push),scene_manager_global(b.scene_manager_global),application_global(b.application_global),fixture(true),lease(this,&release_lease) {
        if(installed) throw std::runtime_error("fixture installation collision");
        install=++next_install; installed=this;
    }
#endif
    InitialTargetPlan plan{};
    bool configured=false,used=false,admitted=false;
    std::array<std::array<char,96>,2> keys{};
    InitialDepthLease lease;
    RttBackingCapture creation;
    std::unique_ptr<abi::GuestCall> color_call,push_call;
    std::unique_ptr<abi::GuestHook> depth_hook;
    explicit Impl(const ElfImage& image);
    static void release_lease(void* context,uint32_t p) noexcept {
        try { auto& s=*static_cast<Impl*>(context); const uint32_t a[]={p}; abi::call_guest(s.release,a,1); }
        catch(...) { trap("private_depth_release"); }
    }
    void request_revocation() noexcept {
        const auto active=identity.load(std::memory_order_acquire);
        if(active) revoked.store(active,std::memory_order_release);
    }
    bool revocation_pending() const noexcept {
        const auto active=identity.load(std::memory_order_acquire);
        return active && revoked.load(std::memory_order_acquire)==active;
    }
    void finish() noexcept {
        if(owner_lane.load(std::memory_order_acquire)!=GetCurrentThreadId()) {
            request_revocation(); return; // NO plain state access or guest call.
        }
        admitted=false;
        identity.store(0,std::memory_order_release);
        lease.reset();
    }
    uint64_t native_constructor(uint32_t original,const uint32_t* args) {
        // ALL rejected/nested routes quarantine the actual factory/attach/push
        // landings. Restoring the outer TLS is required even on host exceptions.
        struct Quarantine {
            Scope* saved;
            Quarantine():saved(current) { current=nullptr; }
            ~Quarantine() { current=saved; }
        } quarantine;
        if(quarantine.saved) creation.reject("nested_native_constructor");
        return abi::call_guest_u64(original,args,3);
    }
    bool scoped() const noexcept {
        return current && current->owner==this && admitted && thread==GetCurrentThreadId();
    }
    uint64_t color_factory(uint32_t output,uint32_t tm,uint32_t dims,uint32_t name,uint32_t format,uint32_t option) {
        const uint32_t original_args[]={output,tm,dims,name,format,option};
        if(!scoped()) return menu_call_sret4(color_original,original_args,6);
        auto& scope=*current;
        if(scope.color || word(dims)!=1024 || word(dims+4)!=1024 || format!=14 || (option&255)!=1)
            trap("color_scope_contract");
        scope.color=true;
        const uint32_t supplied_driver=word(tm+0x5c);
        const uint32_t scene=word(scope.receiver+4);
        if(!supplied_driver || !scene || word(scene+0x10)!=supplied_driver) trap("color_driver_identity");
        const uint32_t physical[]={plan.side,plan.side};
        if(scope.slot==0) {
            driver=supplied_driver;
            uint32_t owned=0;
            const uint32_t args[]={raw(&owned),driver,raw(physical),0x2a};
            menu_call_sret4(depth_factory,args,4);
            lease.adopt(owned);
            if(!owned) {
                // Native capability/factory refusal before any scaled color:
                // both constructors keep their original allocation route.
                admitted=false;
                return menu_call_sret4(color_original,original_args,6);
            }
        } else if(driver!=supplied_driver || !lease.value()) trap("pair_depth_identity");
        const uint32_t args[]={output,tm,raw(physical),raw(keys[scope.slot].data()),format,option};
        const auto result=menu_call_sret4(color_original,args,6);
        if(!word(output)) {
            // Scaled refusal is not native refusal. Drop only the factory lease,
            // then retry the unchanged original name/dimensions/format/option.
            // An already published slot0 keeps its native target-owned depth ref;
            // this slot and remaining slots are wholly Native (including viewport).
            finish();
            const auto fallback=menu_call_sret4(color_original,original_args,6);
            if(!word(output)) trap("native_color_factory_null");
            return fallback;
        }
        return result;
    }
    static uint64_t __cdecl depth(uint32_t target,uint32_t attachment,uint32_t reference) noexcept {
        const auto caller=raw(_ReturnAddress());
        try {
            auto& s=*installed; uint32_t supplied=reference;
            bool expected_caller=caller==s.depth_return;
#ifdef NOVA_INITIAL_TARGET_SCALING_FIXTURE
            expected_caller=expected_caller || s.fixture;
#endif
            if(s.scoped() && expected_caller) {
                auto& scope=*current;
                if(scope.depth || attachment!=1 || reference!=s.global_depth ||
                   target!=word(scope.receiver+8) || !s.lease.value()) trap("depth_scope_contract");
                scope.depth=true; supplied=raw(s.lease.reference());
            }
            const uint32_t args[]={target,attachment,supplied};
            // Preserve all result bits, including original false. Native attachment
            // owns a reference ONLY on success; no host reference increment.
            return abi::call_guest_u64(s.depth_original,args,3);
        } catch(...) { if(installed) installed->finish(); trap("depth_callback"); }
    }
    static uint64_t __cdecl push(uint32_t app,uint32_t target_reference) noexcept {
        try {
            auto& s=*installed;
            if(s.scoped()) {
                auto& scope=*current;
                // C1 stores g_sceneManager at +4 (2b048a..2b04a7). The clone
                // separately calls Application::GetInstance (2a46b5). These are
                // distinct owners: never compare the scene pointer with Application.
                const uint32_t scene=word(s.scene_manager_global);
                const uint32_t application=word(s.application_global);
                if(!scope.color || !scope.depth || scope.viewport || target_reference!=scope.receiver+8 ||
                   !scene || word(scope.receiver+4)!=scene || !application || app!=application)
                    trap("viewport_scope_contract");
                const uint32_t p=scope.receiver;
                if(word(p+0x40)||word(p+0x44)||word(p+0x48)!=960||word(p+0x4c)!=640 ||
                   word(p+0x70)!=0x3f700000 || word(p+0x74)!=0x3f200000)
                    trap("viewport_or_uv_contract");
                // Projection, clipping and scale getters all consume this logical
                // viewport. Increase only GPU rasterization, never native layout.
                ui::MenuSurfaces::publish({s.manager,s.generation,p,word(p+8),s.driver,s.plan.factor,960,640,100});
                scope.viewport=true;
            }
            const uint32_t args[]={app,target_reference};
            return abi::call_guest_u64(s.push_original,args,2);
        } catch(...) { if(installed) installed->finish(); trap("push_callback"); }
    }
};
InitialTargetScaling::Impl* InitialTargetScaling::Impl::installed=nullptr;
std::atomic<uint32_t> InitialTargetScaling::Impl::next_install{0};
thread_local InitialTargetScaling::Impl::Scope* InitialTargetScaling::Impl::current=nullptr;
extern "C" uint64_t __cdecl menu_color_body(uint32_t out,uint32_t tm,uint32_t dims,uint32_t name,uint32_t format,uint32_t option) noexcept {
    try { return InitialTargetScaling::Impl::installed->color_factory(out,tm,dims,name,format,option); }
    catch(...) { if(InitialTargetScaling::Impl::installed) InitialTargetScaling::Impl::installed->finish(); trap("color_callback"); }
}
InitialTargetScaling::Impl::Impl(const ElfImage& image):lease(this,&release_lease) {
    if(installed) throw std::runtime_error("initial-menu scaling already installed");
    install=++next_install; if(!install) throw std::runtime_error("initial-menu cache epoch overflow");
    depth_factory=image.address(0x914250); release=image.address(0xe1850);
    depth_return=image.address(0x2b075b); global_depth=image.address(0x1156124,4);
    scene_manager_global=image.address(0x1156838,4); // g_sceneManager, C1 GOT 113d630.
    application_global=image.address(0x115c08c,4); // GetInstance local static app.
    // Exact preflight for ALL new seams precedes the first write.
    const auto at=image.address(0x2b063e,5);
    const uint8_t expected[]={0xe8,0x7d,0x2d,0x6d,0};
    const uint8_t push_expected[]={0xe8,0x67,0xeb,0x40,0};
    const uint8_t attach_expected[]={0xff,0x50,0x20};
    const uint8_t depth_entry[]={0x55,0x89,0xe5,0x83,0xec,0x28};
    const uint8_t instance_load[]={0x8b,0x83,0x4c,0xdd,0x01,0x00};
    if(std::memcmp(reinterpret_cast<void*>(at),expected,5) ||
       std::memcmp(reinterpret_cast<void*>(image.address(0x2a46c4,5)),push_expected,5) ||
       std::memcmp(reinterpret_cast<void*>(image.address(0x2b0758,3)),attach_expected,3) ||
       std::memcmp(reinterpret_cast<void*>(image.address(0x8d5310,6)),depth_entry,6) ||
        std::memcmp(reinterpret_cast<void*>(image.address(0x6b6e22,6)),instance_load,6))
        throw std::runtime_error("initial-menu scaling seam mismatch");
    if(abi::require_function(image,"_ZN11Application20SafePushRenderTargetERKN5boost13intrusive_ptrIN6glitch5video13IRenderTargetEEE")!=image.address(0x6b3230)) throw std::runtime_error("initial-menu symbol pin mismatch");
    if(abi::require_function(image,"_ZN6glitch5video15CTextureManager10addTextureERKNS_4core11dimension2dIiEEPKcNS0_14E_PIXEL_FORMATEb")!=image.address(0x9833c0)) throw std::runtime_error("initial-menu symbol pin mismatch");
    if(abi::require_function(image,"_ZN6glitch5video19CCommonGLDriverBase17CRenderTargetBase9addTargetENS0_13IRenderTarget17E_ATTACHMENT_TYPEERKN5boost13intrusive_ptrINS0_13IRenderBufferEEE")!=image.address(0x8d5310)) throw std::runtime_error("initial-menu symbol pin mismatch");
    if(abi::require_function(image,"_ZN6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE18createRenderBufferERKNS_4core11dimension2dIiEENS0_14E_PIXEL_FORMATE")!=image.address(0x914250)) throw std::runtime_error("initial-menu symbol pin mismatch");
    if(abi::require_function(image,"_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE")!=image.address(0xe1850)) throw std::runtime_error("initial-menu symbol pin mismatch");
    if(abi::require_function(image,"_ZN11Application11GetInstanceEv")!=image.address(0x6b6e10) ||
       image.symbol("g_sceneManager")!=scene_manager_global)
        throw std::runtime_error("initial-menu owner pin mismatch");
    color_call=std::make_unique<abi::GuestCall>(image,rtt,0x1de,color,raw(reinterpret_cast<void*>(&menu_color_entry)));
    push_call=std::make_unique<abi::GuestCall>(image,clone,0x24,"_ZN11Application20SafePushRenderTargetERKN5boost13intrusive_ptrIN6glitch5video13IRenderTargetEEE",raw(reinterpret_cast<void*>(&push)));
    depth_hook=std::make_unique<abi::GuestHook>(image,"_ZN6glitch5video19CCommonGLDriverBase17CRenderTargetBase9addTargetENS0_13IRenderTarget17E_ATTACHMENT_TYPEERKN5boost13intrusive_ptrINS0_13IRenderBufferEEE",std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x28},raw(reinterpret_cast<void*>(&depth)));
    color_original=color_call->original(); depth_original=depth_hook->original(); push_original=push_call->original();
    installed=this;
}
InitialTargetScaling::InitialTargetScaling(const ElfImage& image):impl_(std::make_unique<Impl>(image)) {}
#ifdef NOVA_INITIAL_TARGET_SCALING_FIXTURE
InitialTargetScaling::InitialTargetScaling(FixtureBindings b):impl_(std::make_unique<Impl>(b)) {}
uint32_t InitialTargetScaling::fixture_color_entry() noexcept { return raw(reinterpret_cast<void*>(&menu_color_entry)); }
uint64_t InitialTargetScaling::fixture_depth(uint32_t t,uint32_t a,uint32_t r) noexcept { return Impl::depth(t,a,r); }
uint64_t InitialTargetScaling::fixture_push(uint32_t a,uint32_t r) noexcept { return Impl::push(a,r); }
#endif
InitialTargetScaling::~InitialTargetScaling() {
    if(impl_->lease.value()) trap("lease_survived_phase16_cleanup");
    ui::MenuSurfaces::retire(impl_->manager,impl_->generation);
    impl_->depth_hook.reset(); impl_->push_call.reset(); impl_->color_call.reset(); Impl::installed=nullptr;
}
void InitialTargetScaling::configure(platform::GuestExtent extent,platform::RenderLimits limits) noexcept {
    auto& s=*impl_; if(s.configured || s.used) return;
    s.plan=initial_target_plan(extent.width,extent.height,limits);
    s.thread=GetCurrentThreadId(); s.configured=true;
    s.owner_lane.store(s.thread,std::memory_order_release);
}
uint64_t InitialTargetScaling::native_constructor(uint32_t receiver,uint32_t width,uint32_t height,uint32_t original) {
    const uint32_t args[]={receiver,width,height};
    return impl_->native_constructor(original,args);
}
uint64_t InitialTargetScaling::construct(uint32_t manager,uint32_t generation,unsigned slot,uint32_t receiver,uint32_t width,uint32_t height,uint32_t original) {
    auto& s=*impl_; const uint32_t args[]={receiver,width,height};
    // The atomic lane check precedes every plain render-owned state read.
    if(s.owner_lane.load(std::memory_order_acquire)!=GetCurrentThreadId() || Impl::current)
        return s.native_constructor(original,args);
    if(s.revocation_pending()) s.finish();
    if(!s.configured || !s.plan.scaled || width!=960 || height!=640 || slot>1)
        return s.native_constructor(original,args);
    if(slot==0) {
        if(s.used) return s.native_constructor(original,args);
        s.used=true; s.manager=manager; s.generation=generation; s.slots=0; s.admitted=true;
        s.revoked.store(0,std::memory_order_relaxed);
        s.identity.store(Impl::token(manager,generation),std::memory_order_release);
        for(unsigned i=0;i<2;++i) {
            const int n=std::snprintf(s.keys[i].data(),s.keys[i].size(),"nova_pc_t09j_%08x_%08x_%08x_%u_%u",s.install,manager,generation,s.plan.side,i);
            if(n<0 || size_t(n)>=s.keys[i].size()) trap("cache_key_bound");
        }
    }
    if(!s.admitted || s.manager!=manager || s.generation!=generation || s.slots!=slot)
        return s.native_constructor(original,args);
    s.creation.created(slot,manager,generation,0,receiver,s.thread,width,height);
    RttBackingCapture::Scope capture(s.creation,receiver,s.thread,0);
    Impl::Scope scope{&s,receiver,slot}; Impl::current=&scope;
    try {
        const auto result=abi::call_guest_u64(original,args,3);
        Impl::current=nullptr; capture.returned(result);
        if(s.admitted && (!scope.color || !scope.depth || !scope.viewport)) { s.finish(); trap("incomplete_constructor_seams"); }
        ++s.slots; if(slot==1 || s.revocation_pending()) s.finish(); return result;
    } catch(...) { Impl::current=nullptr; s.finish(); throw; }
}
void InitialTargetScaling::report(FILE* file) const {
    const auto snapshot=impl_->creation.snapshot();
    for(unsigned slot=0;slot<2;++slot) {
        const auto& t=snapshot.targets[slot];
        std::fprintf(file,"initial-menu-creation slot=%u receiver=%08x returned=%u invalid=%u events=%u scope=C1\n",slot,t.receiver,unsigned(t.returned),unsigned(t.invalid),t.count);
        for(unsigned i=0;i<t.count;++i) {
            const auto& e=t.events[i];
            std::fprintf(file,"initial-menu-creation slot=%u event=%u op=%u result=%08x words=",slot,i,unsigned(e.op),e.result);
            for(auto word:e.words) std::fprintf(file,"%08x,",word);
            std::fputc('\n',file);
        }
    }
}
void InitialTargetScaling::lifecycle(uint32_t manager,uint32_t generation) noexcept {
    ui::MenuSurfaces::retire(manager,generation);
    auto& s=*impl_;
    const auto expected=Impl::token(manager,generation);
    if(!manager || !generation || s.identity.load(std::memory_order_acquire)!=expected) return;
    s.revoked.store(expected,std::memory_order_release);
    // An in-flight original C1 retains its factory lease through return. An
    // unrelated or foreign callback never touches plain state or guest ownership.
    if(s.owner_lane.load(std::memory_order_acquire)==GetCurrentThreadId() && !Impl::current) s.finish();
}
void InitialTargetScaling::finish() noexcept {
    if(Impl::current && Impl::current->owner==impl_.get()) { impl_->request_revocation(); return; }
    impl_->finish();
}
}
