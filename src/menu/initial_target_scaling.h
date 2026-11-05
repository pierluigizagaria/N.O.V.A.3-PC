#pragma once
#include "loader/elf_image.h"
#include "platform/render_catalogue.h"
#include "menu/rtt_backing_capture.h"
#include <array>
#include <cstdint>
#include <memory>
namespace nova::menu {
struct InitialTargetPlan {
    uint32_t factor=1,side=1024,width=960,height=640;
    bool scaled=false;
};
// Uniform power-of-two density covers the ACTUAL selected extent while keeping
// native 3:2 RTT aspect and exactly representable original UVs. At1920x1080:
// side2048, viewport1920x1280. No claim that source artwork gains detail.
inline InitialTargetPlan initial_target_plan(int w,int h,platform::RenderLimits cap) noexcept {
    InitialTargetPlan p;
    if(w<=0 || h<=0 || w>3840 || h>2560) return p;
    uint32_t f=1;
    while(f<4 && (uint32_t(w)>960*f || uint32_t(h)>640*f)) f*=2;
    if(f==1 || !cap.storage(int(1024*f),int(1024*f))) return p;
    p.factor=f; p.side=1024*f; p.width=960*f; p.height=640*f; p.scaled=true; return p;
}
// Native factory owns the returned lease. Attachment acquires its own reference;
// this lease never borrows the game's global shared depth.
class InitialDepthLease {
public:
    using Release=void(*)(void*,uint32_t) noexcept;
    InitialDepthLease(void* context,Release release):context_(context),release_(release) {}
    ~InitialDepthLease() { reset(); }
    InitialDepthLease(const InitialDepthLease&)=delete;
    void adopt(uint32_t owned) noexcept { reset(); value_=owned; }
    void reset() noexcept { auto v=value_; value_=0; if(v) release_(context_,v); }
    uint32_t value() const noexcept { return value_; }
    const uint32_t* reference() const noexcept { return &value_; }
private: void* context_; Release release_; uint32_t value_=0;
};
class InitialTargetScaling {
public:
    // Caller authenticates exact image before installation. No guest producer may
    // run until ALL observer/scaler hooks are installed; quiesce before removal.
    explicit InitialTargetScaling(const ElfImage&);
    ~InitialTargetScaling();
    void configure(platform::GuestExtent actual,platform::RenderLimits limits) noexcept;
    void report(FILE*) const;
    uint64_t construct(uint32_t manager,uint32_t generation,unsigned slot,
                       uint32_t receiver,uint32_t width,uint32_t height,uint32_t original);
    // Matched lifecycle only; foreign lanes publish scalar revocation.
    void lifecycle(uint32_t manager,uint32_t generation) noexcept;
    uint64_t native_constructor(uint32_t receiver,uint32_t width,uint32_t height,uint32_t original);
    void finish() noexcept; // Owner/phase16 boundary releases; foreign lane only revokes.
#ifdef NOVA_INITIAL_TARGET_SCALING_FIXTURE
    struct FixtureBindings { uint32_t color,depth_factory,attach,push,release,global_depth,scene_manager_global,application_global; };
    explicit InitialTargetScaling(FixtureBindings);
    static uint32_t fixture_color_entry() noexcept;
    static uint64_t fixture_depth(uint32_t,uint32_t,uint32_t) noexcept;
    static uint64_t fixture_push(uint32_t,uint32_t) noexcept;
#endif
public: struct Impl; // Opaque implementation used by the fixed ABI landing.
private: std::unique_ptr<Impl> impl_;
};
// Explicit SysV i386 hidden-result pointer/RET4 bridge. Caller supplies ALL words,
// including the output reference first. This is deliberately separate from the
// scalar-only public call_guest contract. EDX:EAX and host callee saves survive.
extern "C" uint64_t __cdecl menu_call_sret4(uint32_t,const uint32_t*,uint32_t);
}
