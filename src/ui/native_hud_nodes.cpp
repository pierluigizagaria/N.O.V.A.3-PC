#include "ui/native_hud_nodes.h"
#include "ui/native_pixel_projection.h"
#include "ui/hud_anchor.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include "menu/initial_target_scaling.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <unordered_map>

namespace nova::ui {
namespace {
uint32_t address(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t word(uint32_t p) { uint32_t v; std::memcpy(&v,reinterpret_cast<void*>(p),4); return v; }
constexpr auto owner="_ZN4CHud6RenderEv";
constexpr auto draw_name="_ZN17CNovaSceneManager8drawNodeEN5boost13intrusive_ptrIN6glitch5scene10ISceneNodeEEE";
constexpr auto find_name="_ZN6glitch5scene13CSceneManager20getSceneNodeFromNameEPKcRKN5boost13intrusive_ptrINS0_10ISceneNodeEEE";
constexpr auto scale_name="_ZN6glitch5scene10ISceneNode8setScaleERKNS_4core8vector3dIfEE";
constexpr auto update_name="_ZN6glitch5scene10ISceneNode22updateAbsolutePositionEb";
constexpr auto release_name="_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE";
}
struct NativeHudNodes::Impl {
    static Impl* installed;
    RequestedScale& requested;
    std::unique_ptr<abi::GuestCall> call;
    std::unique_ptr<NativePixelProjection> pixels;
    std::unique_ptr<abi::GuestHook> button_init,sweep_init,sweep_update,touch_hit;
    std::array<std::unique_ptr<abi::GuestCall>,6> touch_destroy;
    uint32_t button_init_original=0,sweep_init_original=0,sweep_update_original=0,touch_destroy_original=0,touch_hit_original=0,width=0,level_slot=0,last_hud=0;
    struct Hit { uint32_t hud,group; unsigned height; Point anchor; };
    std::unordered_map<uint32_t,Hit> hits;
    uint32_t original,find,set_scale,update,release,height;
    Impl(const ElfImage& image,RequestedScale& s):requested(s) {
        if(installed) throw std::runtime_error("HUD node scale already installed");
        const auto require=[&](const char* name,uint32_t va) {
            const auto fn=abi::require_function(image,name);
            if(fn!=image.address(va)) throw std::runtime_error("HUD node scale identity mismatch");
            return fn;
        };
        require(owner,0x34ffb0); require(draw_name,0x31fff0);
        find=require(find_name,0xa25be0); set_scale=require(scale_name,0x9cd040);
        update=require(update_name,0x9cd4d0); release=require(release_name,0xe1850);
        height=image.address(0x11437f4,4);
        width=image.address(0x11437f0,4);level_slot=image.symbol("_ZN6CLevel10m_instanceE");
        call=std::make_unique<abi::GuestCall>(image,owner,0x419,draw_name,address(reinterpret_cast<void*>(&draw)));
        original=call->original();
        pixels=std::make_unique<NativePixelProjection>(image);
        button_init=std::make_unique<abi::GuestHook>(image,"_ZN10CButtonHud4InitEP4CHudiRKN6glitch4core4rectIsEE",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x83,0xec,0x10},address(reinterpret_cast<void*>(&init_button)));
        touch_hit=std::make_unique<abi::GuestHook>(image,"_ZN15TouchScreenBase13IsTouchInsideERKN6glitch4core8vector2dIsEE",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x83,0xec,0x30},address(reinterpret_cast<void*>(&inside)));
        sweep_init=std::make_unique<abi::GuestHook>(image,"_ZN15CButtonHudSweep4InitEP4CHudP20CHudSweepControlNodeiii",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x83,0xec,0x20},address(reinterpret_cast<void*>(&init_sweep)));
        sweep_update=std::make_unique<abi::GuestHook>(image,"_ZN15CButtonHudSweep6UpdateEb",
            std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},address(reinterpret_cast<void*>(&update_sweep)));
        const std::array<const char*,6> deaths{"_ZN10CButtonHudD1Ev","_ZN10CButtonHudD2Ev","_ZN10CButtonHudD0Ev",
            "_ZN15CButtonHudSweepD1Ev","_ZN15CButtonHudSweepD2Ev","_ZN15CButtonHudSweepD0Ev"};
        for(unsigned i=0;i<deaths.size();++i)touch_destroy[i]=std::make_unique<abi::GuestCall>(image,deaths[i],i<3?0x4c:0x8a,
            "_ZN15TouchScreenBaseD2Ev",address(reinterpret_cast<void*>(&destroy_touch)));
        sweep_init_original=sweep_init->original();sweep_update_original=sweep_update->original();touch_destroy_original=touch_destroy[0]->original();
        button_init_original=button_init->original();touch_hit_original=touch_hit->original();installed=this;
    }
#ifdef NOVA_UI_SCALE_FIXTURE
    Impl(RequestedScale& s,Bindings b):requested(s),original(b.draw),find(b.find),set_scale(b.set_scale),
        update(b.update),release(b.release),height(b.height) {
        if(installed) throw std::runtime_error("HUD node scale already installed"); installed=this;
    }
#endif
    ~Impl() { for(auto& site:touch_destroy)site.reset();sweep_update.reset();sweep_init.reset();touch_hit.reset();button_init.reset();call.reset();pixels.reset();installed=nullptr; }
    static uint64_t __cdecl destroy_touch(uint32_t button) noexcept {
        installed->hits.erase(button);
        const uint32_t args[]{button};return abi::call_guest_u64(installed->touch_destroy_original,args,1);
    }
    void remember(uint32_t button,uint32_t hud,uint32_t node) {
        if(hud!=last_hud){hits.clear();last_hud=hud;}
        hits.erase(button);
        const auto group=group_for(node);
        if(group && word(height))hits.emplace(button,Hit{hud,group,word(height),anchor(hud,group)});
    }
    static uint64_t __cdecl init_sweep(uint32_t button,uint32_t hud,uint32_t node,uint32_t x,uint32_t y,uint32_t index) noexcept {
        try {
            auto& s=*installed;const uint32_t args[]{button,hud,node,x,y,index};
            const auto result=abi::call_guest_u64(s.sweep_init_original,args,6);
            s.remember(button,hud,node);return result;
        }catch(...){std::fputs("HUD sweep initialization failed\n",stderr);TerminateProcess(GetCurrentProcess(),86);std::abort();}
    }
    static uint64_t __cdecl update_sweep(uint32_t button,uint32_t option) noexcept {
        try {
            auto& s=*installed;const uint32_t args[]{button,option};const auto found=s.hits.find(button);
            const auto level=word(s.level_slot);
            if(found==s.hits.end() || !level || word(level+0x638)!=found->second.hud)return abi::call_guest_u64(s.sweep_update_original,args,2);
            // Native gesture deltas are actual screen pixels. Adapt the one
            // read-only quarter-width threshold, leaving gesture state native.
            auto& threshold=*reinterpret_cast<int32_t*>(button+0xc8);
            const auto saved=threshold;
            threshold=std::max(1,int(std::lround(saved*resolution_pixel_factor(found->second.height)*hud_factor(s.requested.percent()))));
            const auto result=abi::call_guest_u64(s.sweep_update_original,args,2);
            threshold=saved;return result;
        }catch(...){std::fputs("HUD sweep pixel boundary failed\n",stderr);TerminateProcess(GetCurrentProcess(),86);std::abort();}
    }
    static uint32_t group_for(uint32_t node) {
        for(unsigned depth=0;node && depth<128;++depth) {
            const auto name=word(node+0xc);
            if(name && hud_edge(reinterpret_cast<const char*>(name)))return node;
            const auto parent=word(node+0xd0);
            if(!parent)return 0;
            const auto label=word(parent+0xc);
            if(label && !_stricmp(reinterpret_cast<const char*>(label),"dummy_hud"))return 0;
            node=parent;
        }
        return 0;
    }
    Point anchor(uint32_t hud,uint32_t group) {
        const auto name=word(group+0xc);
        if(name)if(const auto edge=hud_edge(reinterpret_cast<const char*>(name)))
            return {(1+(*edge)[0])*word(width)*.5f,(1-(*edge)[1])*word(height)*.5f};
        const auto camera=word(hud+0x34),table=word(camera);
        const auto p=abi::invoke(word(table+0xf8),{camera});
        const auto v=abi::invoke(word(table+0xfc),{camera});
        PixelMatrix projection,view,world;
        std::memcpy(projection.data(),reinterpret_cast<void*>(p),sizeof projection);
        std::memcpy(view.data(),reinterpret_cast<void*>(v),sizeof view);
        std::memcpy(world.data(),reinterpret_cast<void*>(group+0x10),sizeof world);
        const auto clip=project_vector(projection,project_vector(view,{world[12],world[13],world[14],1}));
        if(!std::isfinite(clip[3]) || std::abs(clip[3])<1e-6f)throw std::runtime_error("HUD input pivot invalid");
        return {(1+clip[0]/clip[3])*word(width)*.5f,(1-clip[1]/clip[3])*word(height)*.5f};
    }
    static uint64_t __cdecl init_button(uint32_t button,uint32_t hud,uint32_t index,uint32_t rect) noexcept {
        try {
            auto& s=*installed;
            const uint32_t args[]{button,hud,index,rect};
            const auto result=abi::call_guest_u64(s.button_init_original,args,4);
            if(hud!=s.last_hud){s.hits.clear();s.last_hud=hud;}
            s.hits.erase(button);
            const auto begin=word(hud+0xd0),end=word(hud+0xd4);
            if(end<begin || end-begin>4096 || (end-begin)%16)throw std::runtime_error("HUD input registry invalid");
            if(index<(end-begin)/16) {
                auto node=word(begin+index*16);if(!node)node=word(begin+index*16+4);
                s.remember(button,hud,node);
            }
            return result;
        }catch(...){std::fputs("HUD pixel input initialization failed\n",stderr);TerminateProcess(GetCurrentProcess(),86);std::abort();}
    }
    static uint64_t __cdecl inside(uint32_t button,uint32_t point) noexcept {
        try {
            auto& s=*installed;const auto found=s.hits.find(button);
            const auto level=word(s.level_slot);
            if(found==s.hits.end() || !level || word(level+0x638)!=found->second.hud) {
                const uint32_t args[]{button,point};return abi::call_guest_u64(s.touch_hit_original,args,2);
            }
            const auto& hit=found->second;
            const auto now=s.anchor(hit.hud,hit.group);
            const auto factor=resolution_pixel_factor(hit.height)*hud_factor(s.requested.percent());
            std::array<int16_t,2> old;std::memcpy(old.data(),reinterpret_cast<void*>(point),sizeof old);
            const float x=hit.anchor.x+(old[0]-now.x)/factor,y=hit.anchor.y+(old[1]-now.y)/factor;
            if(!std::isfinite(x) || !std::isfinite(y) || x<-32768 || x>32767 || y<-32768 || y>32767)return 0;
            old={static_cast<int16_t>(std::lround(x)),static_cast<int16_t>(std::lround(y))};
            const uint32_t args[]{button,address(old.data())};return abi::call_guest_u64(s.touch_hit_original,args,2);
        }catch(...){std::fputs("HUD pixel input boundary failed\n",stderr);TerminateProcess(GetCurrentProcess(),86);std::abort();}
    }
    uint32_t lookup(uint32_t manager,uint32_t root,const char* name) {
        uint32_t out=0;
        const uint32_t args[]{address(&out),manager,address(name),root};
        menu::menu_call_sret4(find,args,4);
        return out;
    }
    void drop(uint32_t node) { if(node) abi::invoke(release,{node+word(word(node)-0x10)}); }
    static uint64_t __cdecl draw(uint32_t manager,uint32_t root_ref) noexcept {
        try {
            auto& self=*installed;
            const uint32_t args[]{manager,root_ref};
            const auto factor=hud_factor(self.requested.percent());
            const auto pixel_factor=resolution_pixel_factor(word(self.height));
            if(menu_draw_depth || (factor==1.f && pixel_factor==1.f) || !word(root_ref)) return abi::call_guest_u64(self.original,args,2);
            std::vector<uint32_t> held,nodes;
            // Saved native hierarchy distinguishes presentation groups from
            // imported containers at the camera origin and visor coverage.
            // Scaling those containers would move the health anchor or put its
            // pivot on the eye plane. Admit the actual visual group instead.
            std::unordered_map<uint32_t,uint32_t> shared_pivots;
            std::unordered_map<uint32_t,std::array<float,2>> edges;
            uint32_t shield=0;
            for(const auto name:{"shield","weapons","grenade_button","pause_button",
                                 "MP_scores_button","MP_pause_button","hud_jetpack","hud_jeep"}) {
                const auto node=self.lookup(manager,root_ref,name);held.push_back(node);
                if(node)edges.emplace(node,*hud_edge(name));
                if(!std::strcmp(name,"shield"))shield=node;
                if(node && std::find(nodes.begin(),nodes.end(),node)==nodes.end())nodes.push_back(node);
            }
            // Both sibling backing layers belong to the health display. Use
            // the bar's pivot, not each backing mesh's different local origin.
            if(shield)for(const auto name:{"background_top","background_shield","shield_upgrade_vanguard","shield_upgrade_plasma","shield_upgrade_crypton"}) {
                const auto node=self.lookup(manager,root_ref,name);held.push_back(node);
                if(node && std::find(nodes.begin(),nodes.end(),node)==nodes.end()) {
                    nodes.push_back(node);shared_pivots.emplace(node,shield);
                }
            }
            // CHud selected its own camera before this call. Exclude its
            // ancestry before deduplication so other named groups below an
            // excluded container can still receive their independent scale.
            std::vector<uint32_t> camera_lineage;
            for(auto camera=word(manager+0xdc);camera;camera=word(camera+0xd0)) {
                if(camera_lineage.size()>=128) throw std::runtime_error("HUD camera parent cycle");
                camera_lineage.push_back(camera);
            }
            std::erase_if(nodes,[&](uint32_t node) {
                return std::find(camera_lineage.begin(),camera_lineage.end(),node)!=camera_lineage.end();
            });
            std::vector<uint32_t> pixel_groups;
            for(const auto node:nodes) {
                bool nested=false; unsigned depth=0;
                for(auto parent=word(node+0xd0);parent;parent=word(parent+0xd0)) {
                    if(++depth>128) throw std::runtime_error("HUD parent cycle");
                    if(std::find(nodes.begin(),nodes.end(),parent)!=nodes.end()) { nested=true; break; }
                }
                if(nested) continue;
                pixel_groups.push_back(node);
            }
            uint64_t answer;
            {
                NativePixelProjection::Scope scope(pixel_groups,pixel_factor*factor,shared_pivots,edges);
                answer=abi::call_guest_u64(self.original,args,2);
            }
            for(const auto node:held) self.drop(node);
            return answer;
        } catch(...) {
            std::fputs("native-ui-scale FAIL boundary=hud_nodes\n",stderr); std::fflush(stderr);
            TerminateProcess(GetCurrentProcess(),86); std::abort();
        }
    }
};
NativeHudNodes::Impl* NativeHudNodes::Impl::installed=nullptr;
NativeHudNodes::NativeHudNodes(const ElfImage& i,RequestedScale& s):impl_(std::make_unique<Impl>(i,s)) {}
NativeHudNodes::~NativeHudNodes()=default;
#ifdef NOVA_UI_SCALE_FIXTURE
NativeHudNodes::NativeHudNodes(RequestedScale& s,Bindings b):impl_(std::make_unique<Impl>(s,b)) {}
uint64_t NativeHudNodes::fixture_draw(uint32_t m,uint32_t r) { return Impl::draw(m,r); }
#endif
}
