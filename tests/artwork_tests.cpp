#include "sprite_fixture.h"
#include "ui/native_artwork.h"
#include "ui/sprite_density.h"
#include "menu/initial_target_scaling.h"
#include "loader/diagnostics.h"
#include "ui/native_hud_scale.h"
#include "ui/native_scale.h"
#include "ui/native_menu_fonts.h"
#include "ui/native_pixel_resources.h"
#include "ui/menu_sprite_layout.h"
#include "ui/pause_resources.h"
#include <bit>
#include <map>
#include <sstream>
#include <set>
using namespace sprite_fixture;
using namespace nova;
namespace {
void synthetic() {
    struct Fixture {
        std::array<uint32_t,50> object{};
        std::array<uint8_t,2> images{},types{0xfc,0xfc},counts{2,2};
        std::array<uint8_t,4> flags{};
        std::array<int16_t,2> widths{10,4},heights{5,8};
        std::array<uint16_t,4> ids{0,1,0,1};
        std::array<uint16_t,2> starts{0,2};
        std::array<int32_t,2> x{12,42},y{20,60};
        uint32_t name=ptr("icons_960x640.tga");
        Fixture(){auto s=ptr(object.data());half(s,0xe)=2;half(s,0x34)=4;half(s,0x48)=2;half(s,0x98)=1;
            word(s,0x10)=ptr(types.data());word(s,0x14)=ptr(images.data());word(s,0x1c)=ptr(widths.data());word(s,0x20)=ptr(heights.data());
            word(s,0x24)=ptr(x.data());word(s,0x28)=ptr(y.data());word(s,0x38)=ptr(ids.data());word(s,0x3c)=ptr(flags.data());
            word(s,0x4c)=ptr(counts.data());word(s,0x50)=ptr(starts.data());word(s,0xa0)=ptr(&name);word(s,0xa4)=ptr(&name)+4;
        }
    } base,dense;
    const auto a=ptr(base.object.data()),b=ptr(dense.object.data());
    dense.widths={8,20};dense.heights={16,10};dense.ids={1,0,1,0};dense.x={100,200};dense.name=ptr("ICONS_1720x1280.tga");
    const auto plan=[&]{return ui::sprite_density_plan(a,b);};
    auto mapped=plan();check(mapped.size()==2 && mapped[0].module==0 && mapped[0].x==200 && mapped[0].width==20,"packed atlas reordering follows frame correspondence");
    dense.ids[2]=0;check(plan().size()==1 && plan()[0].module==1,"ambiguous shared module falls back");dense.ids[2]=1;
    dense.heights[1]=11;check(plan().size()==1,"non-proportional artwork is rejected");dense.heights[1]=10;
    dense.name=ptr("unrelated_1720x1280.tga");check(plan().empty(),"unrelated source image rejected");dense.name=ptr("icons_1720x1280.tga");
    dense.flags={16,16,16,16};check(plan().empty(),"nested frame references are not treated as modules");dense.flags={};
    half(b,0xc)=1;dense.types[1]=0xfe;check(plan().size()==1,"procedural module cannot replace artwork");half(b,0xc)=0;
    dense.starts[1]=4;check(plan().empty(),"invalid frame range rejected");dense.starts[1]=2;
    half(b,0x48)=1;check(plan().empty(),"incompatible frame tables rejected");half(b,0x48)=2;
    check(ui::sprite_family("Menu_1720x1280.bsprite")=="menu.bsprite" && ui::sprite_family("Menu_17xx12.bsprite")=="menu_17xx12.bsprite","family parsing is strict");
    check(base.widths==std::array<int16_t,2>{10,4} && base.x==std::array<int32_t,2>{12,42},"policy planning never mutates original geometry or data");
}
Runtime* runtime=nullptr;
uint32_t filesystem=0;
std::map<std::string,std::vector<char>> files;
std::map<std::string,std::array<unsigned,3>> sizes;
std::vector<std::string> opened;
bool missing_dense=false;
std::string lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=char(c+32);return value;}
uint32_t __cdecl file_body(uint32_t out,uint32_t,uint32_t name) {
    const auto key=lower(reinterpret_cast<const char*>(name));word(out)=0;
    if(missing_dense && key=="nv_hud_2560x1504.etc")return out;
    auto found=files.find(key);if(found==files.end())return out;
    opened.push_back(key);const auto& bytes=found->second;
    const uint32_t args[]{out,ptr(bytes.data()),static_cast<uint32_t>(bytes.size()),name,0};
    menu::menu_call_sret4(runtime->fn("_ZN6glitch2io20createMemoryReadFileEPvlPKcb"),args,5);return out;
}
__declspec(naked) void file_entry(){__asm{
    push ebp
    mov ebp,esp
    push esi
    push edi
    sub esp,12
    and esp,-16
    lea esi,[ebp+8]
    mov edi,esp
    mov ecx,3
    cld
    rep movsd
    call file_body
    lea esp,[ebp-8]
    pop edi
    pop esi
    pop ebp
    ret 4
}}
struct Texture {
    std::string name,file;
    std::array<uint32_t,20> state{},object{};
};
std::map<std::string,std::unique_ptr<Texture>> textures;
uint32_t __cdecl texture_body(uint32_t out,uint32_t,uint32_t filename,uint32_t cache) {
    const std::string key=reinterpret_cast<const char*>(cache);word(out)=0;
    if(!textures.contains(key)) {
        auto name=lower(reinterpret_cast<const char*>(filename));name=std::filesystem::path(name).replace_extension(".etc").string();
        uint32_t file=0;const uint32_t args[]{ptr(&file),filesystem,ptr(name.c_str())};
        menu::menu_call_sret4(runtime->fn("_ZN17CCustomFileSystem23customCreateAndOpenFileEPKc"),args,3);
        if(!file)return out;
        const std::string chosen=lower(reinterpret_cast<const char*>(word(file,0x20)));
        abi::invoke(runtime->fn("_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE"),{file});
        auto dimension=sizes.find(chosen);check(dimension!=sizes.end(),"selected texture has measured original header");
        auto texture=std::make_unique<Texture>();texture->name=key;texture->file=chosen;
        texture->object[1]=100;texture->object[3]=ptr(texture->name.c_str());texture->object[4]=ptr(texture->state.data());
        texture->object[5]=dimension->second[0];texture->object[6]=dimension->second[1];
        texture->state[9]=1|(1<<14)|(1<<17);reinterpret_cast<uint8_t*>(texture->state.data())[0x2f]=static_cast<uint8_t>(dimension->second[2]);
        textures[key]=std::move(texture);
    }
    auto& texture=*textures.at(key);++texture.object[1];word(out)=ptr(texture.object.data());return out;
}
__declspec(naked) void texture_entry(){__asm{
    push ebp
    mov ebp,esp
    push esi
    push edi
    sub esp,16
    and esp,-16
    lea esi,[ebp+8]
    mov edi,esp
    mov ecx,4
    cld
    rep movsd
    call texture_body
    lea esp,[ebp-8]
    pop edi
    pop esi
    pop ebp
    ret 4
}}
struct Geometry {
    std::vector<uint8_t> logical;
    std::vector<std::array<int16_t,4>> bounds;
    std::vector<std::array<float,7>> quads;
    bool operator==(const Geometry&)const=default;
};
std::array<float,7> emitted;
std::array<float,4> emitted_uv;
uint32_t bound_texture=0;
uint32_t __cdecl material_locked(uint32_t){return 0;}
void __cdecl bind_texture(uint32_t,uint32_t ref){bound_texture=word(ref);}
void __cdecl append_quad(uint32_t,uint32_t rect,uint32_t uv,uint32_t origin){
    std::memcpy(emitted.data(),reinterpret_cast<void*>(rect),16);
    std::memcpy(emitted.data()+4,reinterpret_cast<void*>(origin),12);
    std::memcpy(emitted_uv.data(),reinterpret_cast<void*>(uv),16);
}
Geometry geometry(uint32_t s) {
    Geometry g;
    const auto copy=[&](uint32_t p,size_t n){const auto* bytes=reinterpret_cast<const uint8_t*>(p);g.logical.insert(g.logical.end(),bytes,bytes+n);};
    copy(s+0xb0,16);copy(word(s,0x1c),half(s,0xe)*2);copy(word(s,0x20),half(s,0xe)*2);
    copy(word(s,0x38),half(s,0x34)*2);copy(word(s,0x3c),half(s,0x34));copy(word(s,0x40),half(s,0x34)*2);copy(word(s,0x44),half(s,0x34)*2);
    for(unsigned f=0;f<half(s,0x48);++f)for(unsigned flags=0;flags<4;++flags) {
        std::array<int16_t,4> rect{};
        abi::invoke(runtime->fn("_ZNK7CSprite12GetFrameRectERN6glitch4core4rectIsEEiiijiib"),{s,ptr(rect.data()),f,0,0,flags,0,0,0});g.bounds.push_back(rect);
    }
    std::array<uint32_t,65> vtable{};uint32_t node=ptr(vtable.data());
    vtable[62]=ptr(reinterpret_cast<void*>(&material_locked));vtable[63]=ptr(reinterpret_cast<void*>(&bind_texture));vtable[64]=ptr(reinterpret_cast<void*>(&append_quad));
    for(unsigned m=0;m<half(s,0xe);++m) {
        if((half(s,0xc)&1) && reinterpret_cast<uint8_t*>(word(s,0x10))[m]!=0xfc)continue;
        const auto index=reinterpret_cast<uint8_t*>(word(s,0x14))[m];
        const auto tex=word(word(s,0x9c),index*4);if(!tex)continue;
        for(unsigned flags=0;flags<4;++flags){
            abi::invoke(runtime->fn("_ZN7CSprite16PaintModuleQuadsEP21CQuadsBufferSceneNodeiffjha"),{s,ptr(&node),m,std::bit_cast<uint32_t>(12.25f),std::bit_cast<uint32_t>(33.5f),flags,255,255});
            check(bound_texture==tex,"native quad binds the mapped texture");g.quads.push_back(emitted);
            std::array<float,4> uv{};
            check(abi::invoke_bool(runtime->fn("_ZN7CSprite11GetModuleUVEiRN6glitch4core4rectIfEEja"),{s,m,ptr(uv.data()),flags,255}),"native module UV succeeds");
            check(uv==emitted_uv,"native UV lookup and actual quad agree including flips");
            for(unsigned n=0;n<4;++n){const auto src=n^((n&1)?((flags&2)?2:0):((flags&1)?2:0));
                const float expected=float(reinterpret_cast<int32_t*>(word(s,0x24+src*4))[m])/word(tex,(n&1)?0x18:0x14);
                check(uv[n]==expected,"native UV uses dense source rectangle and real texture dimensions");}
        }
    }
    return g;
}
void clear_textures(){for(const auto& [name,t]:textures)check(t->object[1]==100,"native texture references balance after manager teardown");textures.clear();}
void integration(Runtime& r,const std::filesystem::path& root) {
    runtime=&r;
    for(const auto& file:std::filesystem::directory_iterator(root/"sprites"))if(file.path().extension()==".bsprite") {
        std::ifstream input(file.path(),std::ios::binary);files[lower(file.path().filename().string())]={(std::istreambuf_iterator<char>(input)),{}};
    }
    std::ifstream metadata(root/"texture-metadata.txt");std::string line;
    while(std::getline(metadata,line)) {std::istringstream stream(line);std::string name,a,b,c;
        std::getline(stream,name,'|');std::getline(stream,a,'|');std::getline(stream,b,'|');std::getline(stream,c,'|');
        sizes[name]={static_cast<unsigned>(std::stoul(a)),static_cast<unsigned>(std::stoul(b)),static_cast<unsigned>(std::stoul(c))};files[name]=std::vector<char>(52,0);
    }
    std::array<uint32_t,9> device{};std::array<uint32_t,81> driver{};std::array<uint32_t,21> fs{};std::array<uint32_t,12> pak{};std::array<uint32_t,10> fs_vtable{};
    std::vector<std::array<uint32_t,4>> records;for(const auto& [name,data]:files)records.push_back({0,static_cast<uint32_t>(data.size()),ptr(name.c_str()),0});
    pak[8]=ptr(records.data());pak[9]=pak[8]+static_cast<uint32_t>(records.size()*16);uint32_t pak_ref=ptr(pak.data());
    filesystem=ptr(fs.data());fs[0]=ptr(fs_vtable.data());fs[13]=ptr(&pak_ref);fs[14]=fs[13]+4;fs[19]=4;
    fs_vtable[3]=r.fn("_ZN17CCustomFileSystem17createAndOpenFileEPKc");device[8]=filesystem;device[2]=ptr(driver.data());driver[80]=123;
    word(r.image->symbol("g_device"))=ptr(device.data());
    abi::GuestHook open(*r.image,"_ZN6glitch2io14CGlfFileSystem17createAndOpenFileEPKc",{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(reinterpret_cast<void*>(&file_entry)));
    abi::GuestHook texture(*r.image,"_ZN6glitch5video15CTextureManager10getTextureEPKcS3_",{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(reinterpret_cast<void*>(&texture_entry)));
    const std::array<const char*,6> names{"hud3d.bsprite","menu_elements_nova3.bsprite","controls.bsprite","chatboard.bsprite","upgrades.bsprite","game_actions_icons.bsprite"};
    ui::RequestedScale requested;
    ui::NativeScale menu_scale(*r.image,requested);
    ui::NativeHudScale hud_scale(*r.image,requested);ui::NativeMenuFonts menu_fonts(*r.image);
    std::array<uint32_t,9> pause_target{};uint32_t pause_target_ref=ptr(pause_target.data());
    driver[0x114/4]=ptr(&pause_target_ref);driver[0x118/4]=ptr(&pause_target_ref)+4;
    const auto pause_site=r.image->address(0x4960d1,5);
    check(*reinterpret_cast<uint8_t*>(pause_site)==0xe8,"pause metrics call remains a direct CALL");
    const auto pause_metrics=pause_site+5+word(pause_site+1);
    // Bind this regression to the resource actually requested by Create,
    // including its original PIC string operand, instead of guessing a family.
    const std::array<uint8_t,6> resource_operand{0x8d,0x93,0x6a,0x85,0xd4,0xff};
    check(!std::memcmp(reinterpret_cast<void*>(r.image->address(0x495ef3,6)),resource_operand.data(),6),
          "pause Create's resource operand matches the inspected original");
    const auto pause_name=reinterpret_cast<const char*>(r.image->address(0xe868aa,17));
    check(!std::strcmp(pause_name,"Chatboard.bsprite"),"pause test uses the actual original Create resource");
    std::map<std::array<unsigned,2>,std::map<std::string,std::array<uint32_t,4>>> native_states;
    std::map<std::array<unsigned,2>,std::map<std::string,Geometry>> native_geometry;
    constexpr std::array<unsigned,4> pause_frames{187,188,197,198};
    std::array<std::array<int16_t,4>,4> pause_reference{};
    bool reproduced_pause_overflow=false,reproduced_pause_shrink=false;
    for(auto extent:std::array<std::array<unsigned,2>,8>{{{640,360},{960,540},{1280,720},{1600,900},{1920,1080},{1920,1200},{2560,1440},{3840,2160}}}) {
        std::map<std::string,Geometry> baseline;
        std::map<std::string,std::array<uint32_t,4>> globals;
        struct Palette {uint32_t module,index;std::array<float,4> uv;};std::vector<Palette> palettes;
        for(bool enabled:{false,true}) {
            clear_textures();std::array<uint32_t,4> manager{};
            word(r.image->symbol("g_ScreenW"))=extent[0];word(r.image->symbol("g_ScreenH"))=extent[1];word(r.image->address(0x115679c,4))=0;
            *reinterpret_cast<uint8_t*>(r.image->symbol("isCheckDataGoogleDeal"))=0;
            abi::invoke(r.fn("_ZN14CSpriteManagerC1Ev"),{ptr(manager.data())});
            std::unique_ptr<ui::NativeArtwork> policy;if(enabled)policy=std::make_unique<ui::NativeArtwork>(*r.image);
            uint32_t material=0;
            const uint32_t material_args[]{ptr(&material),r.image->symbol("g_customResFactory"),ptr("nv_hud.tga"),0};
            menu::menu_call_sret4(r.fn("_ZN17CCustomResFactory10getTextureEPKcb"),material_args,4);
            check(material!=0,"direct health/ammo material loads");
            abi::invoke(r.fn("_ZN5boost13intrusive_ptrIN6glitch5video8ITextureEED1Ev"),{ptr(&material)});
            for(const auto* name:names) {
                std::fprintf(stderr,"artwork fixture extent=%ux%u enabled=%u sprite=%s\n",extent[0],extent[1],unsigned(enabled),name);
                check(abi::invoke_bool(r.fn("_ZN14CSpriteManager10LoadSpriteEPKcbbbi"),{ptr(manager.data()),ptr(name),0,1,0,0xffffffff}),"original sprite loads");
                const auto sprite=abi::invoke(r.fn("_ZN14CSpriteManager9GetSpriteEPKc"),{ptr(manager.data()),ptr(name)});check(sprite!=0,"original sprite published");
                const std::array<uint32_t,4> state{word(r.image->symbol("g_ScreenW")),word(r.image->symbol("g_ScreenH")),word(r.image->address(0x115679c,4)),*reinterpret_cast<uint8_t*>(r.image->symbol("isCheckDataGoogleDeal"))};
                if(!enabled)globals[name]=state;else check(globals[name]==state,"native screen metrics and device detection side effects unchanged");
                if(!enabled)native_states[extent][name]=state;
                auto measured=geometry(sprite);
                if(!enabled)native_geometry[extent][name]=measured;
                if(std::string(name)=="menu_elements_nova3.bsprite") {
                    const auto uv_fn=r.fn("_ZN7CSprite11GetModuleUVEiRN6glitch4core4rectIfEEja");
                    if(!enabled)for(unsigned m=0;m<half(sprite,0xe);++m){
                        if((half(sprite,0xc)&1) && reinterpret_cast<uint8_t*>(word(sprite,0x10))[m]!=0xfc)continue;
                        const auto original=reinterpret_cast<uint8_t*>(word(sprite,0x14))[m];
                        const uint32_t alternate=(original+1)%half(sprite,0x98);Palette p{m,alternate,{}};
                        abi::invoke(uv_fn,{sprite,m,ptr(p.uv.data()),0,alternate});palettes.push_back(p);
                    }else for(const auto& p:palettes){
                        std::array<float,4> uv{};abi::invoke(uv_fn,{sprite,p.module,ptr(uv.data()),0,p.index});
                        check(uv==p.uv,"explicit alternate palettes retain original atlas coordinates");
                    }
                }
                if(!enabled)baseline[name]=std::move(measured);else {
                    check(measured==baseline[name],"all original module metrics, offsets, frame bounds and scales unchanged");
                    check(abi::invoke_bool(r.fn("_ZN14CSpriteManager10LoadSpriteEPKcbbbi"),{ptr(manager.data()),ptr(name),0,1,0,0xffffffff}),"cache load preserves boolean result");
                    const auto again=abi::invoke(r.fn("_ZN14CSpriteManager9GetSpriteEPKc"),{ptr(manager.data()),ptr(name)});
                    check(again==sprite && geometry(sprite)==measured,"cache hit is idempotent");
                }
            }
            if(enabled) {
                check(textures.contains("nv_hud") && textures.at("nv_hud")->object[5]==2048 && textures.at("nv_hud")->file=="nv_hud_2560x1504.etc","base HUD material cache owns actual2048 source");
                check(textures.contains("menu_elements_nova3_1720x1280"),"menu HD atlas loaded at every PC resolution");
                const auto sprite=abi::invoke(r.fn("_ZN14CSpriteManager9GetSpriteEPKc"),{ptr(manager.data()),ptr(pause_name)});
                check(sprite && half(sprite,0x48)==264,"actual pause resource has the original264 frames");
                // The isolated fixture skips game initializers. Pause layout
                // runs with native frame-offset scaling enabled in the game.
                auto& scaling=*reinterpret_cast<uint8_t*>(r.image->symbol("_ZN7CSprite13m_bUseScalingE"));
                const auto saved_scaling=scaling;scaling=1;
                const auto compute=r.fn("_ZN7CSprite17ComputeFrameRectsEv");
                for(const auto* icon_name:{"upgrades.bsprite","game_actions_icons.bsprite"}) {
                const auto upgrades=abi::invoke(r.fn("_ZN14CSpriteManager9GetSpriteEPKc"),{ptr(manager.data()),ptr(icon_name)});
                const auto original_icons=geometry(upgrades);
                const auto icons_texture=word(upgrades,0x9c);
                // Real shared icon resource: verify that it is independent of
                // load resolution during pause and fully restored afterwards.
                {
                    ui::PauseResources metrics(compute);metrics.icons(upgrades);
                    check(word(upgrades,0xb0)==std::bit_cast<uint32_t>(*ui::pause_sprite_scale(upgrades,1280,720)),"power carousel uses canonical pause metrics");
                    check(word(upgrades,0x9c)==icons_texture,"pause power metrics preserve HD texture ownership");
                    const auto stable=geometry(upgrades);
                    abi::invoke(compute,{upgrades});
                    check(geometry(upgrades)==stable,"power icon nested bounds converge");
                }
                check(geometry(upgrades)==original_icons,"shared power icon metrics restored after pause");
                }
                // Execute the original TTF measurement against its real vtable,
                // with a retained font of a different raster size.
                std::array<uint32_t,64> font{},ttf{};
                font[0]=r.image->symbol("_ZTV11CTTFFontSpr")+8;
                font[0xc8/4]=ptr(ttf.data());
                reinterpret_cast<uint8_t*>(font.data())[0xc5]=1;
                font[0xb0/4]=font[0xb4/4]=std::bit_cast<uint32_t>(1.f);
                const auto line_height=r.fn("_ZN11CTTFFontSpr13GetLineHeightEv");
                for(unsigned raster:{18u,24u,37u,74u}) {
                    ttf[0x54/4]=raster;
                    {
                        ui::PauseResources metrics(compute);metrics.font(ptr(font.data()),24);
                        const auto measured=abi::invoke(line_height,{ptr(font.data())});
                        check(measured>=23 && measured<=24,"native TTF height stays canonical across cached raster sizes");
                    }
                    check(abi::invoke(line_height,{ptr(font.data())})==raster,"native gameplay font size restored after pause");
                }
                const auto rect_fn=r.fn("_ZNK7CSprite12GetFrameRectERN6glitch4core4rectIsEEiiijiib");
                const auto bounds=[&](unsigned frame) {
                    std::array<int16_t,4> rect{};
                    abi::invoke(rect_fn,{sprite,ptr(rect.data()),frame,0,0,0,0,0,0});return rect;
                };
                const auto set_scale=[&](float scale) {
                    word(sprite,0xb0)=word(sprite,0xb4)=std::bit_cast<uint32_t>(scale);
                    abi::invoke(compute,{sprite});
                };
                const auto texture_array=word(sprite,0x9c),image_names=word(sprite,0xa0);
                const auto logical_name=reinterpret_cast<const char*>(word(image_names));
                const bool dense_descriptor=!std::strcmp(logical_name,"chatboard_1720x1280.tga");
                check(dense_descriptor || !std::strcmp(logical_name,"chatboard_960x640.tga"),"actual pause descriptor admitted");
                // The two native Chatboard variants have different artwork
                // offsets. Use each original descriptor's authored bounds,
                // not the unrelated front-end menu or the other variant.
                set_scale(1);
                for(unsigned pass=1;pass<4;++pass)abi::invoke(compute,{sprite});
                for(unsigned i=0;i<pause_frames.size();++i)pause_reference[i]=bounds(pause_frames[i]);
                const auto before_globals=native_states.at(extent).at("chatboard.bsprite");
                for(auto display:std::array<std::array<unsigned,2>,6>{{{1280,720},{1920,1080},{3840,2160},{960,540},{1920,1080},{1280,720}}}) {
                    const float fit=std::min(display[0]/960.f,display[1]/640.f);
                    // The live failing process retained the low Chatboard
                    // descriptor while another load had set the device flag.
                    // Reproduce that flag independently of this load's extent.
                    const bool flag=true;
                    // Original Create's width-only condition, with its already
                    // loaded sprite retained across the display transition.
                    set_scale(fit*((display[0]==1920 && flag) ? .5f : 1.f));
                    const auto broken=bounds(197);
                    check(bool(ui::pause_sprite_scale(sprite,display[0],display[1])),"known cached menu descriptor admitted");
                    pause_target[7]=display[0];pause_target[8]=display[1];
                    // Exercise the installed Create call target and actual
                    // native frame computation, not only a mirrored formula.
                    abi::invoke(pause_metrics,{sprite});
                    const auto corrected=bounds(197);
                    if(dense_descriptor && display[0]==1280) {
                        check(broken[2]-broken[0]>=1.9f*(corrected[2]-corrected[0]),"original post-resize pause frame reproduces doubled geometry");
                        reproduced_pause_overflow=true;
                    }
                    if(!dense_descriptor && display[0]==1920) {
                        check(corrected[2]-corrected[0]>=1.9f*(broken[2]-broken[0]),"live low Chatboard with1920 flag reproduces half-size artwork");
                        reproduced_pause_shrink=true;
                    }
                    const float authored_scale=fit/(dense_descriptor ? 2.f : 1.f);
                    for(unsigned i=0;i<pause_frames.size();++i) {
                        const auto actual=bounds(pause_frames[i]);
                        for(unsigned axis=0;axis<4;++axis) {
                            if(std::abs(actual[axis]-pause_reference[i][axis]*authored_scale)>2.f*fit+1)
                                std::fprintf(stderr,"pause load=%ux%u display=%ux%u frame=%u axis=%u actual=%d reference=%d fit=%f scale=%f name=%s\n",
                                    extent[0],extent[1],display[0],display[1],pause_frames[i],axis,actual[axis],pause_reference[i][axis],fit,
                                    std::bit_cast<float>(word(sprite,0xb0)),reinterpret_cast<const char*>(word(image_names)));
                            check(std::abs(actual[axis]-pause_reference[i][axis]*authored_scale)<=2.f*fit+1,
                                  "pause frame geometry fits the authored canvas regardless of cached descriptor or display resolution");
                        }
                    }
                    const auto cache=word(sprite,0x5c);const auto size=half(sprite,0x48)*8;
                    std::vector<uint8_t> stable(reinterpret_cast<uint8_t*>(cache),reinterpret_cast<uint8_t*>(cache)+size);
                    abi::invoke(compute,{sprite});
                    check(!std::memcmp(stable.data(),reinterpret_cast<void*>(cache),size),"all nested frame bounds converge before controls measure them");
                    check(word(sprite,0x9c)==texture_array && word(sprite,0xa0)==image_names,"pause reflow preserves HD textures and descriptor identity");
                    check(word(r.image->symbol("g_ScreenW"))==before_globals[0] && word(r.image->symbol("g_ScreenH"))==before_globals[1] &&
                          word(r.image->address(0x115679c,4))==before_globals[2] && *reinterpret_cast<uint8_t*>(r.image->symbol("isCheckDataGoogleDeal"))==before_globals[3],
                          "pause metrics do not spoof display state or device detection");
                }
                scaling=saved_scaling;
            }
            abi::invoke(r.fn("_ZN14CSpriteManagerD1Ev"),{ptr(manager.data())});policy.reset();clear_textures();
            check(allocated==0,"complete native sprite and file lifecycle balances allocations");
        }
    }
    check(reproduced_pause_overflow,"original cached high-density pause regression exercised");
    check(reproduced_pause_shrink,"observed low-descriptor/HD-device mismatch regression exercised");
    // Only HUD families use one logical 720p baseline on a fresh start.
    // Menu/chat resources must match the native load at the actual resolution.
    // The separate HD-only comparison above must continue passing unchanged.
    {
        ui::NativePixelResources pixels(*r.image);
        for(auto extent:std::array<std::array<unsigned,2>,6>{{{1920,1080},{1280,720},{3840,2160},{640,360},{2560,1440},{1920,1080}}}) {
            clear_textures();std::array<uint32_t,4> manager{};
            word(r.image->symbol("g_ScreenW"))=extent[0];word(r.image->symbol("g_ScreenH"))=extent[1];
            word(r.image->address(0x115679c,4))=0;
            *reinterpret_cast<uint8_t*>(r.image->symbol("isCheckDataGoogleDeal"))=0;
            abi::invoke(r.fn("_ZN14CSpriteManagerC1Ev"),{ptr(manager.data())});
            {
                ui::NativeArtwork artwork(*r.image);
                uint32_t material=0;
                const uint32_t material_args[]{ptr(&material),r.image->symbol("g_customResFactory"),ptr("nv_hud.tga"),0};
                menu::menu_call_sret4(r.fn("_ZN17CCustomResFactory10getTextureEPKcb"),material_args,4);
                check(material!=0,"pixel policy direct HUD material loads");
                abi::invoke(r.fn("_ZN5boost13intrusive_ptrIN6glitch5video8ITextureEED1Ev"),{ptr(&material)});
                for(const auto* name:names) {
                    check(abi::invoke_bool(r.fn("_ZN14CSpriteManager10LoadSpriteEPKcbbbi"),{ptr(manager.data()),ptr(name),0,1,0,0xffffffff}),"pixel policy original load succeeds");
                    check(word(r.image->symbol("g_ScreenW"))==extent[0] && word(r.image->symbol("g_ScreenH"))==extent[1],"logical load restores actual display dimensions");
                    const std::array<uint32_t,4> state{word(r.image->symbol("g_ScreenW")),word(r.image->symbol("g_ScreenH")),word(r.image->address(0x115679c,4)),*reinterpret_cast<uint8_t*>(r.image->symbol("isCheckDataGoogleDeal"))};
                    const auto expected_state=native_states.at(extent).at(name);
                    if(state!=expected_state)std::fprintf(stderr,"pixel state %ux%u %s actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",extent[0],extent[1],name,state[0],state[1],state[2],state[3],expected_state[0],expected_state[1],expected_state[2],expected_state[3]);
                    check(state==expected_state,"pixel resources preserve actual-resolution device detection and manager counters");
                    const auto sprite=abi::invoke(r.fn("_ZN14CSpriteManager9GetSpriteEPKc"),{ptr(manager.data()),ptr(name)});
                    const auto measured=geometry(sprite);
                    const bool fixed=std::string(name)=="hud3d.bsprite" || std::string(name)=="controls.bsprite";
                    check(measured==native_geometry.at(fixed ? std::array<unsigned,2>{1280,720} : extent).at(name),
                          "HUD geometry matches720p; shared menu and information resources retain native geometry");
                }
                check(textures.contains("nv_hud") && textures.at("nv_hud")->object[5]==2048,"pixel normalization retains2048 HUD artwork");
                check(textures.contains("menu_elements_nova3_1720x1280"),"pixel normalization retains menu HD artwork");
                abi::invoke(r.fn("_ZN14CSpriteManagerD1Ev"),{ptr(manager.data())});
            }
            clear_textures();check(allocated==0,"pixel resource lifecycle balances allocations");
        }
    }
    // Missing optional dense source must fall back through the original loader.
    {ui::NativeArtwork policy(*r.image);missing_dense=true;uint32_t file=0;
        const uint32_t args[]{ptr(&file),filesystem,ptr("nv_hud.etc")};
        menu::menu_call_sret4(r.fn("_ZN17CCustomFileSystem23customCreateAndOpenFileEPKc"),args,3);
        check(file && !std::strcmp(reinterpret_cast<char*>(word(file,0x20)),"nv_hud.etc"),"missing dense HUD falls back to base source");
        abi::invoke(r.fn("_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE"),{file});missing_dense=false;}
    check(allocated==0,"fallback file lifecycle balanced");
}
}
int main(int argc,char** argv) {
    try{synthetic();if(argc==2 && std::string(argv[1])=="--synthetic"){std::puts("HD correspondence policy PASS");return 0;}
        if(argc!=3 || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(std::filesystem::path(argv[2])/"sprites") || !std::filesystem::exists(std::filesystem::path(argv[2])/"texture-metadata.txt"))return 77;
        Runtime r(argv[1]);Diagnostics diagnostics(*r.image);
        std::vector<std::pair<uint32_t,std::vector<uint8_t>>> saved;
        for(const auto& segment:r.image->segments())if(segment.flags&1){const auto p=r.image->address(segment.vaddr,segment.filesz);saved.push_back({p,{reinterpret_cast<uint8_t*>(p),reinterpret_cast<uint8_t*>(p)+segment.filesz}});}
        integration(r,argv[2]);
        for(const auto& segment:saved)check(!std::memcmp(reinterpret_cast<void*>(segment.first),segment.second.data(),segment.second.size()),"all executable bytes restored after combined hooks");
        std::puts("Native HD artwork: selection, geometry, material source, cache and lifetime PASS");return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"Native HD artwork FAIL %s\n",e.what());return 1;}
}
