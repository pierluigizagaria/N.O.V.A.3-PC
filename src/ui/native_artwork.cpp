#include "ui/native_artwork.h"
#include "ui/sprite_density.h"
#include "ui/native_pixel_resources.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include "menu/initial_target_scaling.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <mutex>
namespace nova::ui {
namespace {
uint32_t ptr(const void* p) {return reinterpret_cast<uint32_t>(p);}
uint32_t& word(uint32_t p,unsigned offset=0) {return *reinterpret_cast<uint32_t*>(p+offset);}
uint16_t& half(uint32_t p,unsigned offset=0) {return *reinterpret_cast<uint16_t*>(p+offset);}
template<class T> T& at(uint32_t p,unsigned offset,unsigned i) {return reinterpret_cast<T*>(word(p,offset))[i];}
constexpr auto load_name="_ZN14CSpriteManager10LoadSpriteEPKcbbbi";
constexpr auto open_name="_ZN17CCustomFileSystem23customCreateAndOpenFileEPKc";
constexpr auto matching_name="_ZN17CCustomFileSystem16GetMatchingFilesERKSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS2_6memory13E_MEMORY_HINTE0EEEERSt6vectorIS8_SaIS8_EE";
[[noreturn]] void fail() {std::fputs("native-artwork FAIL boundary\n",stderr);std::fflush(stderr);TerminateProcess(GetCurrentProcess(),86);std::abort();}
uint32_t (*open_dispatch)(uint32_t,uint32_t,uint32_t)=nullptr;
__declspec(naked) void open_entry() {
    __asm {
        push ebp
        mov ebp,esp
        push esi
        push edi
        sub esp,12
        and esp,-16
        mov edi,esp
        lea esi,[ebp+8]
        mov ecx,3
        cld
        rep movsd
        call dword ptr [open_dispatch]
        lea esp,[ebp-8]
        pop edi
        pop esi
        pop ebp
        ret 4
    }
}
}
struct NativeArtwork::Impl {
    static Impl* active;
    const ElfImage& image;
    std::unique_ptr<abi::GuestHook> load_hook,open_hook;
    std::array<std::unique_ptr<abi::GuestHook>,4> draw_hooks;
    std::array<uint32_t,4> draw_original{};
    struct Source {
        std::vector<uint8_t> images;
        std::array<std::vector<int32_t>,4> rectangles;
    };
    std::unordered_map<uint32_t,Source> sources;
    std::recursive_mutex mutex;
    uint32_t load_original=0,open_original=0,get_sprite=0,matching=0,string_ctor=0,string_dispose=0;
    uint32_t sprite_ctor=0,sprite_data=0,sprite_destroy=0,release=0,texture_factory=0,allocate=0,deallocate=0;
    uint32_t device=0,factory=0,min_filter=0,mag_filter=0;
    unsigned reports=0;
    Impl(const ElfImage& i):image(i) {
        if(active)throw std::runtime_error("Native artwork already installed");
        const auto fn=[&](const char* name){return abi::require_function(image,name);};
        if(fn(load_name)!=image.address(0x3fe7d0) || fn(open_name)!=image.address(0x3211b0))throw std::runtime_error("Native artwork identity mismatch");
        get_sprite=fn("_ZN14CSpriteManager9GetSpriteEPKc");matching=fn(matching_name);
        string_ctor=fn("_ZNSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS1_6memory13E_MEMORY_HINTE0EEEEC1EPKcRKS6_");
        string_dispose=fn("_ZNSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS1_6memory13E_MEMORY_HINTE0EEEE4_Rep10_M_disposeERKS6_");
        sprite_ctor=fn("_ZN7CSpriteC1Ev");sprite_data=fn("_ZN7CSprite14LoadSpriteDataEN5boost13intrusive_ptrIN6glitch2io9IReadFileEEE");sprite_destroy=fn("_ZN7CSpriteD1Ev");
        release=fn("_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE");
        texture_factory=fn("_ZN17CCustomResFactory10getTextureEPKcb");allocate=fn("_Znaj");deallocate=fn("_ZdaPv");
        device=image.symbol("g_device");factory=image.symbol("g_customResFactory");
        if(!device || !factory)throw std::runtime_error("Native artwork globals missing");
        min_filter=fn("_ZN6glitch5video8ITexture12setMinFilterENS0_21E_TEXTURE_FILTER_TYPEE");mag_filter=fn("_ZN6glitch5video8ITexture12setMagFilterENS0_21E_TEXTURE_FILTER_TYPEE");
        open_hook=std::make_unique<abi::GuestHook>(image,open_name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(reinterpret_cast<void*>(&open_entry)));
        open_original=open_hook->original();
        load_hook=std::make_unique<abi::GuestHook>(image,load_name,std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(reinterpret_cast<void*>(&load)));
        load_original=load_hook->original();
        draw_hooks[0]=std::make_unique<abi::GuestHook>(image,"_ZNK7CSprite18PaintModuleRotatedEifffffjha",std::initializer_list<uint8_t>{0x8d,0x4c,0x24,0x04,0x83,0xe4,0xf0},ptr(reinterpret_cast<void*>(&rotated)));
        draw_hooks[1]=std::make_unique<abi::GuestHook>(image,"_ZN7CSprite16PaintModuleQuadsEP21CQuadsBufferSceneNodeiffjha",std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xe4,0xf0},ptr(reinterpret_cast<void*>(&quads)));
        draw_hooks[2]=std::make_unique<abi::GuestHook>(image,"_ZN7CSprite11GetModuleUVEiRN6glitch4core4rectIfEEja",std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x58},ptr(reinterpret_cast<void*>(&uv)));
        draw_hooks[3]=std::make_unique<abi::GuestHook>(image,"_ZN7CSpriteD1Ev",std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(reinterpret_cast<void*>(&destroy)));
        for(unsigned n=0;n<draw_hooks.size();++n)draw_original[n]=draw_hooks[n]->original();
        active=this;open_dispatch=&open;
    }
    ~Impl(){for(auto& hook:draw_hooks)hook.reset();load_hook.reset();open_hook.reset();open_dispatch=nullptr;active=nullptr;}
    uint32_t filesystem()const{return word(word(device),0x20);}
    void drop_string(uint32_t value){uint8_t allocator=0;abi::invoke(string_dispose,{value-12,ptr(&allocator)});}
    std::vector<std::string> variants(const char* request) {
        const auto name=sprite_family(request);uint32_t native=0;uint8_t allocator=0;
        abi::invoke(string_ctor,{ptr(&native),ptr(name.c_str()),ptr(&allocator)});
        std::array<uint32_t,3> list{};abi::invoke(matching,{filesystem(),ptr(&native),ptr(list.data())});drop_string(native);
        std::vector<std::string> names;
        for(auto p=list[0];p<list[1];p+=4){const auto s=word(p);names.emplace_back(reinterpret_cast<const char*>(s));drop_string(s);}
        if(list[0])abi::invoke(image.symbol("_ZdlPv"),{list[0]});
        std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());return names;
    }
    struct Candidate {
        Impl& owner;std::array<uint32_t,50> object{};bool loaded=false;
        uint32_t address(){return ptr(object.data());}
        Candidate(Impl& o,const char* name):owner(o) {
            abi::invoke(owner.sprite_ctor,{address()});uint32_t file=0;
            const uint32_t args[]{ptr(&file),owner.filesystem(),ptr(name)};
            menu::menu_call_sret4(owner.open_original,args,3);
            if(file){abi::invoke(owner.sprite_data,{address(),ptr(&file)});abi::invoke(owner.release,{file});loaded=true;}
        }
        ~Candidate(){half(address(),0x98)=0;abi::invoke(owner.sprite_destroy,{address()});}
    };
    void enhance(uint32_t sprite,const char* request) {
        if(!sprite || !word(sprite,0x9c))return;
        const auto family=sprite_family(request);
        // The base HUD resource is now physically2048. Retain its normalized
        // coverage before any optional descriptor correspondence is applied.
        for(unsigned m=0;m<half(sprite,0xe);++m) {
            if((half(sprite,0xc)&1) && at<uint8_t>(sprite,0x10,m)!=0xfc)continue;
            const auto index=at<uint8_t>(sprite,0x14,m);if(index>=half(sprite,0x98))continue;
            const auto tex=word(word(sprite,0x9c),index*4);
            if(tex && word(tex,0x14)==2048 && word(tex,0x18)==2048 &&
               word(tex,0xc) && !std::strcmp(reinterpret_cast<const char*>(word(tex,0xc)),"nv_hud"))
                for(unsigned offset:{0x24u,0x28u,0x2cu,0x30u})at<int32_t>(sprite,offset,m)*=2;
        }
        std::vector<DenseModule> best;std::string selected;uint64_t best_samples=0;
        for(const auto& name:variants(request)) {
            if(!name.ends_with(".bsprite") || sprite_family(name)!=family)continue;
            Candidate candidate(*this,name.c_str());if(!candidate.loaded)continue;
            auto plan=sprite_density_plan(sprite,candidate.address());uint64_t samples=0;
            for(const auto& m:plan)samples+=uint64_t(m.width)*m.height;
            if(samples>best_samples){best_samples=samples;selected=name;best=std::move(plan);}
        }
        if(best.empty())return;
        Source original;
        const auto modules=half(sprite,0xe);
        original.images.assign(reinterpret_cast<uint8_t*>(word(sprite,0x14)),reinterpret_cast<uint8_t*>(word(sprite,0x14))+modules);
        for(unsigned n=0;n<4;++n){const auto p=reinterpret_cast<int32_t*>(word(sprite,0x24+n*4));original.rectangles[n].assign(p,p+modules);}
        Candidate dense(*this,selected.c_str());if(!dense.loaded)return;
        const auto old_count=half(sprite,0x98),dense_count=half(dense.address(),0x98);
        // Explicit palette arguments are signed bytes in native draw paths.
        if(unsigned(old_count)+dense_count>128)return;
        std::vector<uint32_t> textures(dense_count,0);std::vector<int> slots(dense_count,-1);
        const auto names=word(dense.address(),0xa0);
        for(const auto& m:best) if(m.image<dense_count && !textures[m.image]) {
            const uint32_t args[]{ptr(&textures[m.image]),factory,word(names,m.image*4),0};
            menu::menu_call_sret4(texture_factory,args,4);
            const auto texture=textures[m.image];if(!texture)continue;
            const auto old=word(word(sprite,0x9c),at<uint8_t>(sprite,0x14,m.module)*4);
            if(old) {const auto state=word(word(old,0x10),0x24);abi::invoke(min_filter,{texture,(state>>14)&7});abi::invoke(mag_filter,{texture,(state>>17)&7});}
        }
        unsigned count=old_count;for(unsigned i=0;i<textures.size();++i)if(textures[i])slots[i]=static_cast<int>(count++);
        if(count==old_count)return;
        const auto allocation=abi::invoke(allocate,{4+count*4});if(!allocation)throw std::bad_alloc();word(allocation)=count;
        const auto old_array=word(sprite,0x9c),array=allocation+4;
        std::memcpy(reinterpret_cast<void*>(array),reinterpret_cast<void*>(old_array),old_count*4);
        for(unsigned i=0;i<textures.size();++i)if(slots[i]>=0)word(array,static_cast<unsigned>(slots[i])*4)=textures[i];
        unsigned changed=0;
        for(const auto& m:best) if(m.image<dense_count && slots[m.image]>=0) {
            const auto texture=textures[m.image];
            if(m.x<0 || m.y<0 || uint64_t(m.x)+m.width>word(texture,0x14) || uint64_t(m.y)+m.height>word(texture,0x18))continue;
            at<uint8_t>(sprite,0x14,m.module)=static_cast<uint8_t>(slots[m.image]);
            at<int32_t>(sprite,0x24,m.module)=m.x;at<int32_t>(sprite,0x28,m.module)=m.y;
            at<int32_t>(sprite,0x2c,m.module)=m.x+m.width;at<int32_t>(sprite,0x30,m.module)=m.y+m.height;++changed;
        }
        // Transfer existing and newly acquired references into a native array.
        // CSprite's ordinary destructor owns every slot, including fallbacks.
        word(sprite,0x9c)=array;half(sprite,0x98)=static_cast<uint16_t>(count);abi::invoke(deallocate,{old_array-4});
        if(changed)sources[sprite]=std::move(original);
        if(reports++<128)std::fprintf(stderr,"native-artwork sprite=%s source=%s dense_modules=%u logical_modules=%u layout=preserved\n",request,selected.c_str(),changed,half(sprite,0xe));
    }
    static uint32_t prepare(uint32_t s,uint32_t m,uint32_t& pal,std::array<uint32_t,50>& view) {
        if(!active || (pal&0xff)==0xff)return s;
        std::lock_guard lock(active->mutex);
        const auto found=active->sources.find(s);
        if(found==active->sources.end() || m>=found->second.images.size())return s;
        const auto& source=found->second;
        if((pal&0xff)==source.images[m]){pal=0xff;return s;}
        // An explicit alternate palette uses the original atlas arrangement.
        // A shallow view avoids touching shared sprite data during drawing.
        std::memcpy(view.data(),reinterpret_cast<void*>(s),sizeof(view));
        view[0x14/4]=ptr(source.images.data());
        for(unsigned n=0;n<4;++n)view[0x24/4+n]=ptr(source.rectangles[n].data());
        return ptr(view.data());
    }
    static uint32_t __cdecl rotated(uint32_t s,uint32_t m,uint32_t a,uint32_t x,uint32_t y,uint32_t ox,uint32_t oy,uint32_t flags,uint32_t alpha,uint32_t pal) noexcept {
        try{std::array<uint32_t,50> view;s=prepare(s,m,pal,view);return abi::invoke(active->draw_original[0],{s,m,a,x,y,ox,oy,flags,alpha,pal});}catch(...){fail();}
    }
    static uint32_t __cdecl quads(uint32_t s,uint32_t node,uint32_t m,uint32_t x,uint32_t y,uint32_t flags,uint32_t alpha,uint32_t pal) noexcept {
        try{std::array<uint32_t,50> view;s=prepare(s,m,pal,view);return abi::invoke(active->draw_original[1],{s,node,m,x,y,flags,alpha,pal});}catch(...){fail();}
    }
    static uint32_t __cdecl uv(uint32_t s,uint32_t m,uint32_t rect,uint32_t flags,uint32_t pal) noexcept {
        try{std::array<uint32_t,50> view;s=prepare(s,m,pal,view);return abi::invoke(active->draw_original[2],{s,m,rect,flags,pal});}catch(...){fail();}
    }
    static uint32_t __cdecl destroy(uint32_t s) noexcept {
        try{std::lock_guard lock(active->mutex);active->sources.erase(s);return abi::invoke(active->draw_original[3],{s});}catch(...){fail();}
    }
    static uint32_t __cdecl load(uint32_t manager,uint32_t name,uint32_t font,uint32_t scale,uint32_t cover,uint32_t id) noexcept {
        try{
            auto& self=*active;
            std::lock_guard lock(self.mutex);
            const auto before=name?abi::invoke(self.get_sprite,{manager,name}):0;
            const uint32_t args[]{manager,name,font,scale,cover,id};
            const auto result=NativePixelResources::load(self.load_original,args);
            if(name && !font && (result&0xff)) {
                const auto sprite=abi::invoke(self.get_sprite,{manager,name});
                if(sprite && sprite!=before)self.enhance(sprite,reinterpret_cast<const char*>(name));
            }
            return result;
        }catch(...){fail();}
    }
    static uint32_t __cdecl open(uint32_t out,uint32_t fs,uint32_t name) noexcept {
        try{
            auto& self=*active;const auto* filename=reinterpret_cast<const char*>(name);
            if(filename && !_stricmp(filename,"nv_hud.etc")) {
                const uint32_t dense[]{out,fs,ptr("NV_hud_2560x1504.etc")};menu::menu_call_sret4(self.open_original,dense,3);
                if(word(out)){std::fputs("native-artwork hud=2048 source=NV_hud_2560x1504.etc\n",stderr);return out;}
            }
            const uint32_t args[]{out,fs,name};menu::menu_call_sret4(self.open_original,args,3);return out;
        }catch(...){fail();}
    }
};
NativeArtwork::Impl* NativeArtwork::Impl::active=nullptr;
NativeArtwork::NativeArtwork(const ElfImage& image):impl_(std::make_unique<Impl>(image)){}
NativeArtwork::~NativeArtwork()=default;
uint32_t NativeArtwork::prepare_draw(uint32_t s,uint32_t m,uint32_t& palette,std::array<uint32_t,50>& view) {return Impl::prepare(s,m,palette,view);}
}
