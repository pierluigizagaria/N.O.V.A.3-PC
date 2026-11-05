#include "ui/native_scale.h"
#include "ui/menu_surfaces.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace nova::ui {
namespace {
uint32_t address(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t word(uint32_t p) { uint32_t v; std::memcpy(&v,reinterpret_cast<void*>(p),4); return v; }
[[noreturn]] void failure() noexcept {
    std::fputs("native-ui-scale FAIL boundary=menu_density\n",stderr); std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(),86); std::abort();
}
constexpr auto viewport_name="_ZN6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE15setViewportImplERKNS_4core4rectIiEE";
constexpr auto screen_name="_ZN11CMenuScreen6RenderEv";
constexpr auto pause_name="_ZN13GS_InGameMenu6RenderEv";
constexpr auto manager_name="_ZN12CMenuManager6RenderEv";
}
struct NativeScale::Impl {
    static Impl* installed;
    std::array<std::unique_ptr<abi::GuestHook>,4> hooks;
    // Fixture slots1/3 forward projection/hit; neither is intercepted in play.
    std::array<uint32_t,6> originals{};
    explicit Impl(const ElfImage& image) {
        if(installed) throw std::runtime_error("UI scale already installed");
        const std::array<const char*,4> names{viewport_name,screen_name,pause_name,manager_name};
        const std::array<uint32_t,4> offsets{0x926690,0x546800,0x492440,0x53d6f0};
        const std::array<uint32_t,4> entries{address(reinterpret_cast<void*>(&viewport_entry)),address(reinterpret_cast<void*>(&screen_entry)),address(reinterpret_cast<void*>(&pause_entry)),address(reinterpret_cast<void*>(&manager_entry))};
        constexpr std::array<unsigned,4> slots{0,2,4,5};
        for(size_t i=0;i<names.size();++i) {
            if(abi::require_function(image,names[i])!=image.address(offsets[i]))
                throw std::runtime_error("Menu density function identity mismatch");
            if(!i) hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x78},entries[i]);
            else hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},entries[i]);
            originals[slots[i]]=hooks[i]->original();
        }
        installed=this;
    }
#ifdef NOVA_UI_SCALE_FIXTURE
    explicit Impl(const std::array<uint32_t,6>& functions):originals(functions) {
        if(installed) throw std::runtime_error("UI scale already installed"); installed=this;
    }
#endif
    ~Impl() { for(auto& hook:hooks) hook.reset(); installed=nullptr; }
    static uint32_t top(uint32_t driver) {
        const auto begin=word(driver+0x114),end=word(driver+0x118);
        if(!begin || end<=begin || end-begin>4096 || (end-begin)%4) failure();
        return word(end-4);
    }
    static uint64_t __cdecl viewport_entry(uint32_t driver,uint32_t rectangle) noexcept {
        try {
            const auto surface=MenuSurfaces::target(driver,top(driver));
            const auto width=word(rectangle+8)-word(rectangle),height=word(rectangle+12)-word(rectangle+4);
            // Increase physical density only; never multiply the allocation bind.
            const unsigned factor=surface.receiver && width && height &&
                word(rectangle+8)<=static_cast<unsigned>(surface.width) &&
                word(rectangle+12)<=static_cast<unsigned>(surface.height) &&
                width<=static_cast<unsigned>(surface.width) && height<=static_cast<unsigned>(surface.height) ? surface.density : 1;
            if(factor!=1 && factor!=2 && factor!=4) failure();
            RasterScope raster(factor);
            const uint32_t args[]{driver,rectangle};
            return abi::call_guest_u64(installed->originals[0],args,2);
        } catch(...) { failure(); }
    }
    static uint64_t menu_entry(uint32_t receiver,unsigned slot) noexcept {
        try {
            MenuDrawScope scope;
            const uint32_t args[]{receiver};
            // No projection edits, dirty-counter edits or hit-coordinate edits.
            return abi::call_guest_u64(installed->originals[slot],args,1);
        } catch(...) { failure(); }
    }
    static uint64_t __cdecl screen_entry(uint32_t r) noexcept { return menu_entry(r,2); }
    static uint64_t __cdecl pause_entry(uint32_t r) noexcept { return menu_entry(r,4); }
    static uint64_t __cdecl manager_entry(uint32_t r) noexcept { return menu_entry(r,5); }
};
NativeScale::Impl* NativeScale::Impl::installed=nullptr;
NativeScale::NativeScale(const ElfImage& i,RequestedScale&):impl_(std::make_unique<Impl>(i)) {}
NativeScale::~NativeScale()=default;
#ifdef NOVA_UI_SCALE_FIXTURE
NativeScale::NativeScale(RequestedScale&,const std::array<uint32_t,6>& f,uint32_t):impl_(std::make_unique<Impl>(f)) {}
uint64_t NativeScale::fixture_viewport(uint32_t d,uint32_t r) { return Impl::viewport_entry(d,r); }
uint64_t NativeScale::fixture_projection(uint32_t d) {
    const uint32_t args[]{d}; return abi::call_guest_u64(Impl::installed->originals[1],args,1);
}
uint64_t NativeScale::fixture_screen(uint32_t s) { return Impl::screen_entry(s); }
uint64_t NativeScale::fixture_hit(uint32_t s,uint32_t x,uint32_t y,uint32_t ox,uint32_t oy) {
    const uint32_t args[]{s,x,y,ox,oy}; return abi::call_guest_u64(Impl::installed->originals[3],args,5);
}
uint64_t NativeScale::fixture_pause(uint32_t s) { return Impl::pause_entry(s); }
uint64_t NativeScale::fixture_manager(uint32_t s) { return Impl::manager_entry(s); }
#endif
}
