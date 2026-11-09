#include "ui/native_scale.h"
#include "ui/menu_surfaces.h"
#include "ui/menu_sprite_layout.h"
#include "ui/pause_canvas.h"
#include "ui/pause_resources.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include <windows.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

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
    std::unique_ptr<abi::GuestCall> pause_metrics;
    uint32_t compute_rects=0,device_slot=0;
    std::array<std::unique_ptr<abi::GuestHook>,7> pause_hooks;
    std::unique_ptr<abi::GuestCall> pause_begin;
    std::unique_ptr<abi::GuestCheckpoint> confirm_begin;
    std::unique_ptr<abi::GuestCall> confirm_metrics;
    std::array<std::unique_ptr<abi::GuestHook>,4> confirm_hooks;
    std::array<uint32_t,4> confirm{};
    std::unique_ptr<abi::GuestHook> report_hook;
    uint32_t report_draw=0;
    uint32_t create=0,update=0,release=0,app=0,current=0,width=0,height=0,touch=0;
    uint32_t sprite_manager=0,get_font=0,font_size=0,get_sprite=0,ttf_vtable=0;
    std::array<uint32_t,4> events{};
    std::unordered_map<uint32_t,PauseExtent> canvases;
    struct Draw {
        uint32_t receiver;
        std::unique_ptr<PauseMetricsScope> metrics;
        std::unique_ptr<PauseResources> resources;
    };
    inline static thread_local Draw* drawing=nullptr;
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
        // Create has just written the sprite scale and has not measured or
        // created controls yet. Its width1920 special case assumes the cached
        // descriptor was loaded at the current resolution, which resize breaks.
        if(abi::require_function(image,"_ZN13GS_InGameMenu6CreateEv")!=image.address(0x495e80) ||
           abi::require_function(image,"_ZN7CSprite17ComputeFrameRectsEv")!=image.address(0x3f4c60))
            throw std::runtime_error("Pause menu metrics identity mismatch");
        pause_metrics=std::make_unique<abi::GuestCall>(image,"_ZN13GS_InGameMenu6CreateEv",0x251,
            "_ZN7CSprite17ComputeFrameRectsEv",address(reinterpret_cast<void*>(&pause_metrics_entry)));
        compute_rects=pause_metrics->original();device_slot=image.symbol("g_device");
        // CReportDisplay's constructor assigns its 960x640 layout scale to
        // both shared sprites, even when their retained descriptors have 2x
        // geometry. Normalize the real width/height queries and draws together.
        constexpr auto report_name="_ZN14CReportDisplay4DrawEi";
        if(abi::require_function(image,report_name)!=image.address(0x3cbe10))
            throw std::runtime_error("Post-match report identity mismatch");
        report_hook=std::make_unique<abi::GuestHook>(image,report_name,
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},
            address(reinterpret_cast<void*>(&report_entry)));
        report_draw=report_hook->original();
        const std::array<const char*,7> pause_names{
            "_ZN13GS_InGameMenu6CreateEv","_ZN13GS_InGameMenu6UpdateEv","_ZN13GS_InGameMenu7ReleaseEv",
            "_ZN15TouchScreenBase10touchBeganERKN6glitch4core8vector2dIsEEl",
            "_ZN15TouchScreenBase10touchMovedERKN6glitch4core8vector2dIsEEl",
            "_ZN15TouchScreenBase10touchEndedERKN6glitch4core8vector2dIsEEl",
            "_ZN15TouchScreenBase14touchCancelledERKN6glitch4core8vector2dIsEEl"};
        const std::array<uint32_t,7> pause_va{0x495e80,0x49b9a0,0x490bb0,0x42e770,0x42e670,0x42e570,0x42e470};
        const std::array<uint32_t,7> pause_entries{address(reinterpret_cast<void*>(&create_entry)),
            address(reinterpret_cast<void*>(&update_entry)),address(reinterpret_cast<void*>(&release_entry)),
            address(reinterpret_cast<void*>(&touch_begin)),address(reinterpret_cast<void*>(&touch_move)),
            address(reinterpret_cast<void*>(&touch_end)),address(reinterpret_cast<void*>(&touch_cancel))};
        for(unsigned i=0;i<pause_hooks.size();++i) {
            if(abi::require_function(image,pause_names[i])!=image.address(pause_va[i]))
                throw std::runtime_error("Pause lifecycle identity mismatch");
            pause_hooks[i]=std::make_unique<abi::GuestHook>(image,pause_names[i],
                std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},pause_entries[i]);
        }
        create=pause_hooks[0]->original();update=pause_hooks[1]->original();release=pause_hooks[2]->original();
        for(unsigned i=0;i<4;++i)events[i]=pause_hooks[i+3]->original();
        // Render calls its parent scene first. Enter only at the next direct
        // call, after world/HUD rendering has finished with the real dimensions.
        pause_begin=std::make_unique<abi::GuestCall>(image,pause_name,0x32,
            "_ZN11Application11GetInstanceEv",address(reinterpret_cast<void*>(&draw_begin)));
        app=pause_begin->original();current=abi::require_function(image,"_ZN12gxStateStack12CurrentStateEv");
        width=image.symbol("g_ScreenW");height=image.symbol("g_ScreenH");touch=image.address(0x11568c4,4);
        // Resolve the original manager slot through Create's verified GOT load.
        sprite_manager=word(image.address(0x113dc1c,4));
        get_font=abi::require_function(image,"_ZN14CSpriteManager7GetFontEi");
        font_size=abi::require_function(image,"_ZN11Application11GetFontSizeEiib");
        get_sprite=abi::require_function(image,"_ZN14CSpriteManager9GetSpriteEPKc");
        ttf_vtable=image.symbol("_ZTV11CTTFFontSpr")+8;
        const std::array<const char*,4> confirm_names{"_ZN10GS_Confirm6CreateEv","_ZN10GS_Confirm6UpdateEv",
            "_ZN10GS_Confirm7ReleaseEv","_ZN10GS_Confirm6RenderEv"};
        const std::array<uint32_t,4> confirm_va{0x416d60,0x41ad50,0x410db0,0x416200};
        const std::array<uint32_t,4> confirm_entries{address(reinterpret_cast<void*>(&confirm_create)),
            address(reinterpret_cast<void*>(&confirm_update)),address(reinterpret_cast<void*>(&confirm_release)),address(reinterpret_cast<void*>(&confirm_render))};
        for(unsigned i=0;i<4;++i) {
            if(abi::require_function(image,confirm_names[i])!=image.address(confirm_va[i]))throw std::runtime_error("Confirmation lifecycle identity mismatch");
            confirm_hooks[i]=std::make_unique<abi::GuestHook>(image,confirm_names[i],
                std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},confirm_entries[i]);
            confirm[i]=confirm_hooks[i]->original();
        }
        confirm_metrics=std::make_unique<abi::GuestCall>(image,confirm_names[0],0xa0,
            "_ZN7CSprite17ComputeFrameRectsEv",address(reinterpret_cast<void*>(&pause_metrics_entry)));
        confirm_begin=std::make_unique<abi::GuestCheckpoint>(image,confirm_names[3],0x30,
            std::initializer_list<uint8_t>{0x8b,0x83,0xfc,0x84,0x01,0x00},address(reinterpret_cast<void*>(&confirm_draw_begin)));
        installed=this;
    }
#ifdef NOVA_UI_SCALE_FIXTURE
    explicit Impl(const std::array<uint32_t,6>& functions):originals(functions) {
        if(installed) throw std::runtime_error("UI scale already installed"); installed=this;
    }
#endif
    ~Impl() { confirm_begin.reset();confirm_metrics.reset();for(auto& hook:confirm_hooks)hook.reset();pause_begin.reset();for(auto& hook:pause_hooks)hook.reset();pause_metrics.reset();for(auto& hook:hooks) hook.reset(); installed=nullptr; }
    PauseExtent extent() const { return {word(width),word(height)}; }
    std::unique_ptr<PauseResources> resources() {
        if(!sprite_manager)return {}; // Synthetic lifecycle fixture.
        auto scope=std::make_unique<PauseResources>(compute_rects);
        const auto manager=word(sprite_manager),application=abi::invoke(app);
        if(!manager || !application)throw std::runtime_error("Pause resources unavailable");
        for(unsigned id=0;id<3;++id) {
            const auto font=abi::invoke(get_font,{manager,id});
            if(font && word(font)==ttf_vtable) {
                const auto pixels=abi::invoke(font_size,{application,id,word(application+0x188cc),
                    *reinterpret_cast<uint8_t*>(font+0xc6)});
                scope->font(font,pixels);
            }
        }
        for(const auto* name:{"Chatboard.bsprite","upgrades.bsprite","game_actions_icons.bsprite"}) {
            const auto icons=abi::invoke(get_sprite,{manager,address(name)});
            if(icons)scope->icons(icons);
        }
        return scope;
    }
    std::unique_ptr<PauseMetricsScope> metrics(uint32_t receiver) {
        const auto found=canvases.find(receiver);
        if(found==canvases.end())return {};
        const auto physical=extent();
        if(!physical.valid() || physical==found->second)return {};
        const auto target=top(word(word(device_slot)+8));
        if(word(target+0x14) || word(target+0x18) || word(target+0x1c)!=physical.width || word(target+0x20)!=physical.height)
            throw std::runtime_error("Pause canvas is not on the main target");
        return std::make_unique<PauseMetricsScope>(width,height,target,found->second);
    }
    static uint64_t create_state(uint32_t receiver,bool confirmation) noexcept {
        try {
            auto& s=*installed;const auto physical=s.extent();
            const PauseExtent extent{1280,720};
            s.canvases.erase(receiver);
            std::unique_ptr<PauseMetricsScope> metrics;
            if(physical.valid()) {
                metrics=std::make_unique<PauseMetricsScope>(s.width,s.height,top(word(word(s.device_slot)+8)),extent);
                // Constructor caches these before Create, potentially before a
                // resolution change. Native layout must start from one extent.
                if(!confirmation)std::memcpy(reinterpret_cast<void*>(receiver+0x224),&extent,sizeof extent);
            }
            auto resources=physical.valid() ? s.resources() : nullptr;
            const uint32_t args[]{receiver};const auto result=abi::call_guest_u64(confirmation?s.confirm[0]:s.create,args,1);
            if((result&0xff) && physical.valid()) {
                if(s.canvases.size()>=16)throw std::runtime_error("Too many retained pause canvases");
                s.canvases[receiver]=extent;
            }
            return result;
        } catch(...) { failure(); }
    }
    static uint64_t __cdecl create_entry(uint32_t r) noexcept {return create_state(r,false);}
    static uint64_t __cdecl confirm_create(uint32_t r) noexcept {return create_state(r,true);}
    static uint64_t release_state(uint32_t receiver,bool confirmation) noexcept {
        try {
            auto& s=*installed;s.canvases.erase(receiver);
            const uint32_t args[]{receiver};return abi::call_guest_u64(confirmation?s.confirm[2]:s.release,args,1);
        } catch(...) { failure(); }
    }
    static uint64_t __cdecl release_entry(uint32_t r) noexcept {return release_state(r,false);}
    static uint64_t __cdecl confirm_release(uint32_t r) noexcept {return release_state(r,true);}
    static uint64_t update_state(uint32_t receiver,bool confirmation) noexcept {
        try {
            auto& s=*installed;auto scope=s.metrics(receiver);
            const uint32_t args[]{receiver};return abi::call_guest_u64(confirmation?s.confirm[1]:s.update,args,1);
        } catch(...) { failure(); }
    }
    static uint64_t __cdecl update_entry(uint32_t r) noexcept {return update_state(r,false);}
    static uint64_t __cdecl confirm_update(uint32_t r) noexcept {return update_state(r,true);}
    static void __cdecl confirm_draw_begin() noexcept {draw_begin();}
    static uint64_t __cdecl draw_begin() noexcept {
        try {
            auto& s=*installed;
            if(drawing && !drawing->metrics)drawing->metrics=s.metrics(drawing->receiver);
            if(drawing && !drawing->resources && s.canvases.contains(drawing->receiver))drawing->resources=s.resources();
            return abi::call_guest_u64(s.app,nullptr,0);
        } catch(...) { failure(); }
    }
    static uint64_t touch_entry(unsigned index,uint32_t root,uint32_t point,uint32_t id) noexcept {
        try {
            auto& s=*installed;
            const uint32_t args[]{root,point,id};
            // Only the dispatch root converts coordinates. Widget overrides,
            // HUD hit hooks and nested callbacks must never convert them twice.
            if(!s.canvases.empty() && root==word(s.touch)) {
                const auto application=abi::invoke(s.app);
                const auto state=application ? abi::invoke(s.current,{application+0x18738}) : 0;
                const auto found=s.canvases.find(state);
                const auto physical=s.extent();
                if(found!=s.canvases.end() && physical.valid() && physical!=found->second) {
                    std::array<int16_t,2> p;std::memcpy(p.data(),reinterpret_cast<void*>(point),sizeof p);
                    p=found->second.unproject(p,physical);
                    // The root bounds check precedes dispatch to native widgets.
                    std::array<int16_t,4> saved;std::memcpy(saved.data(),reinterpret_cast<void*>(root+8),sizeof saved);
                    const std::array<int16_t,4> logical{0,0,static_cast<int16_t>(found->second.width),static_cast<int16_t>(found->second.height)};
                    auto scope=s.metrics(state);
                    std::memcpy(reinterpret_cast<void*>(root+8),logical.data(),sizeof logical);
                    const uint32_t mapped[]{root,address(p.data()),id};
                    const auto result=abi::call_guest_u64(s.events[index],mapped,3);
                    std::memcpy(reinterpret_cast<void*>(root+8),saved.data(),sizeof saved);
                    return result;
                }
            }
            return abi::call_guest_u64(s.events[index],args,3);
        } catch(...) { failure(); }
    }
    static uint64_t __cdecl touch_begin(uint32_t r,uint32_t p,uint32_t id) noexcept {return touch_entry(0,r,p,id);}
    static uint64_t __cdecl touch_move(uint32_t r,uint32_t p,uint32_t id) noexcept {return touch_entry(1,r,p,id);}
    static uint64_t __cdecl touch_end(uint32_t r,uint32_t p,uint32_t id) noexcept {return touch_entry(2,r,p,id);}
    static uint64_t __cdecl touch_cancel(uint32_t r,uint32_t p,uint32_t id) noexcept {return touch_entry(3,r,p,id);}
    static uint64_t __cdecl pause_metrics_entry(uint32_t sprite) noexcept {
        try {
            auto& s=*installed;
            const auto device=word(s.device_slot),driver=word(device+8),target=top(driver);
            const auto left=word(target+0x14),upper=word(target+0x18);
            const auto right=word(target+0x1c),bottom=word(target+0x20);
            const auto compute=[&](uint32_t value) {return abi::call_guest_u64(s.compute_rects,&value,1);};
            if(right>left && bottom>upper && right-left<=8192 && bottom-upper<=8192)
                return rebuild_pause_sprite(sprite,right-left,bottom-upper,compute);
            return compute(sprite);
        } catch(...) { failure(); }
    }
    static uint64_t __cdecl report_entry(uint32_t receiver,uint32_t alpha) noexcept {
        try {
            auto& s=*installed;
            float layout;std::memcpy(&layout,reinterpret_cast<void*>(receiver+0xa8),4);
            if(!std::isfinite(layout) || layout<=0.f || layout>32.f)
                throw std::runtime_error("Post-match report layout is invalid");
            PauseResources resources(s.compute_rects);
            // +0x80 is Chatboard (including loadout slots); +0x84 is ranks.
            // Use descriptor identity, never physical texture dimensions:
            // NativeArtwork may have promoted only the source rectangles.
            resources.icons_at_layout(word(receiver+0x80),layout);
            resources.icons_at_layout(word(receiver+0x84),layout);
            const uint32_t args[]{receiver,alpha};
            return abi::call_guest_u64(s.report_draw,args,2);
        } catch(...) { failure(); }
    }
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
    static uint64_t draw_state(uint32_t r,bool confirmation) noexcept {
        Draw context{r,{},{}};auto* previous=drawing;drawing=&context;
        MenuDrawScope scope;
        const auto function=confirmation?installed->confirm[3]:installed->originals[4];
        const auto result=abi::call_guest_u64(function,&r,1);
        context.resources.reset();context.metrics.reset();drawing=previous;return result;
    }
    static uint64_t __cdecl pause_entry(uint32_t r) noexcept {return draw_state(r,false);}
    static uint64_t __cdecl confirm_render(uint32_t r) noexcept {return draw_state(r,true);}
    static uint64_t __cdecl manager_entry(uint32_t r) noexcept { return menu_entry(r,5); }
};
NativeScale::Impl* NativeScale::Impl::installed=nullptr;
NativeScale::NativeScale(const ElfImage& i,RequestedScale&):impl_(std::make_unique<Impl>(i)) {}
NativeScale::~NativeScale()=default;
#ifdef NOVA_UI_SCALE_FIXTURE
NativeScale::NativeScale(RequestedScale&,const std::array<uint32_t,6>& f,uint32_t):impl_(std::make_unique<Impl>(f)) {}
void NativeScale::fixture_pause_bindings(PauseBindings b) {
    auto& s=*impl_;s.create=b.create;s.update=b.update;s.release=b.release;s.app=b.app;s.current=b.current;
    s.device_slot=b.device;s.width=b.width;s.height=b.height;s.touch=b.touch;s.events=b.events;
    s.confirm=b.confirmation;
}
uint64_t NativeScale::fixture_create(uint32_t r,bool c){return Impl::create_state(r,c);}
uint64_t NativeScale::fixture_update(uint32_t r,bool c){return Impl::update_state(r,c);}
uint64_t NativeScale::fixture_release(uint32_t r,bool c){return Impl::release_state(r,c);}
uint64_t NativeScale::fixture_draw_begin(){return Impl::draw_begin();}
uint64_t NativeScale::fixture_touch(unsigned i,uint32_t r,uint32_t p,uint32_t id){return Impl::touch_entry(i,r,p,id);}
uint64_t NativeScale::fixture_viewport(uint32_t d,uint32_t r) { return Impl::viewport_entry(d,r); }
uint64_t NativeScale::fixture_projection(uint32_t d) {
    const uint32_t args[]{d}; return abi::call_guest_u64(Impl::installed->originals[1],args,1);
}
uint64_t NativeScale::fixture_screen(uint32_t s) { return Impl::screen_entry(s); }
uint64_t NativeScale::fixture_hit(uint32_t s,uint32_t x,uint32_t y,uint32_t ox,uint32_t oy) {
    const uint32_t args[]{s,x,y,ox,oy}; return abi::call_guest_u64(Impl::installed->originals[3],args,5);
}
uint64_t NativeScale::fixture_pause(uint32_t s,bool c) { return Impl::draw_state(s,c); }
uint64_t NativeScale::fixture_manager(uint32_t s) { return Impl::manager_entry(s); }
#endif
}
