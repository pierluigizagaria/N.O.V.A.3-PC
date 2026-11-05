// Execute the original selector in an isolated ELF; never initialize the game.
#include "abi/guest_hook.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include "menu/initial_target_scaling.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace nova;
namespace {
uint32_t ptr(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t& word(uint32_t p) { return *reinterpret_cast<uint32_t*>(p); }
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
constexpr auto selector="_ZN14CSpriteManager17GetSpriteFilenameEPKcRSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS4_6memory13E_MEMORY_HINTE0EEEERfRiSD_b";
constexpr auto ctor="_ZNSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS1_6memory13E_MEMORY_HINTE0EEEEC1EPKcRKS6_";
uint32_t construct=0,dispose=0;
unsigned allocations=0;
std::string texture_filename,texture_key;
uint32_t __cdecl texture_request(uint32_t out,uint32_t,uint32_t filename,uint32_t key) {
    texture_filename=reinterpret_cast<const char*>(filename);
    texture_key=reinterpret_cast<const char*>(key);word(out)=0;return out;
}
__declspec(naked) void texture_request_entry() {
    __asm {
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
        call texture_request
        lea esp,[ebp-8]
        pop edi
        pop esi
        pop ebp
        ret 4
    }
}
uint32_t __cdecl allocate(uint32_t n,uint32_t) { ++allocations; return ptr(std::calloc(1,n)); }
void __cdecl release(uint32_t p) { if(p) { --allocations;std::free(reinterpret_cast<void*>(p)); } }
void string_new(uint32_t out,const char* name) { unsigned char allocator=0; abi::invoke(construct,{out,ptr(name),ptr(&allocator)}); }
void string_delete(uint32_t data) { unsigned char allocator=0; abi::invoke(dispose,{data-12,ptr(&allocator)}); }
struct Family { std::string name;std::vector<std::string> files; };
void run(const char* path,const char* catalog) {
    ImportRegistry imports;bionic::register_boot(imports);audio::register_opensl(imports);gles::register_imports(imports);bionic::CxaRegistry cxa(imports);
    ElfImage image(path,imports);
    std::vector<std::pair<uint32_t,std::vector<uint8_t>>> saved;
    for(const auto& s:image.segments()) if(s.flags&1) {
        const auto p=image.address(s.vaddr,s.filesz);saved.push_back({p,{reinterpret_cast<uint8_t*>(p),reinterpret_cast<uint8_t*>(p)+s.filesz}});
    }
    construct=abi::require_function(image,ctor);
    dispose=abi::require_function(image,"_ZNSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS1_6memory13E_MEMORY_HINTE0EEEE4_Rep10_M_disposeERKS6_");
    std::vector<Family> families{{"hud3d.bsprite",{"hud3d.bsprite","hud3d_2560x1504.bsprite"}},
        {"menu_elements_nova3.bsprite",{"menu_elements_nova3_960x640.bsprite","menu_elements_nova3_1720x1280.bsprite"}},
        {"nv_hud.tga",{"nv_hud.etc","nv_hud_1720x1280.etc","nv_hud_2560x1504.etc"}}};
    if(catalog) {
        families.clear();std::ifstream input(catalog);check(bool(input),"catalog readable");std::string line;
        while(std::getline(input,line)) {std::istringstream stream(line);Family f;std::getline(stream,f.name,'|');std::string name;
            while(std::getline(stream,name,'|')) f.files.push_back(name);families.push_back(std::move(f));}
    }
    // Native matching consumes ordinary archive index records (four words,
    // filename at +8). Payloads are unnecessary for filename selection.
    std::array<uint32_t,9> device{};std::array<uint32_t,21> fs{};std::array<uint32_t,12> pak{};
    std::array<uint32_t,81> driver{};device[2]=ptr(driver.data());driver[80]=123;
    uint32_t pak_ref=ptr(pak.data());fs[13]=ptr(&pak_ref);fs[14]=fs[13]+4;fs[19]=4; // Native ETC format.
    device[8]=ptr(fs.data());
    const auto device_slot=image.symbol("g_device"),width_slot=image.symbol("g_ScreenW"),height_slot=image.symbol("g_ScreenH"),flag_slot=image.symbol("isCheckDataGoogleDeal");
    const auto old_device=word(device_slot),old_width=word(width_slot),old_height=word(height_slot);
    const auto old_flag=*reinterpret_cast<uint8_t*>(flag_slot);
    const auto count_slot=image.address(0x115679c,4),old_count=word(count_slot);
    word(device_slot)=ptr(device.data());
    {
        abi::GuestHook alloc(image,"_Z11GlitchAllocjN6glitch6memory13E_MEMORY_HINTE",{0x55,0x89,0xe5,0x83,0xec,0x28},ptr(reinterpret_cast<void*>(&allocate)));
        abi::GuestHook free(image,"_Z10GlitchFreePv",{0x55,0x89,0xe5,0x83,0xec,0x18},ptr(reinterpret_cast<void*>(&release)));
        constexpr auto factory="_ZN17CCustomResFactory10getTextureEPKcb";
        abi::GuestCall texture(image,factory,0xc4,"_ZN6glitch5video15CTextureManager10getTextureEPKcS3_",ptr(reinterpret_cast<void*>(&texture_request_entry)));
        std::puts("family,width,height,initial_flag,scan_count,cover,selected,scale,nominal_w,nominal_h,final_flag");
        for(const auto& f:families) for(const auto extent:std::array<std::array<unsigned,2>,12>{{{960,640},{1280,720},{1720,1280},{1920,720},{1920,1080},{1920,1128},{1920,1200},{1920,1280},{1920,1504},{2560,1440},{2560,1504},{3840,2160}}})
        for(unsigned initial_flag:{0u,1u}) for(unsigned scanned:{0u,3u}) for(unsigned cover:{0u,1u}) {
            std::vector<std::array<uint32_t,4>> records;
            for(const auto& name:f.files) records.push_back({0,0,ptr(name.c_str()),0});
            pak[8]=ptr(records.data());pak[9]=pak[8]+static_cast<uint32_t>(records.size()*16);
            word(width_slot)=extent[0];word(height_slot)=extent[1];*reinterpret_cast<uint8_t*>(flag_slot)=static_cast<uint8_t>(initial_flag);
            word(count_slot)=scanned;
            uint32_t output=0,w=0,h=0;float scale=0;string_new(ptr(&output),"");
            abi::invoke(abi::require_function(image,selector),{0,ptr(f.name.c_str()),ptr(&output),ptr(&scale),ptr(&w),ptr(&h),cover});
            if(f.name=="hud3d.bsprite") {
                const auto expected=extent[0]==1920 && extent[1]!=1080?"hud3d_2560x1504.bsprite":"hud3d.bsprite";
                check(!std::strcmp(reinterpret_cast<const char*>(output),expected),"HUD native HD resolution gates");
            }
            if(f.name=="nv_hud.tga") {
                const auto expected=extent[0]!=1920?"nv_hud.tga":
                    extent[1]==1504 && cover?"nv_hud_2560x1504.etc":"nv_hud_1720x1280.etc";
                check(!std::strcmp(reinterpret_cast<const char*>(output),expected),"native texture variant selection and ETC matching");
                uint32_t resource=99;
                const uint32_t args[]{ptr(&resource),0,ptr("nv_hud.tga"),0};
                menu::menu_call_sret4(abi::require_function(image,factory),args,4);
                check(!resource && texture_filename=="nv_hud.tga" && texture_key=="nv_hud","direct material request bypasses HD sprite selection at every extent");
            }
            if(f.name=="menu_elements_nova3.bsprite") {
                // Without the detected-device flag, fit mode can still prefer
                // the 960 variant at the artificial 1920x720 extent.
                const bool dense=extent[0]==1920 && !(extent[1]==720 && !cover && !initial_flag && scanned==3);
                const auto expected=dense?"menu_elements_nova3_1720x1280.bsprite":"menu_elements_nova3_960x640.bsprite";
                check(!std::strcmp(reinterpret_cast<const char*>(output),expected),"menu HD is selected only at the original device width");
            }
            std::printf("%s,%u,%u,%u,%u,%u,%s,%.9g,%u,%u,%u\n",f.name.c_str(),extent[0],extent[1],initial_flag,scanned,cover,reinterpret_cast<const char*>(output),scale,w,h,*reinterpret_cast<uint8_t*>(flag_slot));
            string_delete(output);check(allocations==0,"selector balances native strings and vector allocations");
        }
    }
    word(device_slot)=old_device;word(width_slot)=old_width;word(height_slot)=old_height;*reinterpret_cast<uint8_t*>(flag_slot)=old_flag;
    word(count_slot)=old_count;
    for(const auto& s:saved) check(!std::memcmp(reinterpret_cast<const void*>(s.first),s.second.data(),s.second.size()),"all executable bytes restored");
}
}
int main(int argc,char** argv) {
    if(argc<2 || !std::filesystem::exists(argv[1])) return 77;
    try {run(argv[1],argc>2?argv[2]:nullptr);return 0;}
    catch(const std::exception& e) {std::fprintf(stderr,"sprite selection FAIL %s\n",e.what());return 1;}
}
