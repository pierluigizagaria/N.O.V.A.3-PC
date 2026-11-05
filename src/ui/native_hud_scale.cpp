#include "ui/native_hud_scale.h"
#include "ui/native_artwork.h"
#include "ui/menu_text_trace.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <intrin.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace nova::ui {
namespace {
uint32_t address(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t word(uint32_t p) { uint32_t v; std::memcpy(&v,reinterpret_cast<void*>(p),4); return v; }
[[noreturn]] void failed() noexcept {
    std::fputs("native-ui-scale FAIL boundary=hud\n",stderr); std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(),86); std::abort();
}
constexpr std::array<const char*,10> names{
    "_ZN6CLevel8Render2DEv","_ZNK7CSprite10PaintFrameEiffjffh",
    "_ZNK7CSprite17PaintFrameRotatedEifffjffh",
    "_ZN11CSpriteFont10DrawStringEPKciiihiiS1_b",
    "_ZN11CTTFFontSpr10DrawStringEPKciiihiiS1_b","_ZN6CLevel13MpRenderRadarEv",
    "_ZNK7CSprite11PaintAFrameEiiffjffh","_ZNK7CSprite12PaintFModuleEiiffjffha","_ZNK7CSprite11PaintModuleEiffjha","_ZN6CLevel15RenderCrossHairEib"};
constexpr std::array<uint32_t,10> offsets{0x3b1fe0,0x3eacd0,0x3ecc10,0x3f57b0,0x4cbbc0,0x3a0870,0x3f81b0,0x3eaa90,0x3ea670,0x392590};
}
struct NativeHudScale::Impl {
    static Impl* installed;
    struct Canvas { uint32_t driver,target; unsigned width,height; float factor; unsigned drawing=0; };
    static thread_local Canvas* current;
    RequestedScale& requested;
    std::array<std::unique_ptr<abi::GuestHook>,10> hooks;
    std::array<std::unique_ptr<abi::GuestCall>,8> metric_calls;
    uint32_t native_scale_x=0,native_scale_y=0;
    std::array<uint32_t,10> originals{};
    uint32_t device_address=0,width_address=0,height_address=0,clip_address=0;
    uint32_t image_base=0;
    Impl(const ElfImage& image,RequestedScale& scale):requested(scale) {
        if(installed) throw std::runtime_error("HUD scale already installed");
        image_base=image.base();
        const std::array<uint32_t,10> entries{address(reinterpret_cast<void*>(&hud)),
            address(reinterpret_cast<void*>(&frame)),address(reinterpret_cast<void*>(&rotated)),
            address(reinterpret_cast<void*>(&font)),address(reinterpret_cast<void*>(&ttf)),address(reinterpret_cast<void*>(&radar)),
            address(reinterpret_cast<void*>(&aframe)),address(reinterpret_cast<void*>(&fmodule)),address(reinterpret_cast<void*>(&module)),address(reinterpret_cast<void*>(&crosshair))};
        for(size_t i=0;i<names.size();++i) {
            if(abi::require_function(image,names[i])!=image.address(offsets[i]))
                throw std::runtime_error("HUD scale symbol identity mismatch");
            if(i==5) hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],
                std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},entries[i]);
            else if(i==9) hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],
                std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x38},entries[i]);
            else hooks[i]=std::make_unique<abi::GuestHook>(image,names[i],
                std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},entries[i]);
            originals[i]=hooks[i]->original();
        }
        device_address=image.symbol("g_device");
        if(device_address!=image.address(0x115683c)) throw std::runtime_error("HUD device identity mismatch");
        width_address=image.address(0x11437f0,4); height_address=image.address(0x11437f4,4);
        clip_address=image.symbol("_ZN7CSprite10s_clipRectE");
        if(clip_address!=image.address(0x1156780,16)) throw std::runtime_error("HUD clip identity mismatch");
        constexpr auto x="_ZN11Application9GetScaleXEv",y="_ZN11Application9GetScaleYEv";
        native_scale_x=abi::require_function(image,x);native_scale_y=abi::require_function(image,y);
        // Only these HUD size consumers change. GetScaledPos and every other
        // viewport getter caller retain actual display/target coordinates.
        const std::array<uint32_t,8> sites{0x8e,0xad,0x2f,0x3f,0xe8,0xf8,0x117,0x970};
        const std::array<bool,8> horizontal{true,false,false,true,true,false,true,false};
        for(unsigned i=0;i<sites.size();++i)metric_calls[i]=std::make_unique<abi::GuestCall>(image,i<2?names[0]:names[5],sites[i],horizontal[i]?x:y,
            address(reinterpret_cast<void*>(horizontal[i]?&metric_x:&metric_y)));
        installed=this;
    }
#ifdef NOVA_UI_SCALE_FIXTURE
    Impl(RequestedScale& scale,Bindings b):requested(scale),originals(b.functions),
        device_address(b.device),width_address(b.width),height_address(b.height),clip_address(b.clip) {
        if(installed) throw std::runtime_error("HUD scale already installed"); installed=this;
    }
#endif
    ~Impl() { for(auto& site:metric_calls)site.reset();for(auto& hook:hooks) hook.reset(); installed=nullptr; }
    static float __cdecl metric_x(uint32_t app) {
        return menu_draw_depth ? abi::call_guest_float(installed->native_scale_x,&app,1) : 4.f;
    }
    static float __cdecl metric_y(uint32_t app) {
        return menu_draw_depth ? abi::call_guest_float(installed->native_scale_y,&app,1) : 3.375f;
    }
    static void flush(uint32_t driver) { abi::invoke(word(word(driver)+0xb8),{driver}); }
    static void projection(uint32_t driver,const std::array<float,16>& matrix) {
        abi::invoke(word(word(driver)+0x58),{driver,1,address(matrix.data()),0});
        std::memcpy(reinterpret_cast<void*>(driver+0x1ac),matrix.data(),sizeof matrix);
    }
    static uint64_t draw(unsigned index,const uint32_t* args,unsigned count,Point anchor) {
        auto& self=*installed;
        // Only the native level's 2D pass is admitted. Fonts and nested frames
        // share one transform, avoiding per-glyph or rotated-frame double scale.
        if(menu_draw_depth || !current || current->drawing || current->factor==1.f)
            return abi::call_guest_u64(self.originals[index],args,count);
        auto& canvas=*current;
        if(word(canvas.driver+0xec)!=1 || word(word(canvas.driver+0x118)-4)!=canvas.target) return abi::call_guest_u64(self.originals[index],args,count);
        std::array<float,16> saved;
        std::memcpy(saved.data(),reinterpret_cast<void*>(canvas.driver+0x1ac),sizeof saved);
        auto matrix=saved;
        scale_projection(matrix,anchor,canvas.factor);
        flush(canvas.driver); projection(canvas.driver,matrix); ++canvas.drawing;
        const auto result=abi::call_guest_u64(self.originals[index],args,count);
        flush(canvas.driver); --canvas.drawing; projection(canvas.driver,saved);
        return result;
    }
    static uint64_t __cdecl hud(uint32_t receiver) noexcept {
        try {
            auto& self=*installed;
            const uint32_t args[]{receiver};
            // Do not rewrite display metrics or clip rectangles: native menus,
            // fonts and cached resources must see one coherent real extent.
            if(menu_draw_depth || current) return abi::call_guest_u64(self.originals[0],args,1);
            const auto factor=hud_factor(self.requested.percent());
            if(factor==1.f) return abi::call_guest_u64(self.originals[0],args,1);
            const auto device=word(self.device_address),driver=word(device+8);
            const auto begin=word(driver+0x114),end=word(driver+0x118);
            if(!begin || end<=begin || end-begin>4096 || (end-begin)%4) failed();
            const auto target=word(end-4);
            const auto width=word(self.width_address),height=word(self.height_address);
            if(word(target+0x14) || word(target+0x18) || word(target+0x1c)!=width || word(target+0x20)!=height ||
               uint64_t(width)*9!=uint64_t(height)*16 || width<640 || width>3840)
                return abi::call_guest_u64(self.originals[0],args,1);
            Canvas canvas{driver,target,width,height,factor}; current=&canvas;
            const auto result=abi::call_guest_u64(self.originals[0],args,1);
            current=nullptr;
            if(word(driver+0x118)!=end || word(end-4)!=target) failed();
            return result;
        } catch(...) { failed(); }
    }
    static uint64_t __cdecl frame(uint32_t s,uint32_t f,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t alpha) noexcept {
        try { const uint32_t args[]{s,f,x,y,flags,ox,oy,alpha}; return draw(1,args,8,{std::bit_cast<float>(x),std::bit_cast<float>(y)}); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl rotated(uint32_t s,uint32_t f,uint32_t angle,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t alpha) noexcept {
        try { const uint32_t args[]{s,f,angle,x,y,flags,ox,oy,alpha}; return draw(2,args,9,{std::bit_cast<float>(x),std::bit_cast<float>(y)}); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl font(uint32_t s,uint32_t text,uint32_t x,uint32_t y,uint32_t flags,uint32_t alpha,uint32_t a,uint32_t b,uint32_t end,uint32_t option) noexcept {
        MenuTextTrace::Scope trace(menu_draw_depth!=0,false,s,reinterpret_cast<const char*>(text),address(_ReturnAddress())-installed->image_base);
        try { const uint32_t args[]{s,text,x,y,flags,alpha,a,b,end,option}; return draw(3,args,10,{float(int32_t(x)),float(int32_t(y))}); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl ttf(uint32_t s,uint32_t text,uint32_t x,uint32_t y,uint32_t flags,uint32_t alpha,uint32_t a,uint32_t b,uint32_t end,uint32_t option) noexcept {
        MenuTextTrace::Scope trace(menu_draw_depth!=0,true,s,reinterpret_cast<const char*>(text),address(_ReturnAddress())-installed->image_base);
        try { const uint32_t args[]{s,text,x,y,flags,alpha,a,b,end,option}; return draw(4,args,10,{float(int32_t(x)),float(int32_t(y))}); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl aframe(uint32_t s,uint32_t anim,uint32_t f,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t alpha) noexcept {
        try { const uint32_t args[]{s,anim,f,x,y,flags,ox,oy,alpha}; return draw(6,args,9,{std::bit_cast<float>(x),std::bit_cast<float>(y)}); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl fmodule(uint32_t s,uint32_t f,uint32_t m,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t alpha,uint32_t pal) noexcept {
        try { const uint32_t args[]{s,f,m,x,y,flags,ox,oy,alpha,pal}; return draw(7,args,10,{std::bit_cast<float>(x),std::bit_cast<float>(y)}); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl module(uint32_t s,uint32_t m,uint32_t x,uint32_t y,uint32_t flags,uint32_t alpha,uint32_t pal) noexcept {
        try { std::array<uint32_t,50> view; s=NativeArtwork::prepare_draw(s,m,pal,view); const uint32_t args[]{s,m,x,y,flags,alpha,pal}; return draw(8,args,7,{std::bit_cast<float>(x),std::bit_cast<float>(y)}); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl crosshair(uint32_t receiver,uint32_t dt,uint32_t option) noexcept {
        try {
            const uint32_t args[]{receiver,dt,option};
            if(!current || menu_draw_depth) return abi::call_guest_u64(installed->originals[9],args,3);
            return draw(9,args,3,{current->width*.5f,current->height*.5f});
        } catch(...) { failed(); }
    }
    static uint64_t __cdecl radar(uint32_t receiver) noexcept {
        try {
            const uint32_t args[]{receiver};
            if(!current || menu_draw_depth) return abi::call_guest_u64(installed->originals[5],args,1);
            return draw(5,args,1,fitted_anchor(current->width,current->height,{39,87}));
        } catch(...) { failed(); }
    }
};
NativeHudScale::Impl* NativeHudScale::Impl::installed=nullptr;
thread_local NativeHudScale::Impl::Canvas* NativeHudScale::Impl::current=nullptr;
NativeHudScale::NativeHudScale(const ElfImage& image,RequestedScale& scale):impl_(std::make_unique<Impl>(image,scale)) {}
NativeHudScale::~NativeHudScale()=default;
#ifdef NOVA_UI_SCALE_FIXTURE
NativeHudScale::NativeHudScale(RequestedScale& scale,Bindings b):impl_(std::make_unique<Impl>(scale,b)) {}
uint64_t NativeHudScale::fixture_hud(uint32_t r) { return Impl::hud(r); }
uint64_t NativeHudScale::fixture_frame(const std::array<uint32_t,8>& a) { return Impl::frame(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]); }
uint64_t NativeHudScale::fixture_font(const std::array<uint32_t,10>& a) { return Impl::font(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9]); }
uint64_t NativeHudScale::fixture_crosshair(uint32_t r,uint32_t dt,uint32_t option) { return Impl::crosshair(r,dt,option); }
uint64_t NativeHudScale::fixture_radar(uint32_t r) { return Impl::radar(r); }
uint64_t NativeHudScale::fixture_sprite(unsigned index,const std::array<uint32_t,10>& a) {
    if(index==6) return Impl::aframe(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8]);
    if(index==7) return Impl::fmodule(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9]);
    return Impl::module(a[0],a[1],a[2],a[3],a[4],a[5],a[6]);
}
#endif
}
