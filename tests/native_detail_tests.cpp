#include "platform/native_detail.h"
#include "platform/profile.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include "camera/native_world_fov.h"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
constexpr auto setter="_ZN21CCustomColladaFactory16SetLodsDistScaleEf";
constexpr std::array<const char *,3> owners{
    "_ZN6CLevel7PreInitEi","_ZN6CLevel6UpdateEv","_ZN6CLevel16SetLodsDistScaleEf"};
constexpr std::array<uint32_t,3> offsets{0x11c,0x24ee,0x22};
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
template<class T> uint32_t ptr(T *p) { return reinterpret_cast<uint32_t>(p); }
uint32_t &word(void *p,size_t offset) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p)+offset); }
void scalar(void *p,size_t offset,float value) { word(p,offset)=std::bit_cast<uint32_t>(value); }
void __cdecl quiet(uint32_t,uint32_t) {}
void __cdecl visibility(uint32_t node,uint32_t visible) { word(reinterpret_cast<void *>(node),0x104)=visible; }
alignas(4) std::array<std::array<uint8_t,0x30>,2> chunks{};
uint32_t __cdecl get_chunk(uint32_t,uint32_t index) { check(index<2,"native batch chunk index");return ptr(chunks[index].data()); }

uint32_t call_target(const nova::ElfImage &image,size_t index) {
    const auto call=nova::abi::require_function(image,owners[index])+offsets[index];
    const auto *bytes=reinterpret_cast<const uint8_t *>(call);
    check(bytes[0]==0xe8,"original owner retains direct CALL");
    uint32_t delta;std::memcpy(&delta,bytes+1,4);return call+5+delta;
}
void produce(const nova::ElfImage &image,bool real,size_t index,void *factory,float scale) {
    // Synthetic tests execute the complete caller and aligned guest re-entry.
    // Real tests use the installed call destination without running CLevel logic.
    const auto fn=real ? call_target(image,index) : nova::abi::require_function(image,owners[index]);
    nova::abi::invoke(fn,{ptr(factory),std::bit_cast<uint32_t>(scale)});
}
void consumers(const nova::ElfImage &image,void *factory,float distance_scale) {
    constexpr auto root="_ZN24CColladaRootLODSceneNode9onAnimateEf";
    constexpr auto zone="_ZN14CZoneSceneNode9onAnimateEf";
    nova::abi::GuestCall root_parent(image,root,0x21,"_ZN6glitch7collada14CRootSceneNode9onAnimateEf",ptr(&quiet));
    nova::abi::GuestCall zone_parent(image,zone,0x326,"_ZN6glitch5scene10ISceneNode9onAnimateEf",ptr(&quiet));
    nova::abi::GuestHook transforms(image,"_ZN6glitch5scene10ISceneNode22updateAbsolutePositionEb",
        {0x55,0x89,0xe5,0x57,0x56},ptr(&quiet));
    alignas(4) std::array<uint8_t,0x1d0> object{},batched{};
    alignas(4) std::array<uint8_t,0xe0> scene{};
    alignas(4) std::array<uint8_t,0x110> camera{},high{},low{},container{},mesh{};
    alignas(4) std::array<uint8_t,0x44> lod{};
    alignas(4) std::array<uint8_t,0xcc> batch_lod{};
    std::array<float,20> ranges{};
    ranges.fill(std::numeric_limits<float>::max());
    ranges[0]=0;ranges[1]=100;ranges[10]=100;
    std::array<uint32_t,20> vtable{};vtable[0x4c/4]=ptr(&visibility);
    word(high.data(),0)=word(low.data(),0)=ptr(vtable.data());
    word(scene.data(),0xdc)=ptr(camera.data());
    for(auto *node:{object.data(),batched.data()}) word(node,0xe8)=ptr(scene.data());
    object[0x1c0]=batched[0x1a0]=1;
    word(object.data(),0x1a8)=ptr(lod.data());word(object.data(),0x1ac)=ptr(lod.data()+lod.size());
    word(object.data(),0x1b8)=ptr(static_cast<uint8_t *>(factory)+8);
    word(lod.data(),4)=ptr(high.data());word(lod.data(),8)=ptr(low.data());word(lod.data(),0x40)=ptr(ranges.data());
    word(batched.data(),0x188)=ptr(batch_lod.data());word(batched.data(),0x18c)=ptr(batch_lod.data()+batch_lod.size());
    word(batched.data(),0x198)=ptr(static_cast<uint8_t *>(factory)+8);
    word(batch_lod.data(),0xc4)=ptr(ranges.data());
    // Two real batched-geometry descriptors, each selecting one terminal chunk.
    word(batch_lod.data(),4)=0x00010000;word(batch_lod.data(),20)=0x00020001;
    std::array<uint32_t,2> indices{0,1};
    std::array<uint32_t,35> mesh_vtable{};mesh_vtable[0x88/4]=ptr(&get_chunk);
    word(mesh.data(),0)=ptr(mesh_vtable.data());word(container.data(),0x104)=ptr(mesh.data());
    word(batched.data(),0x148)=ptr(indices.data());word(batched.data(),0x150)=ptr(container.data());
    for(auto first:{true,false}) {
        word(lod.data(),0x30)=word(batch_lod.data(),0xb4)=first ? UINT32_MAX : 0u;
        for(float distance:{5.f,15.f,45.f,15.f,5.f}) {
            scalar(camera.data(),0x40,distance);
            nova::abi::invoke(nova::abi::require_function(image,root),{ptr(object.data()),0});
            nova::abi::invoke(nova::abi::require_function(image,zone),{ptr(batched.data()),0});
            const auto expected=distance>10.f*distance_scale ? 1u : 0u;
            check(word(lod.data(),0x30)==expected && word(batch_lod.data(),0xb4)==expected,
                  "actual individual and batched native consumers keep detail to multiplied distance");
            check(word(high.data(),0x104)==unsigned(expected==0) && word(low.data(),0x104)==unsigned(expected==1),
                  "native object visibility follows selected detail");
            check((word(chunks[0].data(),0x28)&1)==unsigned(expected==0) &&
                  (word(chunks[1].data(),0x28)&1)==unsigned(expected==1),"native batch chunk visibility follows detail");
        }
    }
}
void matrix(const nova::ElfImage &image,bool real,int multiplier) {
    alignas(4) std::array<uint8_t,32> factory{};factory.fill(0xa5);
    for(size_t i=0;i<owners.size();++i) for(float input:{1.f,.5f,2.f,0.f,-1.f}) {
        for(int repeat=0;repeat<3;++repeat) {
            produce(image,real,i,factory.data(),input);
            const float scale=input>0 ? input*static_cast<float>(multiplier) : input;
            check(std::bit_cast<float>(word(factory.data(),8))==scale*scale,"squared native distance, FOV composition, no compounding");
            for(size_t b=0;b<factory.size();++b) if(b<8 || b>=12) check(factory[b]==0xa5,"no unrelated factory fields change");
        }
    }
    for(size_t i=0;i<owners.size();++i) {
        produce(image,real,i,factory.data(),1.f);
        if(real) consumers(image,factory.data(),static_cast<float>(multiplier));
    }
    for(float input:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN(),std::sqrt(std::numeric_limits<float>::max())}) {
        nova::abi::invoke(nova::abi::require_function(image,setter),{ptr(factory.data()),std::bit_cast<uint32_t>(input)});
        const auto original=word(factory.data(),8);
        produce(image,real,0,factory.data(),input);
        check(word(factory.data(),8)==original,"nonfinite and overflow-prone values preserve original setter behavior");
    }
    // Direct calls not belonging to the three level producers remain original.
    nova::abi::invoke(nova::abi::require_function(image,setter),{ptr(factory.data()),std::bit_cast<uint32_t>(1.f)});
    check(std::bit_cast<float>(word(factory.data(),8))==1.f,"unrelated setter callers remain native");
}
void config_check(const std::filesystem::path &fixture) {
    const auto file=fixture.parent_path()/"native-detail-test.conf";
    struct Cleanup { std::filesystem::path file;~Cleanup() { std::error_code ec;std::filesystem::remove(file,ec); } } cleanup{file};
    std::filesystem::path obb;
    for(auto text:{"", "lod_distance = 1\n", "lod_distance = 4\n", "lod_distance = 2\n"}) {
        {std::ofstream out(file);out<<text;}
        const int expected=*text==0 ? 4 : text[15]-'0';
        check(nova::profile::read_config(file,obb).lod_distance==expected,"startup LOD configuration and default");
    }
    for(auto text:{"0","5","-1","nan","2.5","2junk",""}) {
        {std::ofstream out(file);out<<"lod_distance = "<<text<<'\n';}
        bool rejected=false;try { (void)nova::profile::read_config(file,obb); } catch(const std::runtime_error &) { rejected=true; }
        check(rejected,"invalid distance configuration rejected");
    }
}
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    const bool real=std::strcmp(argv[2],"--real")==0;
    if(real && !std::filesystem::exists(argv[1])) return 77;
    try {
        if(!real) config_check(argv[1]);
        nova::ImportRegistry imports;
        if(real) { nova::bionic::register_boot(imports);nova::audio::register_opensl(imports);nova::gles::register_imports(imports); }
        nova::bionic::CxaRegistry cxa(imports);nova::ElfImage image(argv[1],imports);
        std::array<uint32_t,3> before{};
        for(size_t i=0;i<3;++i) before[i]=call_target(image,i);
        matrix(image,real,1);
        for(auto multiplier:{1,2,4}) {
            { nova::platform::NativeDetail policy(image,multiplier);
              bool rejected=false;try { nova::platform::NativeDetail duplicate(image); } catch(const std::runtime_error &) { rejected=true; }
              check(rejected,"duplicate ownership rejected");matrix(image,real,multiplier); }
            for(size_t i=0;i<3;++i) check(call_target(image,i)==before[i],"exact native CALL restoration");
        }
        matrix(image,real,1);
        if(real) {
            nova::platform::NativeDetail detail(image);
            nova::camera::NativeWorldFov camera(image,90.f,nullptr,nullptr,90.f,true);
            matrix(image,true,4);
        }
        for(auto invalid:{0,5,-1}) {
            bool rejected=false;try { nova::platform::NativeDetail policy(image,invalid); } catch(const std::invalid_argument &) { rejected=true; }
            check(rejected,"invalid multiplier cannot install");
        }
        {
            nova::abi::GuestCall foreign(image,owners[1],offsets[1],setter,ptr(&quiet));
            bool rejected=false;try { nova::platform::NativeDetail wrong(image); } catch(const std::runtime_error &) { rejected=true; }
            check(rejected && call_target(image,0)==before[0] && call_target(image,1)==ptr(&quiet) &&
                  call_target(image,2)==before[2],"failed admission rolls back and preserves foreign patch");
        }
        {nova::platform::NativeDetail retry(image);matrix(image,real,4);}
        std::puts("native detail: distance producers, consumer visibility, restore, admission and config PASS");return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"native detail: %s\n",e.what());return 1; }
}
