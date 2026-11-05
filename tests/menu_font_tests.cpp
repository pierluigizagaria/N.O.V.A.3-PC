#include "ui/native_menu_fonts.h"
#include "ui/scale.h"
#include "ui/menu_text_trace.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include "audio/opensl.h"
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace nova;
namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
uint32_t addr(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t& word(uint32_t p,unsigned offset=0) { return *reinterpret_cast<uint32_t*>(p+offset); }
using Glyph=std::array<uint32_t,14>;
constexpr uint64_t result=0x12345678abcdef01ull;
std::array<uint32_t,40> font{};
std::array<uint32_t,3> face{};
uint32_t face_ref=0;
unsigned cached=0,destroyed=0,cleared=0,size_restored=0,slot_restored=0;
bool fallback=false;
std::array<uint32_t,4> captured_source{},captured_destination{};
uint32_t captured_texture=0;
uint64_t __cdecl construct(uint32_t p) { std::memset(reinterpret_cast<void*>(p),0,56); return result; }
uint64_t __cdecl cache(uint32_t g,uint32_t index,uint32_t f,uint32_t driver,uint32_t atlas,uint32_t size,uint32_t border,uint32_t color,uint32_t extra,uint32_t bold) {
    check(index==42 && f==addr(&face_ref) && driver==123 && atlas==456 && color==0xffaabbcc && !bold,"complete cache ABI");
    check((size==20 && border==1 && extra==2)||(size==40 && border==2 && extra==4),"raster size and decoration density");
    ++cached;
    const Glyph glyph{18,2,0,0,0,0,0,0,fallback?10u:0u,size,10,20,10+size,20+size};
    std::memcpy(reinterpret_cast<void*>(g),glyph.data(),56); return result;
}
uint64_t __cdecl destroy(uint32_t) { ++destroyed; return result; }
uint64_t __cdecl clear(uint32_t a) { check(a==456 || a==789,"atlas clear receiver"); ++cleared; return result; }
uint32_t __cdecl set_size(uint32_t f,uint32_t x,uint32_t y) { check(f==321 && !x && y==20,"restore face size"); ++size_restored; return 0; }
uint32_t __cdecl load(uint32_t f,uint32_t i,uint32_t flags) { check(f==321 && i==42 && flags==0x100e,"restore active glyph slot"); ++slot_restored; return 0; }
uint64_t __cdecl rectangle(uint32_t driver,uint32_t dest,uint32_t source,uint32_t colors,uint32_t clip) {
    check(driver==123 && colors==98 && clip==99,"native clip and color pointers");
    std::memcpy(captured_source.data(),reinterpret_cast<void*>(source),16);
    std::memcpy(captured_destination.data(),reinterpret_cast<void*>(dest),16); return result;
}
uint64_t __cdecl draw(uint32_t f,uint32_t g,uint32_t pos,uint32_t clip,uint32_t color,uint32_t fade,uint32_t scale) {
    check(f==addr(font.data()) && pos==77 && clip==99 && color==98 && fade==1 && scale==std::bit_cast<uint32_t>(1.5f),"native drawing arguments");
    check(word(g)==18 && word(g,4)==2 && word(g,0x30)-word(g,0x28)==20,"native bearings and logical bounds preserved");
    captured_texture=word(g,0x24);
    const std::array<uint32_t,4> destination{31,47,61,77};
    return ui::NativeMenuFonts::fixture_rectangle({123,addr(destination.data()),g+0x28,color,clip});
}
void synthetic() {
    face[2]=321; face_ref=addr(face.data()); font[2]=123;
    ui::NativeMenuFonts::Bindings bindings{{addr(reinterpret_cast<void*>(&cache)),addr(reinterpret_cast<void*>(&draw)),addr(reinterpret_cast<void*>(&draw)),
        addr(reinterpret_cast<void*>(&rectangle)),addr(reinterpret_cast<void*>(&destroy)),addr(reinterpret_cast<void*>(&destroy)),addr(reinterpret_cast<void*>(&clear))},
        addr(reinterpret_cast<void*>(&construct)),addr(reinterpret_cast<void*>(&set_size)),addr(reinterpret_cast<void*>(&load))};
    Glyph glyph{};
    const std::array<uint32_t,10> args{addr(glyph.data()),42,addr(&face_ref),123,456,20,1,0xffaabbcc,2,0};
    const std::array<uint32_t,7> drawing{addr(font.data()),addr(glyph.data()),77,99,98,1,std::bit_cast<uint32_t>(1.5f)};
    {
        ui::NativeMenuFonts quality(bindings);
        check(ui::NativeMenuFonts::fixture_cache(args)==result && cached==2 && size_restored==1 && slot_restored==1,"base return and shared face restoration");
        const auto saved=glyph;
        for(bool smooth:{false,true}) for(bool menu:{false,true}) {
            ui::menu_draw_depth=menu?1:0;
            check(ui::NativeMenuFonts::fixture_draw(drawing,smooth)==result,"draw full return preserved");
            check(captured_texture==(menu?40u:20u),"only menus select supplemental texture");
            check(captured_source==std::array<uint32_t,4>{10,20,menu?50u:30u,menu?60u:40u},"only raster source changes");
            check(captured_destination==std::array<uint32_t,4>{31,47,61,77} && glyph==saved,"logical rectangle and original glyph immutable");
        }
        ui::menu_draw_depth=0;
        ui::NativeMenuFonts::fixture_cache(args); check(destroyed==1 && cached==4,"recache releases previous supplemental ownership");
        ui::NativeMenuFonts::fixture_clear(789); check(destroyed==1,"unrelated atlas retained");
        ui::NativeMenuFonts::fixture_clear(456); check(destroyed==2,"atlas invalidation releases density cache");
        ui::menu_draw_depth=1; ui::NativeMenuFonts::fixture_draw(drawing); check(captured_texture==20,"invalidated cache falls back"); ui::menu_draw_depth=0;
        ui::NativeMenuFonts::fixture_cache(args);
        check(ui::NativeMenuFonts::fixture_destroy(addr(glyph.data()),true)==result && destroyed==4,"D2 releases supplemental then base");
        fallback=true; const auto before=cached; ui::NativeMenuFonts::fixture_cache(args); check(cached==before+1,"bitmap fallback is not rerasterized"); fallback=false;
        ui::NativeMenuFonts::fixture_cache(args);
    }
    check(destroyed==5,"owner teardown releases remaining supplemental glyphs");
}

std::array<std::array<uint32_t,2>,1024> texture_refs{};
struct Raster { unsigned width,height; std::vector<uint8_t> pixels; };
std::vector<Raster> rasters;
unsigned allocated=0;
uint32_t __cdecl allocate(uint32_t size,uint32_t) { ++allocated; return addr(std::malloc(size)); }
void __cdecl release(uint32_t p) { if(p) { --allocated; std::free(reinterpret_cast<void*>(p)); } }
void __cdecl add_glyph(uint32_t,uint32_t dimensions,uint32_t pixels,uint32_t output) {
    const auto w=word(dimensions),h=word(dimensions,4);
    check(w>0 && h>0 && w<512 && h<512,"native atlas upload bounds");
    const auto id=rasters.size(); check(id<texture_refs.size(),"fixture texture capacity");
    texture_refs[id][1]=101;
    word(output)=addr(texture_refs[id].data()); word(output,4)=3; word(output,8)=5; word(output,12)=3+w; word(output,16)=5+h;
    const auto* data=reinterpret_cast<const uint8_t*>(pixels);
    rasters.push_back({w,h,{data,data+size_t(w)*h*4}});
}
__declspec(naked) void texture_bind() {
    __asm {
        mov eax, [esp+4]
        mov dword ptr [eax], 0
        mov ecx, [esp+12]
        mov ecx, [ecx]
        mov captured_texture, ecx
        ret 4
    }
}
uint64_t __cdecl capture_rectangle(uint32_t,uint32_t dest,uint32_t source,uint32_t,uint32_t) {
    std::memcpy(captured_source.data(),reinterpret_cast<void*>(source),16);
    std::memcpy(captured_destination.data(),reinterpret_cast<void*>(dest),16); return 0;
}
uint64_t __cdecl dispatch_rectangle(uint32_t d,uint32_t p,uint32_t s,uint32_t c,uint32_t clip) {
    return ui::NativeMenuFonts::fixture_rectangle({d,p,s,c,clip});
}
void native_raster(const ElfImage& image,const char* font_path) {
    const auto fn=[&](const char* name){return abi::require_function(image,name);};
    abi::GuestCall alloc(image,"_Znaj",0x20,"_Z11GlitchAllocjN6glitch6memory13E_MEMORY_HINTE",addr(reinterpret_cast<void*>(&allocate)));
    abi::GuestCall dealloc(image,"_ZdaPv",0x18,"_Z10GlitchFreePv",addr(reinterpret_cast<void*>(&release)));
    abi::GuestHook atlas(image,"_ZN17GlyphAtlasManager8AddGlyphERKN6glitch4core11dimension2dIiEEPvR17AtlassedGlyphInfo",{0x55,0x89,0xe5,0x83,0xec,0x28},addr(reinterpret_cast<void*>(&add_glyph)));
    abi::GuestHook texture(image,"_ZN6glitch5video9C2DDriver12set2DTextureEPNS0_12IVideoDriverERKN5boost13intrusive_ptrINS0_8ITextureEEEbb",{0x55,0x89,0xe5,0x56,0x83,0xec,0x24},addr(reinterpret_cast<void*>(&texture_bind)));
    std::ifstream file(font_path,std::ios::binary);
    check(bool(file),"local fixture font exists");
    const std::vector<char> data((std::istreambuf_iterator<char>(file)),{});
    uint32_t library=0,ft_face=0;
    check(!abi::invoke(fn("FT_Init_FreeType"),{addr(&library)}),"native FreeType initialized in isolation");
    // Native outline caching resolves CTTFLibrary's singleton then its +8 handle.
    const auto library_slot=image.address(0x11567a0,4);
    const auto saved_library=word(library_slot);
    std::array<uint32_t,3> library_wrapper{0,0,library};
    word(library_slot)=addr(library_wrapper.data());
    check(!abi::invoke(fn("FT_New_Memory_Face"),{library,addr(data.data()),uint32_t(data.size()),0,addr(&ft_face)}),"native FreeType opens local test font");
    std::array<uint32_t,3> wrapper{0,0,ft_face}; uint32_t reference=addr(wrapper.data());
    std::array<uint32_t,20> table{}; table[0x38/4]=addr(reinterpret_cast<void*>(&dispatch_rectangle));
    std::array<uint32_t,2> driver{addr(table.data()),0};
    std::array<uint32_t,40> native_font{}; native_font[2]=addr(driver.data());
    std::array<uint32_t,40> font_table{};
    font_table[0x48/4]=fn("_ZN11CTTFFontSpr14SetCharSpacingEs");
    std::array<uint32_t,64> font_sprite{};
    font_sprite[0]=addr(font_table.data()); font_sprite[0xc8/4]=addr(native_font.data());
    native_font[0x54/4]=20; native_font[0x80/4]=reference;
    abi::invoke(fn("_ZN8CTTFFont9setBorderEjN6glitch5video6SColorE"),{addr(native_font.data()),8,0xff000000});
    check(native_font[0x60/4]==160,"native outlined style uses strength 160 for a 20px font");
    reinterpret_cast<uint8_t*>(font_sprite.data())[0xdd]=1;
    abi::invoke(fn("_ZN11CTTFFontSpr17SetOutlinedGlyphsEb"),{addr(font_sprite.data()),0});
    check(!reinterpret_cast<uint8_t*>(font_sprite.data())[0xdd] && native_font[0x60/4]==160,
          "disabling outlined drawing retains shared cache strength for later plain text");
    ui::NativeMenuFonts::Bindings bindings{{fn("_ZN9CTTFGlyph5cacheEjRKN5boost13intrusive_ptrI8CTTFFaceEEPN6glitch5video12IVideoDriverEP17GlyphAtlasManagerjjNS7_6SColorEjb"),
        fn("_ZN8CTTFFont9drawGlyphEPK9CTTFGlyphRKN6glitch4core8vector2dIiEEPKNS4_4rectIiEENS3_5video6SColorEbf"),
        fn("_ZN8CTTFFont21drawGlyphSmoothShadedEPK9CTTFGlyphRKN6glitch4core8vector2dIiEEPKNS4_4rectIiEEPNS3_5video6SColorEbf"),
        addr(reinterpret_cast<void*>(&capture_rectangle)),fn("_ZN9CTTFGlyphD1Ev"),fn("_ZN9CTTFGlyphD2Ev"),addr(reinterpret_cast<void*>(&clear))},
        fn("_ZN9CTTFGlyphC1Ev"),fn("FT_Set_Pixel_Sizes"),fn("FT_Load_Glyph")};
    {
        ui::NativeMenuFonts quality(bindings);
        for(unsigned size:{12u,20u,40u}) for(unsigned character:{unsigned('A'),unsigned('g'),unsigned('W'),0xe8u}) {
            const auto index=abi::invoke(fn("FT_Get_Char_Index"),{ft_face,character});
            check(index!=0,"test glyph available");
            for(unsigned border:{0u,1u}) for(unsigned coefficient:{0u,7u,8u,9u,10u,11u}) for(unsigned bold:{0u,1u}) {
                if(!coefficient && bold) continue;
                Glyph glyph{}; abi::invoke(bindings.construct,{addr(glyph.data())});
                const size_t first=rasters.size();
                // Both normal and outlined caches receive this native 26.6 strength.
                native_font[0x54/4]=size; native_font[0x80/4]=reference;
                abi::invoke(fn("_ZN8CTTFFont9setBorderEjN6glitch5video6SColorE"),{addr(native_font.data()),coefficient,0xff000000});
                const unsigned strength=native_font[0x60/4];
                check(strength==size*coefficient,"native border producer supplies 26.6 strength, not pixel count");
                ui::NativeMenuFonts::fixture_cache({addr(glyph.data()),index,addr(&reference),addr(driver.data()),456,size,border,0xff000000,strength,bold});
                check(rasters.size()==first+2,"actual native cache produces base and dense raster");
                const auto& low=rasters[first]; const auto& high=rasters[first+1];
                // Emboldening adds its own whole-pixel rounding to glyph hinting.
                const unsigned tolerance=bold?3u:2u;
                if(high.width+tolerance<low.width*2 || high.height+tolerance<low.height*2)
                    std::printf("Raster size=%u character=%u border=%u strength=%u bold=%u low=%ux%u high=%ux%u\n",size,character,border,strength,bold,low.width,low.height,high.width,high.height);
                check(high.width+tolerance>=low.width*2 && high.height+tolerance>=low.height*2,"actual higher resolution glyph coverage");
                unsigned partial=0; for(size_t i=0;i<high.pixels.size();i+=4) if(high.pixels[i]>0 && high.pixels[i]<255) ++partial;
                check(partial>0,"native raster retains grayscale edge coverage");
                const auto saved=glyph; native_font[0x54/4]=size;
                const std::array<uint32_t,2> position{73,91};
                const std::array<uint32_t,4> clip{0,0,1920,1080},colors{0xffeeddcc,0xff112233,0xff334455,0xff778899};
                for(bool smooth:{false,true}) for(float scale:{.5f,1.f,1.5f,3.f}) {
                    const std::array<uint32_t,7> a{addr(native_font.data()),addr(glyph.data()),addr(position.data()),addr(clip.data()),smooth?addr(colors.data()):0xffeeddcc,0,std::bit_cast<uint32_t>(scale)};
                    ui::menu_draw_depth=0; ui::NativeMenuFonts::fixture_draw(a,smooth);
                    const auto destination=captured_destination,source=captured_source;
                    check(captured_texture==addr(texture_refs[first].data()),"native base raster bound");
                    ui::menu_draw_depth=1; ui::NativeMenuFonts::fixture_draw(a,smooth); ui::menu_draw_depth=0;
                    check(captured_destination==destination && glyph==saved,"native before/after geometry identical at every scale");
                    check(captured_source[2]-captured_source[0]==high.width && captured_source[3]-captured_source[1]==high.height,"dense source rectangle reaches actual native driver boundary");
                    check(captured_texture==addr(texture_refs[first+1].data()) && source[2]-source[0]==low.width,"matching high density texture bound");
                }
                ui::NativeMenuFonts::fixture_destroy(addr(glyph.data()));
                check(texture_refs[first][1]==100 && texture_refs[first+1][1]==100,"both native intrusive references released exactly once");
            }
        }
    }
    check(allocated==0,"native cache scratch allocations balanced");
    word(library_slot)=saved_library;
    check(!abi::invoke(fn("FT_Done_Face"),{ft_face}) && !abi::invoke(fn("FT_Done_FreeType"),{library}),"native FreeType ownership released");
    std::printf("Native raster evidence: %zu uploads, original geometry retained for Latin/accented glyphs, outlines and both drawing paths\n",rasters.size());
}

void real(const char* path,const char* font_path) {
    ImportRegistry imports; bionic::register_boot(imports); audio::register_opensl(imports); gles::register_imports(imports); bionic::CxaRegistry cxa(imports);
    ElfImage image(path,imports);
    std::vector<std::pair<uint32_t,std::vector<uint8_t>>> saved;
    for(const auto& segment:image.segments()) if(segment.flags&1) {
        const auto p=image.address(segment.vaddr,segment.filesz); saved.push_back({p,{reinterpret_cast<uint8_t*>(p),reinterpret_cast<uint8_t*>(p)+segment.filesz}});
    }
    const auto selector=abi::require_function(image,"_ZN11Application11GetFontNameEii");
    for(unsigned id=0;id<3;++id) {
        const auto selected=abi::invoke(selector,{0,id,0});
        check(std::strcmp(reinterpret_cast<const char*>(selected),"neuropol.ttf")==0,"native default menu font is Neuropol for all three IDs");
    }
    for(const auto& entry:std::array<std::pair<unsigned,const char*>,4>{{{5,"NewCezannePro-DB.ttf"},{6,"NanumGothic.ttf"},{7,"WenQuanYiZenHei.ttf"},{9,"Days.ttf"}}})
        check(!std::strcmp(reinterpret_cast<const char*>(abi::invoke(selector,{0,0,entry.first})),entry.second),"native localized font selection");
    {
        ui::NativeMenuFonts quality(image);
    }
    native_raster(image,font_path);
    for(const auto& item:saved) check(!std::memcmp(reinterpret_cast<void*>(item.first),item.second.data(),item.second.size()),"all executable bytes restored");
}
void tracing() {
    using Trace=ui::MenuTextTrace;
    Trace::configure(true);
    Trace::Glyph sample; sample.size=20; sample.available=sample.selected=true;
    sample.low_width=12; sample.high_width=24; sample.target_width=960; sample.target_density=2;
    for(unsigned repeat=0;repeat<2;++repeat) {
        Trace::Scope outer(true,true,123,"Fixture title",0x1234);
        Trace::glyph(sample); Trace::rectangle();
        { Trace::Scope excluded(false,false,0,reinterpret_cast<const char*>(1),0); Trace::glyph(sample); }
        { Trace::Scope inner(true,false,456,"Fixture button",0x5678); }
        Trace::glyph(sample); Trace::rectangle();
    }
    FILE* output=std::tmpfile(); check(output!=nullptr,"trace fixture output");
    Trace::report(output); std::rewind(output);
    std::array<char,4096> buffer{}; const auto size=std::fread(buffer.data(),1,buffer.size()-1,output);
    const std::string text(buffer.data(),size);
    check(text.find("backend=bitmap")!=std::string::npos && text.find("glyphs=0")!=std::string::npos,"uncovered text path observable");
    check(text.find("glyphs=2 dense=2 missing=0 mono=0 dense_quads=2")!=std::string::npos,"nested contexts restored, excluded calls ignored");
    check(text.find("Fixture title")==text.rfind("Fixture title"),"duplicate frames suppressed");
    const auto before=std::ftell(output); Trace::report(output); check(std::ftell(output)==before,"records emitted once");
    std::fclose(output);
    Trace::configure(true);
    for(unsigned i=0;i<Trace::capacity;++i) { Trace::Scope unique(true,true,123,"Fixture",i); }
    { Trace::Scope full(true,true,0,reinterpret_cast<const char*>(1),0); check(!Trace::active(),"full trace never reads further guest strings"); }
    Trace::configure(false);
    { Trace::Scope disabled(true,true,0,reinterpret_cast<const char*>(1),0); check(!Trace::active(),"disabled trace never reads guest data"); }
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2 || argc==3) {
            const char* font_path=argc==3?argv[2]:"C:/Windows/Fonts/arial.ttf";
            if(!std::filesystem::exists(argv[1]) || !std::filesystem::exists(font_path)) return 77;
            real(argv[1],font_path);
        } else { synthetic(); tracing(); }
        std::puts("Menu font density checks passed; no game initialization or gameplay"); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"Menu font FAIL %s\n",e.what()); return 1; }
}
