#include "ui/native_pixel_resources.h"
#include "abi/guest_call.h"
#include "abi/guest_hook.h"
#include "menu/native_menu_observer.h"
#include <windows.h>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace nova::ui {
namespace {
uint32_t address(const void* p){return reinterpret_cast<uint32_t>(p);}
uint32_t& word(uint32_t p){return *reinterpret_cast<uint32_t*>(p);}
constexpr auto selector="_ZN14CSpriteManager17GetSpriteFilenameEPKcRSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS4_6memory13E_MEMORY_HINTE0EEEERfRiSD_b";
}
struct NativePixelResources::Impl {
    static Impl* active;
    struct Loading { unsigned width,height;uint8_t profile;Loading* previous; };
    static thread_local Loading* loading;
    uint32_t width=0,height=0,profile=0,selector_count=0,original=0;
    std::recursive_mutex mutex;
    std::unique_ptr<abi::GuestHook> hook;
    explicit Impl(const ElfImage& image) {
        if(active || abi::require_function(image,selector)!=image.address(0x3fd6a0))throw std::runtime_error("GUI sprite selector identity mismatch");
        width=image.symbol("g_ScreenW");height=image.symbol("g_ScreenH");profile=image.symbol("isCheckDataGoogleDeal");
        selector_count=image.address(0x115679c,4);
        hook=std::make_unique<abi::GuestHook>(image,selector,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},address(reinterpret_cast<void*>(&select)));
        original=hook->original();active=this;
    }
    ~Impl(){hook.reset();active=nullptr;}
    static uint64_t __cdecl select(uint32_t manager,uint32_t name,uint32_t output,uint32_t scale,uint32_t w,uint32_t h,uint32_t cover) noexcept {
        try {
            auto& s=*active;const uint32_t args[]{manager,name,output,scale,w,h,cover};
            if(!loading)return abi::call_guest_u64(s.original,args,7);
            // Preserve the original actual-display device-selection side effect.
            // Only the returned logical descriptor/metrics use the reference.
            word(s.width)=loading->width;word(s.height)=loading->height;
            *reinterpret_cast<uint8_t*>(s.profile)=loading->profile;
            abi::call_guest_u64(s.original,args,7);
            loading->profile=*reinterpret_cast<uint8_t*>(s.profile);
            const auto actual_count=word(s.selector_count);
            word(s.width)=1280;word(s.height)=720;
            const auto result=abi::call_guest_u64(s.original,args,7);
            *reinterpret_cast<uint8_t*>(s.profile)=loading->profile;
            word(s.selector_count)=actual_count;
            return result;
        }catch(...){std::fputs("GUI logical resource boundary failed\n",stderr);TerminateProcess(GetCurrentProcess(),86);std::abort();}
    }
};
NativePixelResources::Impl* NativePixelResources::Impl::active=nullptr;
thread_local NativePixelResources::Impl::Loading* NativePixelResources::Impl::loading=nullptr;
NativePixelResources::NativePixelResources(const ElfImage& image):impl_(std::make_unique<Impl>(image)){}
NativePixelResources::~NativePixelResources()=default;
uint32_t NativePixelResources::load(uint32_t original,const uint32_t* args) {
    auto* s=Impl::active;
    // A shared menu/font must never enter the fixed HUD resource cache merely
    // because it was first loaded outside CMenuManager::Load. Coverage and
    // weapon-specific reticles also retain their original native metrics.
    const auto name=reinterpret_cast<const char*>(args[1]);
    const bool hud=name && (!_stricmp(name,"hud3d.bsprite") || !_stricmp(name,"controls.bsprite"));
    if(!s || !hud || (args[2]&255) || (args[4]&255) || Impl::loading || menu::NativeMenuObserver::loading() || !word(s->height))return abi::call_guest(original,args,6);
    std::lock_guard guard(s->mutex);
    Impl::Loading scope{word(s->width),word(s->height),*reinterpret_cast<uint8_t*>(s->profile),Impl::loading};
    if(scope.width==1280 && scope.height==720)return abi::call_guest(original,args,6);
    struct Restore {
        Impl& s;Impl::Loading& scope;
        ~Restore(){word(s.width)=scope.width;word(s.height)=scope.height;*reinterpret_cast<uint8_t*>(s.profile)=scope.profile;Impl::loading=scope.previous;}
    } restore{*s,scope};
    Impl::loading=&scope;word(s->width)=1280;word(s->height)=720;
    return abi::call_guest(original,args,6);
}
}
