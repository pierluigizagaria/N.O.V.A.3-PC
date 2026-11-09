#include "ui/native_hud_scale.h"
#include "ui/native_artwork.h"
#include "ui/menu_text_trace.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <intrin.h>
#include <bit>
#include <cmath>
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
    std::unique_ptr<abi::GuestHook> power_draw;
    std::array<std::unique_ptr<abi::GuestCall>,3> power_clips;
    std::array<std::unique_ptr<abi::GuestCall>,2> power_icons;
    uint32_t power_paint=0;
    uint32_t power_original=0,power_rect=0,power_set_clip=0,level_slot=0;
    static thread_local const Point* power_anchor;
    static thread_local Point power_destination;
    std::array<std::unique_ptr<abi::GuestCall>,6> metric_calls;
    uint32_t native_scale_x=0,native_scale_y=0;
    uint32_t player_component=0,is_sniper=0;
    static thread_local unsigned native_overlay;
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
        // Render2D's metric locals also place in-match messages. Keep them
        // native; only the radar's size consumers use reference pixels.
        const std::array<uint32_t,6> sites{0x2f,0x3f,0xe8,0xf8,0x117,0x970};
        const std::array<bool,6> horizontal{false,true,true,false,true,false};
        for(unsigned i=0;i<sites.size();++i)metric_calls[i]=std::make_unique<abi::GuestCall>(image,names[5],sites[i],horizontal[i]?x:y,
            address(reinterpret_cast<void*>(horizontal[i]?&metric_x:&metric_y)));
        player_component=abi::require_function(image,"_ZN6CLevel18GetPlayerComponentEv");
        is_sniper=abi::require_function(image,"_ZN16CWeaponComponent8IsSniperEv");
        if(player_component!=image.address(0x391680) || is_sniper!=image.address(0x17cac0))
            throw std::runtime_error("HUD overlay owner identity mismatch");
        level_slot=image.symbol("_ZN6CLevel10m_instanceE");
        power_rect=abi::require_function(image,"_ZNK7CSprite12GetFrameRectERN6glitch4core4rectIsEEiiijiib");
        power_set_clip=abi::require_function(image,"_ZN7CSprite7SetClipEiiii");
        power_draw=std::make_unique<abi::GuestHook>(image,"_ZN12CButtonSweep4DrawEi",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},address(reinterpret_cast<void*>(&selector)));
        power_original=power_draw->original();
        const std::array<const char*,3> clip_owners{"_ZN12CButtonSweep4DrawEi",
            "_ZN12CButtonSweep8DrawDataERK9SweepDatabiiiiiibi.clone.0",
            "_ZN12CButtonSweep8DrawDataERK9SweepDatabiiiiiibi"};
        const std::array<uint32_t,3> clip_sites{0x54,0x54,0x63};
        for(unsigned i=0;i<clip_sites.size();++i)
            power_clips[i]=std::make_unique<abi::GuestCall>(image,clip_owners[i],clip_sites[i],
                "_ZN7CSprite7SetClipEiiii",address(reinterpret_cast<void*>(&selector_clip)));
        power_paint=abi::require_function(image,names[1]);
        for(unsigned i=0;i<power_icons.size();++i)
            power_icons[i]=std::make_unique<abi::GuestCall>(image,clip_owners[i+1],i?0xe2:0xdc,
                names[1],address(reinterpret_cast<void*>(&selector_icon)));
        installed=this;
    }
#ifdef NOVA_UI_SCALE_FIXTURE
    Impl(RequestedScale& scale,Bindings b):requested(scale),player_component(b.player_component),is_sniper(b.is_sniper),originals(b.functions),
        device_address(b.device),width_address(b.width),height_address(b.height),clip_address(b.clip) {
        if(installed) throw std::runtime_error("HUD scale already installed"); installed=this;
    }
#endif
    ~Impl() { for(auto& site:power_icons)site.reset();for(auto& site:power_clips)site.reset();power_draw.reset();for(auto& site:metric_calls)site.reset();for(auto& hook:hooks) hook.reset(); installed=nullptr; }
    static uint64_t __cdecl selector_icon(uint32_t sprite,uint32_t frame,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t alpha) noexcept {
        try {
            // Handed touch schemes may flip the border, never the icon itself.
            if(power_anchor)flags&=~1u;
            const uint32_t args[]{sprite,frame,x,y,flags,ox,oy,alpha};
            return abi::call_guest_u64(installed->power_paint,args,8);
        }catch(...) { failed(); }
    }
    static uint64_t __cdecl selector_clip(int32_t x,int32_t y,int32_t width,int32_t height) noexcept {
        try {
            if(power_anchor && current) {
                const auto factor=current->factor;
                const auto left=int32_t(std::floor(power_destination.x+(x-power_anchor->x)*factor));
                const auto top=int32_t(std::floor(power_destination.y+(y-power_anchor->y)*factor));
                const auto right=int32_t(std::ceil(power_destination.x+(x+width-power_anchor->x)*factor));
                const auto bottom=int32_t(std::ceil(power_destination.y+(y+height-power_anchor->y)*factor));
                x=left;y=top;width=right-left;height=bottom-top;
            }
            const uint32_t args[]{uint32_t(x),uint32_t(y),uint32_t(width),uint32_t(height)};
            return abi::call_guest_u64(installed->power_set_clip,args,4);
        }catch(...) { failed(); }
    }
    static uint64_t __cdecl selector(uint32_t button,uint32_t alpha) noexcept {
        try {
            auto& self=*installed;const uint32_t args[]{button,alpha};
            const auto original=[&] { return abi::call_guest_u64(self.power_original,args,2); };
            if(!current || menu_draw_depth || current->drawing)return original();
            const auto level=word(self.level_slot),normal=level?word(level+0x17c):0;
            if(!normal)return original();
            const auto scheme=word(normal+0xc);
            const auto special=scheme?abi::invoke(word(word(scheme)+0x44),{scheme}):0;
            if(button!=special && button!=word(normal+0x5c))return original();
            const auto begin=word(button+0x84),end=word(button+0x88);
            const auto selection=int32_t(word(button+0xa4));
            if(end<begin || (end-begin)%44 || end-begin>44*128 || selection<0 || uint32_t(selection)>=(end-begin)/44)
                return original();
            const auto entry=begin+uint32_t(selection)*44,sprite=word(entry);
            const auto frame=word(entry+(*reinterpret_cast<const uint8_t*>(entry+0x10)?12:8));
            if(!sprite || frame>=*reinterpret_cast<const uint16_t*>(sprite+0x48))return original();
            const auto rect=reinterpret_cast<const int16_t*>(button+8);
            const auto origin=reinterpret_cast<const int16_t*>(button+0x48);
            Point anchor{(rect[0]+rect[2])*.5f,(rect[1]+rect[3])*.5f};
            const auto old_x=word(button+0xc8),old_y=word(button+0xcc);
            const int32_t x=origin[0]+int32_t(word(button+0x30))+int32_t(old_x);
            const int32_t y=origin[1]+int32_t(word(button+0x34))+int32_t(word(button+0x38))+int32_t(old_y);
            std::array<int16_t,4> icon{};
            abi::invoke(self.power_rect,{sprite,address(icon.data()),frame,uint32_t(x),uint32_t(y),
                uint32_t(*reinterpret_cast<const uint8_t*>(button+0xb9))&~1u,0,0,0});
            // Menu icon frames carry different authored pivots from controls.
            // Center their native bounds inside the selector's clipping window.
            const auto center_x=anchor.x+(int32_t(word(button+0xa8))-int32_t(word(button+0xac)))*.5f;
            struct Offsets {
                uint32_t button,x,y;
                ~Offsets(){*reinterpret_cast<uint32_t*>(button+0xc8)=x;*reinterpret_cast<uint32_t*>(button+0xcc)=y;}
            } offsets{button,old_x,old_y};
            *reinterpret_cast<int32_t*>(button+0xc8)=int32_t(old_x)+int32_t(std::lround(center_x-(icon[0]+icon[2])*.5f));
            *reinterpret_cast<int32_t*>(button+0xcc)=int32_t(old_y)+int32_t(std::lround(anchor.y-(icon[1]+icon[3])*.5f));
            auto& canvas=*current;
            if(word(canvas.driver+0xec)!=1 || word(word(canvas.driver+0x118)-4)!=canvas.target)return original();
            std::array<float,16> saved;std::memcpy(saved.data(),reinterpret_cast<void*>(canvas.driver+0x1ac),sizeof saved);
            // Native left-handed touch layouts place this feedback under the
            // movement pad. PC keeps the informative selector on the right;
            // translate the whole group and its clip without changing input
            // geometry, preferences or the original sweep animation.
            Point destination=anchor;
            if((*reinterpret_cast<const uint8_t*>(button+0xb9)&1u) && anchor.x<canvas.width*.5f)
                destination.x=canvas.width-anchor.x;
            auto matrix=saved;scale_projection(matrix,anchor,canvas.factor);
            matrix[12]+=saved[0]*(destination.x-anchor.x);
            matrix[13]+=saved[5]*(destination.y-anchor.y);
            power_destination=destination;
            flush(canvas.driver);projection(canvas.driver,matrix);++canvas.drawing;power_anchor=&anchor;
            const auto result=original();
            flush(canvas.driver);power_anchor=nullptr;--canvas.drawing;projection(canvas.driver,saved);
            return result;
        }catch(...) { failed(); }
    }
    static float __cdecl metric_x(uint32_t app) {
        return menu_draw_depth ? abi::call_guest_float(installed->native_scale_x,&app,1) : 1280.f/480;
    }
    static float __cdecl metric_y(uint32_t app) {
        return menu_draw_depth ? abi::call_guest_float(installed->native_scale_y,&app,1) : 720.f/320;
    }
    static void flush(uint32_t driver) { abi::invoke(word(word(driver)+0xb8),{driver}); }
    static void projection(uint32_t driver,const std::array<float,16>& matrix) {
        abi::invoke(word(word(driver)+0x58),{driver,1,address(matrix.data()),0});
        std::memcpy(reinterpret_cast<void*>(driver+0x1ac),matrix.data(),sizeof matrix);
    }
    static uint64_t draw(unsigned index,const uint32_t* args,unsigned count,Point anchor,std::optional<Point> destination={}) {
        auto& self=*installed;
        // Only the native level's 2D pass is admitted. Fonts and nested frames
        // share one transform, avoiding per-glyph or rotated-frame double scale.
        if(menu_draw_depth || native_overlay || !current || current->drawing ||
           (current->factor==1.f && (!destination || (destination->x==anchor.x && destination->y==anchor.y))))
            return abi::call_guest_u64(self.originals[index],args,count);
        auto& canvas=*current;
        if(word(canvas.driver+0xec)!=1 || word(word(canvas.driver+0x118)-4)!=canvas.target) return abi::call_guest_u64(self.originals[index],args,count);
        std::array<float,16> saved;
        std::memcpy(saved.data(),reinterpret_cast<void*>(canvas.driver+0x1ac),sizeof saved);
        auto matrix=saved;
        scale_projection(matrix,anchor,canvas.factor);
        if(destination) {
            matrix[12]+=saved[0]*(destination->x-anchor.x);
            matrix[13]+=saved[5]*(destination->y-anchor.y);
        }
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
            const auto device=word(self.device_address);
            if(!device)return abi::call_guest_u64(self.originals[0],args,1);
            const auto driver=word(device+8);
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
        try { OverlayScope scope; const uint32_t args[]{s,text,x,y,flags,alpha,a,b,end,option}; return abi::call_guest_u64(installed->originals[3],args,10); }
        catch(...) { failed(); }
    }
    static uint64_t __cdecl ttf(uint32_t s,uint32_t text,uint32_t x,uint32_t y,uint32_t flags,uint32_t alpha,uint32_t a,uint32_t b,uint32_t end,uint32_t option) noexcept {
        MenuTextTrace::Scope trace(menu_draw_depth!=0,true,s,reinterpret_cast<const char*>(text),address(_ReturnAddress())-installed->image_base);
        try { OverlayScope scope; const uint32_t args[]{s,text,x,y,flags,alpha,a,b,end,option}; return abi::call_guest_u64(installed->originals[4],args,10); }
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
    struct OverlayScope {
        OverlayScope(){++native_overlay;}
        ~OverlayScope(){--native_overlay;}
    };
    static bool sniper(uint32_t receiver) {
        auto& s=*installed;
        if(!s.player_component)return false; // Synthetic fixture has no game state.
        const auto player=abi::invoke(s.player_component,{receiver});
        if(!player)return false;
        const auto manager=word(player+0x170);
        if(!manager)return false;
        const auto begin=word(manager+0x148),end=word(manager+0x14c),index=word(manager+0x160);
        if(!begin || end<begin || end-begin>4096 || (end-begin)%24 || index>=(end-begin)/24)return false;
        const auto weapon=word(begin+index*24+12);
        return weapon && abi::invoke_bool(s.is_sniper,{weapon});
    }
    static uint64_t __cdecl crosshair(uint32_t receiver,uint32_t dt,uint32_t option) noexcept {
        try {
            const uint32_t args[]{receiver,dt,option};
            if(current && current->factor!=1.f && !menu_draw_depth && sniper(receiver)) {
                OverlayScope scope;
                return abi::call_guest_u64(installed->originals[9],args,3);
            }
            if(!current || menu_draw_depth) return abi::call_guest_u64(installed->originals[9],args,3);
            return draw(9,args,3,{current->width*.5f,current->height*.5f});
        } catch(...) { failed(); }
    }
    static uint64_t __cdecl radar(uint32_t receiver) noexcept {
        try {
            const uint32_t args[]{receiver};
            if(!current || menu_draw_depth) return abi::call_guest_u64(installed->originals[5],args,1);
            const auto reference=fitted_anchor(1280,720,{39,87});
            return draw(5,args,1,fitted_anchor(current->width,current->height,{39,87}),
                        Point{reference.x*current->factor,reference.y*current->factor});
        } catch(...) { failed(); }
    }
};
NativeHudScale::Impl* NativeHudScale::Impl::installed=nullptr;
thread_local NativeHudScale::Impl::Canvas* NativeHudScale::Impl::current=nullptr;
thread_local unsigned NativeHudScale::Impl::native_overlay=0;
thread_local const Point* NativeHudScale::Impl::power_anchor=nullptr;
thread_local Point NativeHudScale::Impl::power_destination{};
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
