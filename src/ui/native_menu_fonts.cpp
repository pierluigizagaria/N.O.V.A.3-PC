#include "ui/native_menu_fonts.h"
#include "ui/scale.h"
#include "ui/menu_text_trace.h"
#include "ui/menu_surfaces.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

namespace nova::ui {
namespace {
uint32_t address(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t word(uint32_t p) { uint32_t v; std::memcpy(&v,reinterpret_cast<void*>(p),4); return v; }
[[noreturn]] void failed() noexcept {
    std::fputs("native-menu-fonts FAIL boundary=glyph_density\n",stderr); std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(),86); std::abort();
}
constexpr std::array<const char*,7> names{
    "_ZN9CTTFGlyph5cacheEjRKN5boost13intrusive_ptrI8CTTFFaceEEPN6glitch5video12IVideoDriverEP17GlyphAtlasManagerjjNS7_6SColorEjb",
    "_ZN8CTTFFont9drawGlyphEPK9CTTFGlyphRKN6glitch4core8vector2dIiEEPKNS4_4rectIiEENS3_5video6SColorEbf",
    "_ZN8CTTFFont21drawGlyphSmoothShadedEPK9CTTFGlyphRKN6glitch4core8vector2dIiEEPKNS4_4rectIiEEPNS3_5video6SColorEbf",
    "_ZN6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE15draw2DRectangleERKNS_4core4rectIiEESE_PKNS0_6SColorEPSD_",
    "_ZN9CTTFGlyphD1Ev","_ZN9CTTFGlyphD2Ev","_ZN17GlyphAtlasManager5ClearEv"};
constexpr std::array<uint32_t,7> offsets{0x3f1d60,0x3e8b80,0x3e8610,0x9374f0,0x3e94f0,0x3e9520,0x3eb220};
}
struct NativeMenuFonts::Impl {
    static Impl* installed;
    using Glyph=std::array<uint32_t,14>;
    struct Extra { Glyph pixels{}; uint32_t atlas=0; };
    struct Drawing { uint32_t driver; std::array<uint32_t,4> low,high; Drawing* previous; };
    static thread_local Drawing* drawing;
    std::array<std::unique_ptr<abi::GuestHook>,7> hooks;
    std::array<uint32_t,7> originals{};
    uint32_t construct=0,set_size=0,load=0;
    std::map<uint32_t,Extra> extras;
    std::recursive_mutex mutex;
    explicit Impl(const ElfImage& image) {
        if(installed) throw std::runtime_error("Menu fonts already installed");
        const std::array<uint32_t,7> entries{address(reinterpret_cast<void*>(&cache)),address(reinterpret_cast<void*>(&draw)),
            address(reinterpret_cast<void*>(&smooth)),address(reinterpret_cast<void*>(&rectangle)),
            address(reinterpret_cast<void*>(&destroy)),address(reinterpret_cast<void*>(&destroy2)),address(reinterpret_cast<void*>(&clear))};
        for(size_t i=0;i<names.size();++i) {
            if(abi::require_function(image,names[i])!=image.address(offsets[i])) throw std::runtime_error("Menu font function identity mismatch");
            if(i==1 || i==2) hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},entries[i]);
            else if(i==4 || i==5) hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x56,0x83,0xec,0x14},entries[i]);
            else hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},entries[i]);
            originals[i]=hooks[i]->original();
        }
        construct=abi::require_function(image,"_ZN9CTTFGlyphC1Ev");
        set_size=abi::require_function(image,"FT_Set_Pixel_Sizes");
        load=abi::require_function(image,"FT_Load_Glyph");
        installed=this;
    }
#ifdef NOVA_MENU_FONT_FIXTURE
    explicit Impl(Bindings b):originals(b.functions),construct(b.construct),set_size(b.set_size),load(b.load) {
        if(installed) throw std::runtime_error("Menu fonts already installed"); installed=this;
    }
#endif
    ~Impl() {
        for(auto& item:extras) abi::invoke(originals[4],{address(item.second.pixels.data())});
        extras.clear();
        for(auto& hook:hooks) hook.reset();
        installed=nullptr;
    }
    void erase(uint32_t glyph) {
        const auto it=extras.find(glyph);
        if(it==extras.end()) return;
        abi::invoke(originals[4],{address(it->second.pixels.data())}); extras.erase(it);
    }
    static uint64_t __cdecl cache(uint32_t g,uint32_t index,uint32_t face,uint32_t driver,uint32_t atlas,
                                 uint32_t size,uint32_t border,uint32_t color,uint32_t extra,uint32_t bold) noexcept {
        try {
            auto& self=*installed; std::lock_guard lock(self.mutex); self.erase(g);
            const uint32_t args[]{g,index,face,driver,atlas,size,border,color,extra,bold};
            const auto result=abi::call_guest_u64(self.originals[0],args,10);
            // Retain native bitmap fallback and bound supplemental atlas usage.
            // Provisional fixed 2x density avoids tying resource lifetime to resize.
            // extra is FT_Bitmap_Embolden strength in 26.6 units, NOT pixels.
            // Native setBorder stores size * outline coefficient (typically 7..11)
            // here, including on normal glyph caches after an outlined label.
            constexpr uint32_t max_outline_26_6=32*64;
            if(!word(g+0x24) || word(g+0x20) || !size || size>128 || border>8 || extra>max_outline_26_6 || self.extras.size()>=4096)
                return result;
            const auto width=word(g+0x30)-word(g+0x28),height=word(g+0x34)-word(g+0x2c);
            if(!width || !height || width>128 || height>128) return result;
            Extra high; high.atlas=atlas;
            abi::invoke(self.construct,{address(high.pixels.data())});
            abi::invoke(self.originals[0],{address(high.pixels.data()),index,face,driver,atlas,size*2,border*2,color,extra*2,bold});
            // cache() finishes by loading the native monochrome fallback slot.
            // Restore the shared FreeType face, including its active glyph slot.
            const auto ft_face=word(word(face)+8);
            if(abi::invoke(self.set_size,{ft_face,0,size}) || abi::invoke(self.load,{ft_face,index,0x100e})) failed();
            if(high.pixels[9] && !high.pixels[8] && high.pixels[12]>high.pixels[10] && high.pixels[13]>high.pixels[11])
                self.extras.emplace(g,high);
            else abi::invoke(self.originals[4],{address(high.pixels.data())});
            return result;
        } catch(...) { failed(); }
    }
    static uint64_t render(unsigned slot,uint32_t f,uint32_t g,uint32_t pos,uint32_t clip,uint32_t color,uint32_t fade,uint32_t scale) {
        auto& self=*installed;
        std::lock_guard lock(self.mutex);
        const uint32_t args[]{f,g,pos,clip,color,fade,scale};
        const auto it=self.extras.find(g);
        if(MenuTextTrace::active()) {
            MenuTextTrace::Glyph sample;
            sample.font=f; sample.size=word(f+0x54); sample.path=slot;
            sample.outline_26_6=word(f+0x60);
            sample.low_width=word(g+0x30)-word(g+0x28); sample.low_height=word(g+0x34)-word(g+0x2c);
            sample.available=it!=self.extras.end(); sample.monochrome=word(g+0x20)!=0;
            sample.selected=menu_draw_depth && sample.available && !sample.monochrome;
            if(sample.available) {
                const auto& high=it->second.pixels;
                sample.high_width=high[12]-high[10]; sample.high_height=high[13]-high[11];
            }
            const auto driver=word(f+8),begin=word(driver+0x114),end=word(driver+0x118);
            if(begin && end>begin && end-begin<=4096 && (end-begin)%4==0) {
                const auto target=word(end-4);
                sample.target_width=word(target+0x1c)-word(target+0x14);
                sample.target_height=word(target+0x20)-word(target+0x18);
                sample.target_density=MenuSurfaces::target(driver,target).density;
            }
            MenuTextTrace::glyph(sample);
        }
        if(!menu_draw_depth || it==self.extras.end() || word(g+0x20)) {
            auto* previous=drawing; drawing=nullptr;
            const auto answer=abi::call_guest_u64(self.originals[slot],args,7);
            drawing=previous; return answer;
        }
        Glyph logical; std::memcpy(logical.data(),reinterpret_cast<void*>(g),sizeof logical);
        const auto& high=it->second.pixels;
        // Borrow only the texture reference. Native bearings, atlas rectangle
        // dimensions and all measurements stay at the original logical density.
        logical[9]=high[9];
        Drawing context{word(f+8),{logical[10],logical[11],logical[12],logical[13]},
            {high[10],high[11],high[12],high[13]},drawing};
        drawing=&context;
        const uint32_t dense_args[]{f,address(logical.data()),pos,clip,color,fade,scale};
        const auto answer=abi::call_guest_u64(self.originals[slot],dense_args,7);
        drawing=context.previous; return answer;
    }
    static uint64_t __cdecl draw(uint32_t f,uint32_t g,uint32_t p,uint32_t c,uint32_t color,uint32_t fade,uint32_t scale) noexcept {
        try { return render(1,f,g,p,c,color,fade,scale); } catch(...) { failed(); }
    }
    static uint64_t __cdecl smooth(uint32_t f,uint32_t g,uint32_t p,uint32_t c,uint32_t colors,uint32_t fade,uint32_t scale) noexcept {
        try { return render(2,f,g,p,c,colors,fade,scale); } catch(...) { failed(); }
    }
    static uint64_t __cdecl rectangle(uint32_t driver,uint32_t dest,uint32_t source,uint32_t colors,uint32_t clip) noexcept {
        try {
            if(drawing && drawing->driver==driver && !std::memcmp(reinterpret_cast<void*>(source),drawing->low.data(),16)) {
                source=address(drawing->high.data());
                MenuTextTrace::rectangle(word(dest+8)-word(dest),word(dest+12)-word(dest+4));
            }
            const uint32_t args[]{driver,dest,source,colors,clip};
            return abi::call_guest_u64(installed->originals[3],args,5);
        } catch(...) { failed(); }
    }
    static uint64_t destruct(uint32_t g,unsigned slot) {
        std::lock_guard lock(installed->mutex);
        installed->erase(g); const uint32_t args[]{g}; return abi::call_guest_u64(installed->originals[slot],args,1);
    }
    static uint64_t __cdecl destroy(uint32_t g) noexcept { try { return destruct(g,4); } catch(...) { failed(); } }
    static uint64_t __cdecl destroy2(uint32_t g) noexcept { try { return destruct(g,5); } catch(...) { failed(); } }
    static uint64_t __cdecl clear(uint32_t atlas) noexcept {
        try {
            auto& self=*installed;
            std::lock_guard lock(self.mutex);
            for(auto it=self.extras.begin();it!=self.extras.end();) {
                if(it->second.atlas==atlas) { abi::invoke(self.originals[4],{address(it->second.pixels.data())}); it=self.extras.erase(it); }
                else ++it;
            }
            const uint32_t args[]{atlas}; return abi::call_guest_u64(self.originals[6],args,1);
        } catch(...) { failed(); }
    }
};
NativeMenuFonts::Impl* NativeMenuFonts::Impl::installed=nullptr;
thread_local NativeMenuFonts::Impl::Drawing* NativeMenuFonts::Impl::drawing=nullptr;
NativeMenuFonts::NativeMenuFonts(const ElfImage& image):impl_(std::make_unique<Impl>(image)) {}
NativeMenuFonts::~NativeMenuFonts()=default;
#ifdef NOVA_MENU_FONT_FIXTURE
NativeMenuFonts::NativeMenuFonts(Bindings b):impl_(std::make_unique<Impl>(b)) {}
uint64_t NativeMenuFonts::fixture_cache(const std::array<uint32_t,10>& a) { return Impl::cache(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9]); }
uint64_t NativeMenuFonts::fixture_draw(const std::array<uint32_t,7>& a,bool smooth) { return Impl::render(smooth?2:1,a[0],a[1],a[2],a[3],a[4],a[5],a[6]); }
uint64_t NativeMenuFonts::fixture_rectangle(const std::array<uint32_t,5>& a) { return Impl::rectangle(a[0],a[1],a[2],a[3],a[4]); }
uint64_t NativeMenuFonts::fixture_destroy(uint32_t g,bool alt) { return Impl::destruct(g,alt?5:4); }
uint64_t NativeMenuFonts::fixture_clear(uint32_t atlas) { return Impl::clear(atlas); }
#endif
}
