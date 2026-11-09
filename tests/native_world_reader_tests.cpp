#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "loader/diagnostics.h"
#include "loader/sha256.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
uint32_t ptr(const void *value) { return reinterpret_cast<uint32_t>(value); }
uint32_t shotProjectile=0,shotCount=0,shotMultiplayer=0;
uint32_t ammoLevel=0;
std::array<float,3> collisionDimensions{},collisionPosition{};
float authoredAnimationSpeed=0;
uint32_t authoredAnimationTime=0,authoredEventCount=0;
uint32_t __cdecl authored_speed_set(uint32_t,uint32_t slot,float speed) { check(slot==0,"authored speed slot");authoredAnimationSpeed=speed;return 0; }
float __cdecl authored_speed_get(uint32_t,uint32_t) { return authoredAnimationSpeed; }
uint32_t __cdecl authored_time_get(uint32_t,uint32_t) { return authoredAnimationTime; }
uint32_t __cdecl authored_length_get(uint32_t,uint32_t) { return 666; }
void __cdecl motion_track_value(uint32_t,uint32_t track,float time,float *out) {
    check(track==1 || track==5,"synthetic root track identity");
    if(track==1) { out[0]=10+time*.01f;out[1]=20+time*.02f;out[2]=30+time*.03f; }
    else { out[0]=out[1]=out[2]=0;out[3]=1; }
}
uint32_t __cdecl authored_event(uint32_t,uint32_t kind,const uint32_t *event) {
    check(kind==9 && event[0]==4 && event[1]==7 && event[2]==0,"authored refill event dispatch");++authoredEventCount;return 0;
}
uint32_t __cdecl collision_construct(uint32_t self,uint32_t,uint32_t kind,const float *dimensions) {
    check(kind==1,"original actor collision shape kind");
    std::memcpy(collisionDimensions.data(),dimensions,12);
    *reinterpret_cast<uint32_t *>(self)=self;
    return self;
}
uint32_t __cdecl collision_position(uint32_t,const float *position) {
    std::memcpy(collisionPosition.data(),position,12);return 0;
}
uint32_t addonLoadout=0,addonMask=0;
uint32_t __cdecl health_perk(uint32_t self,uint32_t perk) {
    check(self==addonLoadout && perk==5,"original health perk identity");
    return addonMask!=0;
}
uint32_t __cdecl addon_component(uint32_t,uint32_t kind) { check(kind==58,"addon loadout component");return addonLoadout; }
uint32_t __cdecl addon_active(uint32_t self,uint32_t weapon,uint32_t addon) {
    check(self==addonLoadout && weapon==123 && addon>=1 && addon<=6,"addon selection identity");
    return (addonMask & (1u<<addon))!=0;
}
uint32_t __cdecl ammo_level() { return ammoLevel; }
uint32_t __cdecl ammo_noop(uint32_t,...) { return 0; }
uint32_t __cdecl shot_level() { return 1; }
uint32_t __cdecl shot_pool(uint32_t,uint32_t type) { check(type==200,"shot projectile definition");return 1; }
uint32_t __cdecl shot_component(uint32_t,uint32_t kind) { check(kind==40,"shot component type");return shotProjectile; }
uint32_t __cdecl shot_multiplayer(uint32_t) { return shotMultiplayer; }
uint32_t __cdecl shot_time(uint32_t) { return 0; }
float __cdecl target_health(uint32_t) { return 100; }
uint32_t targetQueryMode=0,targetQueryCount=0;
uint32_t __cdecl target_query(uint32_t,const float *capsule,uint32_t,uint32_t aimed) {
    check(capsule[0]==10 && capsule[3]==20 && capsule[6]==.001f,"native bullet event query geometry");
    targetQueryMode=aimed;++targetQueryCount;return 0;
}
uint32_t __cdecl shot_launch(uint32_t projectile,uint32_t origin,uint32_t target,uint32_t,uint32_t owner,uint32_t weapon,float range,uint32_t,float damage,uint32_t,uint32_t,uint32_t) {
    check(projectile==shotProjectile && owner==0x1111 && weapon==100,"original projectile launch identities");
    check(range==777 && damage==2.5f,"original weapon damage multiplier");
    check(reinterpret_cast<const float *>(origin)[0]==10 && reinterpret_cast<const float *>(target)[0]==20,"original zero-spread target");
    ++shotCount;return 1;
}
void integer(Bytes &b, uint32_t value) {
    for (int shift : {24, 16, 8, 0}) b.push_back(static_cast<uint8_t>(value >> shift));
}
void scalars(Bytes &b, unsigned count) { while(count--) integer(b, 0x3f800000); }
void empty_string(Bytes &b) { integer(b, 0xffffffff); }
Bytes dictionary(const Bytes &body) {
    Bytes result{'D','I','C','T'};
    integer(result, static_cast<uint32_t>(9 + body.size()));
    result.push_back(0);
    result.insert(result.end(), body.begin(), body.end());
    integer(result, 0);
    return result;
}
Bytes base() {
    Bytes b; integer(b, 1234); scalars(b, 9); b.insert(b.end(), {1,0}); return b;
}
Bytes component(unsigned kind) {
    Bytes b;
    switch(kind) {
    case 0: return base();
    case 1: empty_string(b); integer(b, 17); b.resize(b.size()+11); break;
    case 6:
        integer(b, 3); scalars(b, 9); integer(b, 8); b.push_back(1);
        empty_string(b); for(int i=0;i<3;++i) integer(b, 42);
        b.push_back(0); integer(b, 17); b.push_back(1); break;
    case 7:
        integer(b, 2); b.push_back(1); scalars(b, 3); integer(b, 5);
        b.resize(b.size()+5); empty_string(b); scalars(b, 1); b.push_back(1); break;
    case 93:
        integer(b, 0); integer(b, 2);
        for(int i=0;i<2;++i) {
            for(int j=0;j<5;++j) empty_string(b);
            b.insert(b.end(), {0,0,1}); integer(b, 254);
        }
        break;
    case 12:
        empty_string(b); empty_string(b); scalars(b, 1); b.push_back(0);
        empty_string(b); b.insert(b.end(), {0,1}); integer(b, 2);
        for(int i=0;i<2;++i) { integer(b, 2); for(int j=0;j<2;++j) { empty_string(b); scalars(b,1); } }
        b.resize(b.size()+3); break;
    case 13: scalars(b,6); break;
    case 21: for(int i=0;i<4;++i) integer(b, 1); scalars(b,1); break;
    case 22: empty_string(b); integer(b,3); scalars(b,3); b.push_back(1); break;
    case 23: case 121: integer(b,7); break;
    case 25:
        b.resize(5); empty_string(b); integer(b,2);
        for(int i=0;i<2;++i) { empty_string(b); integer(b, i+1); }
        integer(b,99); break;
    case 40:
        b.push_back(0); scalars(b,11); empty_string(b); empty_string(b);
        scalars(b,2); b.push_back(0); scalars(b,1);
        integer(b,1); integer(b,2); empty_string(b); integer(b,3);
        integer(b,40); integer(b,1); integer(b,2); integer(b,1); empty_string(b); scalars(b,1);
        integer(b,1); integer(b,2); integer(b,3); scalars(b,1);
        b.push_back(0); empty_string(b); empty_string(b); scalars(b,1);
        b.insert(b.end(),{0,0,1,0}); empty_string(b); scalars(b,1); b.push_back(0);
        integer(b,7); scalars(b,1); integer(b,8); integer(b,9);
        b.insert(b.end(),{1,0}); integer(b,1000); break;
    case 58:
        scalars(b,15);integer(b,1);integer(b,2);scalars(b,1);
        for(unsigned n=3;n<=6;++n) integer(b,n);
        integer(b,1);empty_string(b);integer(b,25);integer(b,40);scalars(b,1);
        for(unsigned n=7;n<=10;++n) integer(b,n);
        break;
    case 44:
        scalars(b,17);integer(b,80);integer(b,2);
        for(unsigned i=0;i<2;++i) {
            integer(b,100+i);integer(b,200+i);empty_string(b);integer(b,300+i);empty_string(b);b.insert(b.end(),{1,0});
            for(unsigned j=0;j<2;++j) { integer(b,2);empty_string(b);empty_string(b); }
        }
        integer(b,2);empty_string(b);empty_string(b);
        for(unsigned i=0;i<6;++i) empty_string(b);
        break;
    case 68:
        b.push_back(1); empty_string(b); b.insert(b.end(), {0,1}); empty_string(b); break;
    case 71:
        empty_string(b); empty_string(b); integer(b,0); integer(b,7); integer(b,1);
        b.push_back(1); integer(b,123); b.push_back(0);
        for(int i=0;i<8;++i) integer(b,20+i);
        empty_string(b); empty_string(b); integer(b,51); integer(b,52); integer(b,0);
        for(int i=0;i<3;++i) empty_string(b);
        b.push_back(1); integer(b,7);
        for(int i=0;i<4;++i) empty_string(b);
        b.insert(b.end(),{1,0,1});
        for(int i=0;i<6;++i) integer(b,10+i);
        scalars(b,1); integer(b,7); integer(b,8); scalars(b,11);
        b.push_back(1); integer(b,7); empty_string(b); b.push_back(0);
        for(int i=0;i<4;++i) integer(b,7);
        empty_string(b); empty_string(b); b.insert(b.end(),{0,1});
        scalars(b,1); b.push_back(1); scalars(b,15);
        integer(b,7); empty_string(b); integer(b,7); empty_string(b);
        scalars(b,2); b.insert(b.end(),{1,0}); scalars(b,4); break;
    case 101:
        integer(b,2); empty_string(b); empty_string(b); b.resize(b.size()+2);
        scalars(b,1); integer(b,1); integer(b,2); empty_string(b); b.resize(b.size()+4); break;
    case 102: case 103: scalars(b,1); integer(b,2); break;
    case 114: integer(b,1); integer(b,2); b.push_back(1); break;
    case 117: for(unsigned i=1;i<=35;++i) integer(b,i); break;
    case 138: scalars(b,5); break;
    case 140:
        integer(b,2);
        for(unsigned s=0;s<2;++s) {
            integer(b,40+s); integer(b,2);
            for(unsigned i=0;i<2;++i) {
                integer(b,i+1);integer(b,i);integer(b,i);
                empty_string(b);empty_string(b);
                for(unsigned j=1;j<=6;++j) { const float v=static_cast<float>(j);uint32_t bits;std::memcpy(&bits,&v,4);integer(b,bits); }
                b.push_back(static_cast<uint8_t>(i));
            }
            integer(b,1);for(unsigned j=1;j<=4;++j) integer(b,j);scalars(b,3);
        }
        integer(b,1);integer(b,3);integer(b,5);break;
    case 142:
        b.push_back(1);integer(b,2);b.push_back(0);scalars(b,17);empty_string(b);b.push_back(1);break;
    case 145:
        integer(b,2);
        for(int i=0;i<2;++i) { integer(b, i+1); empty_string(b); b.insert(b.end(), {0,1}); }
        break;
    case 146:
        integer(b,2);
        for(int i=0;i<2;++i) { empty_string(b); empty_string(b); b.resize(b.size()+5); }
        break;
    case 153:
        integer(b,7); scalars(b,9); b.push_back(1); integer(b,8); integer(b,9);
        integer(b,2); integer(b,10); scalars(b,1); integer(b,11); scalars(b,1);
        b.push_back(0); integer(b,12); empty_string(b); break;
    case 154: b.push_back(1); scalars(b,1); b.push_back(0); scalars(b,3); break;
    case 155: scalars(b,1); integer(b,2); scalars(b,1); integer(b,3); empty_string(b); break;
    case 162:
        for(int i=0;i<2;++i) { integer(b,2); integer(b,3); integer(b,4); } break;
    case 166:
        scalars(b,3);
        for(int i=0;i<6;++i) { integer(b, i); b.resize(b.size()+4); } break;
    case 167: scalars(b,2); integer(b,3); b.resize(b.size()+4); break;
    case 168:
        scalars(b,2); b.push_back(1);
        for(int i=0;i<2;++i) { integer(b,i); b.resize(b.size()+4); }
        for(int i=0;i<3;++i) integer(b,i);
        b.resize(b.size()+8); break;
    case 174: empty_string(b); integer(b,2); empty_string(b); b.insert(b.end(), {0,1}); break;
    default: throw std::runtime_error("unknown synthetic component");
    }
    return b;
}
}

int main(int argc,char **argv) {
    try {
        check(argc==2 || (argc==3 && (std::string(argv[2])=="--index-templates" || std::string(argv[2])=="--collision-bounds" || std::string(argv[2])=="--sample-hitbox-tracks" || std::string(argv[2])=="--compose-hitbox-pose" || std::string(argv[2])=="--blend-hitbox-values")),"usage: native-world-reader-tests ELF [--index-templates|--collision-bounds|--sample-hitbox-tracks|--compose-hitbox-pose|--blend-hitbox-values]");
        if (!std::getenv("NOVA_REAL_TESTS") || std::string(std::getenv("NOVA_REAL_TESTS"))!="1" || !std::filesystem::is_regular_file(argv[1])) return 77;
        nova::ImportRegistry imports; nova::bionic::register_boot(imports);
        nova::bionic::CxaRegistry cxa(imports); nova::ElfImage image(argv[1],imports);
        check(nova::sha256(image.source_bytes())=="a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e","native identity");
        nova::Diagnostics diagnostics(image); image.run_initializers();
        const auto function=[&](const char *s){return nova::abi::require_function(image,s);};
        const auto ctor=function("_ZN13CMemoryStreamC1EPKvjb");
        const auto begin=function("_ZN13CMemoryStream9BeginReadEv");
        const auto destroy=function("_ZN13CMemoryStreamD1Ev");
        const auto generate=function("_Z25GenerateComponentTemplateiP13CMemoryStream");
        const auto sampleTrack=[&](uint32_t kind,const std::vector<uint16_t> &times,const std::vector<float> &values,float time) {
            const uint32_t dimensions=kind==1?3:4;
            check((kind==1 || kind==5) && times.size()>0 && times.size()<=65536 && values.size()==times.size()*dimensions,"invalid numeric animation samples");
            std::array<uint32_t,8> sampler{1,3,1,0,6,dimensions,1,(4*dimensions)<<16};
            std::array<uint32_t,3> channel{0,kind,0x19ffff};
            std::array<uint32_t,8> animation{0,1,ptr(sampler.data()),1,ptr(channel.data()),0,0,0};
            std::array<uint32_t,5> sources{2,static_cast<uint32_t>(times.size()*2),ptr(times.data()),static_cast<uint32_t>(values.size()*4),ptr(values.data())};
            std::array<uint32_t,4> cache{};
            std::array<uint32_t,3> accessor{ptr(animation.data()),ptr(sources.data()),ptr(cache.data())};
            const auto track=nova::abi::invoke(function("_ZN6glitch7collada16CColladaDatabase19getAnimationTrackExEPKNS0_10SAnimationE"),{ptr(animation.data())});
            check(track!=0,"native numeric animation handler");
            std::array<float,4> result{};
            uint32_t cursor=0,timeBits;std::memcpy(&timeBits,&time,4);
            nova::abi::invoke(function("_ZNK6glitch7collada17CAnimationTrackEx8getValueERKNS0_18SAnimationAccessorEfPvRib"),{track,ptr(accessor.data()),timeBits,ptr(result.data()),ptr(&cursor),1});
            return result;
        };
        if(argc==3 && std::string(argv[2])=="--blend-hitbox-values") {
            _setmode(_fileno(stdin),_O_BINARY);
            uint32_t count=0;std::cin.read(reinterpret_cast<char *>(&count),4);
            check(std::cin && count>0 && count<=4096,"invalid blend query count");
            const auto position=function("_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExINS_4core8vector3dIfEENS1_23CSceneNodePositionMixinIfEEEEE15getBlendedValueEPKvPKfiPv");
            const auto quaternion=function("_ZNK6glitch7collada15animation_track10CVirtualExINS1_13CApplyValueExINS_4core10quaternionENS1_25CSceneNodeQuaternionMixinIfEEEEE15getBlendedValueEPKvPKfiPv");
            std::puts("HITBOX_BLENDS 1");
            for(uint32_t i=0;i<count;++i) {
                std::array<uint32_t,2> header{};std::cin.read(reinterpret_cast<char *>(header.data()),8);
                check(std::cin && (header[0]==1 || header[0]==5) && header[1]>0 && header[1]<=16,"invalid blend query");
                std::vector<float> weights(header[1]),values(header[1]*(header[0]==1?3:4));
                std::cin.read(reinterpret_cast<char *>(weights.data()),weights.size()*4);
                std::cin.read(reinterpret_cast<char *>(values.data()),values.size()*4);
                check(bool(std::cin),"truncated blend query");
                for(float v:values) check(std::isfinite(v),"invalid blend value");
                for(float w:weights) check(std::isfinite(w) && w>0,"invalid active blend weight");
                std::array<float,4> result{};
                nova::abi::invoke(header[0]==1?position:quaternion,{0,ptr(values.data()),ptr(weights.data()),header[1],ptr(result.data())});
                std::printf("%u %.9g %.9g %.9g %.9g\n",i,result[0],result[1],result[2],result[3]);
            }
            check(std::cin.peek()==std::char_traits<char>::eof(),"trailing blend queries");
            return 0;
        }
        if(argc==3 && std::string(argv[2])=="--compose-hitbox-pose") {
            // Execute original scene hierarchy transforms over numeric TRS
            // fixtures only. No renderer, resource loading or graph is needed.
            _setmode(_fileno(stdin),_O_BINARY);
            uint32_t count=0;std::cin.read(reinterpret_cast<char *>(&count),4);
            check(std::cin && count>0 && count<=2048,"invalid pose node count");
            std::vector<std::array<uint32_t,0x100/4>> nodes(count);
            std::array<uint32_t,0x4c/4> table{};
            table[0x48/4]=function("_ZNK6glitch5scene10ISceneNode25getAbsoluteTransformationEv");
            const auto update=function("_ZN6glitch5scene10ISceneNode30updateAbsolutePositionInternalEv");
            std::puts("HITBOX_POSE 1");
            for(uint32_t i=0;i<count;++i) {
                int32_t parent=-1;std::array<float,10> values{};
                std::cin.read(reinterpret_cast<char *>(&parent),4);
                std::cin.read(reinterpret_cast<char *>(values.data()),sizeof(values));
                check(std::cin && parent>=-1 && parent<static_cast<int32_t>(i),"invalid pose parent");
                for(float v:values) check(std::isfinite(v) && std::abs(v)<=1e9f,"invalid pose transform");
                float norm=0;for(unsigned j=3;j<7;++j) norm+=values[j]*values[j];
                check(std::abs(norm-1)<.001f,"invalid pose quaternion");
                auto &node=nodes[i];node[0]=ptr(table.data());node[0xf4/4]=0x1e0;
                if(parent>=0) node[0xd0/4]=ptr(nodes[parent].data());
                std::memcpy(node.data()+0x90/4,values.data(),sizeof(values));
                nova::abi::invoke(update,{ptr(node.data())});
                const auto *result=reinterpret_cast<const float *>(node.data()+0x10/4);
                std::printf("%u",i);
                for(unsigned column=0;column<4;++column) for(unsigned axis=0;axis<3;++axis) std::printf(" %.9g",result[column*4+axis]);
                std::puts("");
            }
            check(std::cin.peek()==std::char_traits<char>::eof(),"trailing pose nodes");
            return 0;
        }
        if(argc==3 && std::string(argv[2])=="--sample-hitbox-tracks") {
            // Numeric development oracle, no original names, resources or files
            // are emitted. Inputs stay in the caller's memory and stdin pipe.
            _setmode(_fileno(stdin),_O_BINARY);
            uint32_t count=0;std::cin.read(reinterpret_cast<char *>(&count),4);
            check(std::cin && count>0 && count<=4096,"invalid sample query count");
            std::puts("HITBOX_SAMPLES 1");
            for(uint32_t i=0;i<count;++i) {
                std::array<uint32_t,3> header{};std::cin.read(reinterpret_cast<char *>(header.data()),12);
                check(std::cin && (header[0]==1 || header[0]==5) && header[1]>0 && header[1]<=65536,"invalid sample query");
                float time;std::memcpy(&time,&header[2],4);check(std::isfinite(time),"invalid sample time");
                std::vector<uint16_t> times(header[1]);std::vector<float> values(header[1]*(header[0]==1?3:4));
                std::cin.read(reinterpret_cast<char *>(times.data()),times.size()*2);
                std::cin.read(reinterpret_cast<char *>(values.data()),values.size()*4);
                check(bool(std::cin),"truncated sample query");
                for(size_t j=1;j<times.size();++j) check(times[j]>times[j-1],"unordered sample times");
                for(float v:values) check(std::isfinite(v),"nonfinite animation sample");
                const auto result=sampleTrack(header[0],times,values,time);
                std::printf("%u %.9g %.9g %.9g %.9g\n",i,result[0],result[1],result[2],result[3]);
            }
            check(std::cin.peek()==std::char_traits<char>::eof(),"trailing sample queries");
            return 0;
        }
        // Private development oracle. Read a pinned original input through
        // stdin; emit numeric boundaries only, never scripts or asset data.
        if(argc==3 && std::string(argv[2])=="--collision-bounds") {
            // Development-only numeric oracle. The caller supplies local mesh
            // bounds after vertex scaling and unlinked object placements. The
            // server package receives six resulting numbers, never guest code.
            _setmode(_fileno(stdin),_O_BINARY);
            uint32_t count=0;
            std::cin.read(reinterpret_cast<char *>(&count),4);
            check(std::cin && count>0 && count<=4096,"invalid collision bound count");
            std::array<float,3> minimum{},maximum{};
            minimum.fill(std::numeric_limits<float>::max());
            maximum.fill(-std::numeric_limits<float>::max());
            for(uint32_t i=0;i<count;++i) {
                std::array<float,12> source{};
                std::cin.read(reinterpret_cast<char *>(source.data()),sizeof(source));
                check(bool(std::cin),"truncated collision bounds");
                for(float v:source) check(std::isfinite(v) && std::abs(v)<=1e9f,"invalid collision bound scalar");
                for(size_t axis=0;axis<3;++axis) check(source[6+axis]<=source[9+axis],"inverted local bounds");
                std::array<uint8_t,0x100> object{};
                std::memcpy(object.data()+0x60,source.data()+3,12);
                std::array<float,4> q{};
                nova::abi::invoke(function("_ZNK11CGameObject21GetQuaternionRotationERN6glitch4core10quaternionE"),{ptr(object.data()),ptr(q.data())});
                const float xx=2*(q[0]*q[0]),yy=2*(q[1]*q[1]),zz=2*(q[2]*q[2]);
                const float xy=2*(q[0]*q[1]),xz=2*(q[0]*q[2]),yz=2*(q[1]*q[2]);
                const float wx=2*(q[3]*q[0]),wy=2*(q[3]*q[1]),wz=2*(q[3]*q[2]);
                // btTransform rows follow the quaternion placement used by
                // addCollisionObject, including the native diagonal ordering.
                alignas(16) std::array<float,16> transform{
                    (-yy-zz)+1,xy-wz,xz+wy,0,
                    xy+wz,(-zz-xx)+1,yz-wx,0,
                    xz-wy,yz+wx,(-yy-xx)+1,0,
                    source[0],source[1],source[2],0};
                std::array<int32_t,3> indices{0,1,2};
                // A synthetic triangle with the same local AABB is sufficient:
                // native getAabb operates on the constructor's cached extrema.
                std::array<float,9> vertices{source[6],source[7],source[8],source[9],source[10],source[11],source[6],source[10],source[8]};
                alignas(16) std::array<uint8_t,256> mesh{},shape{};
                alignas(16) std::array<float,4> low{},high{};
                nova::abi::invoke(function("_ZN26btTriangleIndexVertexArrayC1EiPiiiPfi"),{ptr(mesh.data()),1,ptr(indices.data()),12,3,ptr(vertices.data()),12});
                nova::abi::invoke(function("_ZN22btBvhTriangleMeshShapeC1EP23btStridingMeshInterfacebb"),{ptr(shape.data()),ptr(mesh.data()),1,1});
                nova::abi::invoke(function("_ZNK19btTriangleMeshShape7getAabbERK11btTransformR9btVector3S4_"),{ptr(shape.data()),ptr(transform.data()),ptr(low.data()),ptr(high.data())});
                for(size_t axis=0;axis<3;++axis) {
                    minimum[axis]=std::min(minimum[axis],low[axis]);
                    maximum[axis]=std::max(maximum[axis],high[axis]);
                }
                nova::abi::invoke(function("_ZN22btBvhTriangleMeshShapeD1Ev"),{ptr(shape.data())});
                nova::abi::invoke(function("_ZN26btTriangleIndexVertexArrayD1Ev"),{ptr(mesh.data())});
            }
            check(std::cin.peek()==std::char_traits<char>::eof(),"trailing collision bounds input");
            std::printf("COLLISION_BOUNDS 1 %.9g %.9g %.9g %.9g %.9g %.9g\n",minimum[0],minimum[1],minimum[2],maximum[0],maximum[1],maximum[2]);
            return 0;
        }
        if(argc==3) {
            _setmode(_fileno(stdin),_O_BINARY);
            Bytes data;
            std::array<char,4096> buffer{};
            while(std::cin.read(buffer.data(),buffer.size()) || std::cin.gcount()) {
                data.insert(data.end(),buffer.data(),buffer.data()+std::cin.gcount());
                check(data.size()<=2*1024*1024,"template input exceeds limit");
            }
            check(nova::sha256(data)=="26527276476c4e18b63b3b12f33b20925a63b30a5359d4a2d19a1e3cda628ff3","template input identity");
            const auto word=[&](size_t offset) {
                check(offset+4<=data.size(),"template word bounds");
                return (uint32_t(data[offset])<<24)|(uint32_t(data[offset+1])<<16)|(uint32_t(data[offset+2])<<8)|data[offset+3];
            };
            const auto short_word=[&](size_t offset) {
                check(offset+2<=data.size(),"template short bounds");
                return (uint32_t(data[offset])<<8)|data[offset+1];
            };
            std::array<uint32_t,14> stream{};
            nova::abi::invoke(ctor,{ptr(stream.data()),ptr(data.data()),static_cast<uint32_t>(data.size()),0});
            nova::abi::invoke(begin,{ptr(stream.data())});
            const auto count=word(13);
            std::puts("WORLD_INDEX 1");
            for(uint32_t index=0;index<count;++index) {
                const auto start=word(17+index*4);
                const auto end=index+1<count ? word(21+index*4) : word(4);
                const auto kind=word(start), references=word(start+4);
                const auto header=start+8+4*references;
                const auto components=short_word(header);
                stream[3]=header+6+4*components;
                for(uint32_t i=0;i<components;++i) {
                    const auto id=short_word(header+6+4*i);
                    const auto before=stream[3];
                    const auto object=nova::abi::invoke(generate,{id,ptr(stream.data())});
                    check(object && stream[3]<=end,"native template boundary outside record");
                    std::printf("T %u %u %u %u\n",kind,id,before,stream[3]);
                    const auto vtable=*reinterpret_cast<uint32_t *>(object);
                    nova::abi::invoke(*reinterpret_cast<uint32_t *>(vtable+4),{object});
                }
                // The original seeks to each indexed record. Some contain
                // unused editor/legacy trailing fields. Report, never export
                // or guess their interpretation.
                if(stream[3]!=end) std::printf("G %u %u %u\n",kind,stream[3],end);
            }
            nova::abi::invoke(destroy,{ptr(stream.data())});
            return 0;
        }
        {
            std::array<uint32_t,0x50/4> animation{};
            std::array<uint32_t,0x38/4> slot{};
            std::array<uint32_t,0x74/4> animatorTable{};
            animatorTable[0x70/4]=ptr(reinterpret_cast<void *>(&motion_track_value));
            const uint32_t animator=ptr(animatorTable.data());
            animation[0x20/4]=ptr(slot.data());animation[0x3c/4]=1;animation[0x40/4]=5;
            slot[4/4]=1000;slot[8/4]=100;slot[12/4]=1100;slot[0x18/4]=0x3f800000;
            slot[0x1c/4]=200;slot[0x20/4]=100;slot[0x24/4]=ptr(&animator);
            const auto compute=function("_ZN19CAnimationComponent18ComputeMotionDeltaEiRN6glitch4core8vector3dIfEERNS1_10quaternionES4_S6_S4_");
            for(uint32_t flags=0;flags<8;++flags) {
                slot[0x34/4]=flags;
                std::array<float,3> motion{},position{},velocity{};
                std::array<float,4> motionRotation{},rotation{};
                const auto active=nova::abi::invoke(compute,{ptr(animation.data()),0,ptr(motion.data()),ptr(motionRotation.data()),ptr(position.data()),ptr(rotation.data()),ptr(velocity.data())}) & 255u;
                check(active==(flags!=0),"native translation motion flags activation");
                for(unsigned axis=0;axis<3;++axis) {
                    const float factor=static_cast<float>(axis+1);
                    check(std::abs(position[axis]-(flags&(1u<<axis)?11.f:13.f)*factor)<1e-5f,"native root motion preserves clip-start coordinate");
                    check(std::abs(motion[axis]-(flags&(1u<<axis)?factor:0.f))<1e-5f,"native root motion delta filters axes");
                    check(std::abs(velocity[axis]-(flags&(1u<<axis)?10.f*factor:0.f))<1e-5f,"native root cycle speed filters axes");
                }
                check(rotation==std::array<float,4>{0,0,0,1} && motionRotation==rotation,"native translation-only root rotation");
            }
        }
        {
            const auto translation=sampleTrack(1,{0,30},{1,2,3,3,4,5},500);
            check(std::abs(translation[0]-2.00100112f)<1e-6f && std::abs(translation[1]-3.00100088f)<1e-6f && std::abs(translation[2]-4.00100088f)<1e-6f,"native frame-to-ms truncation in translation interpolation");
            const auto rotation=sampleTrack(5,{0,30},{0,0,0,1,0,0,1,0},500);
            check(std::abs(rotation[2]-.707662523f)<1e-6f && std::abs(rotation[3]-.706550658f)<1e-6f,"native frame-to-ms truncation in quaternion interpolation");
            // The original impact helper returns the closest point on the shot
            // segment, not the first ray/surface crossing.
            const auto overlap=function("_ZNK7Capsule23TestCapsuleIntersectionERKS_RN6glitch4core8vector3dIfEE");
            std::array<float,7> shot{0,0,0,10,0,0,0},body{5,-1,0,5,1,0,1};
            std::array<float,3> point{};
            check((nova::abi::invoke(overlap,{ptr(shot.data()),ptr(body.data()),ptr(point.data())}) & 255u)==1 && point==std::array<float,3>{5,0,0},"native closest shot point differs from entry at4");
            body={5,-1,1,5,1,1,1};
            check((nova::abi::invoke(overlap,{ptr(shot.data()),ptr(body.data()),ptr(point.data())}) & 255u)==1 && point==std::array<float,3>{5,0,0},"native capsule tangent is included");
            body[2]=body[5]=1.01f;
            check((nova::abi::invoke(overlap,{ptr(shot.data()),ptr(body.data()),ptr(point.data())}) & 255u)==0,"native capsule near miss");
            shot[6]=.02f;
            check((nova::abi::invoke(overlap,{ptr(shot.data()),ptr(body.data()),ptr(point.data())}) & 255u)==1,"native swept radius adds to target radius");
            body={12,0,0,12,0,0,1};shot[6]=0;
            check((nova::abi::invoke(overlap,{ptr(shot.data()),ptr(body.data()),ptr(point.data())}) & 255u)==0,"native finite segment excludes beyond endpoint");
            body={10.5f,0,0,10.5f,0,0,1};
            check((nova::abi::invoke(overlap,{ptr(shot.data()),ptr(body.data()),ptr(point.data())}) & 255u)==1 && point==std::array<float,3>{10,0,0},"native sphere endpoint contact");
            shot={0,0,0,.02f,0,0,0};body={.02f,0,0,.02f,0,.02f,.01f};
            check((nova::abi::invoke(overlap,{ptr(shot.data()),ptr(body.data()),ptr(point.data())}) & 255u)==0 && point==std::array<float,3>{0,0,0},"native short capsule axes use squared-length tolerance");
        }
        {
            // Synthetic animated body transforms isolate the original region
            // selection from the renderer and physics world. The aimed query
            // and the actual MP collision query are different native paths.
            std::array<uint32_t,0x160/4> component{};
            std::array<uint32_t,0x90/4> ragdoll{};
            std::array<uint32_t,7> skeleton{};
            std::array<std::array<uint32_t,12>,2> definitions{};
            std::array<std::array<uint32_t,0x60/4>,2> bodies{};
            std::array<std::array<uint32_t,0xf0/4>,2> rigid{};
            std::array<std::array<uint32_t,0x38/4>,2> shapes{};
            std::array<uint32_t,2> bodyPointers{};
            std::array<uint32_t,2> collisionInfo{0,0x1234};
            auto set=[](uint32_t *data,unsigned offset,float value){ std::memcpy(data+offset/4,&value,4); };
            for(unsigned i=0;i<2;++i) {
                bodyPointers[i]=ptr(bodies[i].data());
                bodies[i][1]=definitions[i][0]=i==0?1:3;
                bodies[i][0x1c/4]=ptr(rigid[i].data());
                set(bodies[i].data(),0x50,i==0?2.f:6.f);set(bodies[i].data(),0x54,i==0?1.f:3.f);
                set(definitions[i].data(),0x1c,i==0?2.f:6.f);set(definitions[i].data(),0x20,i==0?1.f:3.f);
                set(definitions[i].data(),0x24,i==0?1.f:3.f);
                for(unsigned diagonal:{4u,0x18u,0x2cu}) set(rigid[i].data(),diagonal,1);
                set(rigid[i].data(),0x34,5.f+i);
                rigid[i][0xc0/4]=ptr(shapes[i].data());rigid[i][0xec/4]=ptr(collisionInfo.data());
                set(shapes[i].data(),0x1c,.2f);set(shapes[i].data(),0x20,.5f);set(shapes[i].data(),0x24,.2f);shapes[i][0x34/4]=1;
            }
            skeleton[1]=ptr(definitions.data());skeleton[2]=skeleton[1]+sizeof(definitions);
            ragdoll[0x2c/4]=2;ragdoll[0x34/4]=ptr(bodyPointers.data());ragdoll[0x8c/4]=ptr(skeleton.data());
            component[0x14/4]=ptr(ragdoll.data());component[0x158/4]=0xffffffff;
            std::array<float,7> shot{0,0,0,10,0,0,0};
            std::array<uint32_t,16> target{};
            const auto intersect=function("_ZN24CPhysicsRagdollComponent20IntersectWithCapsuleERK7CapsuleR7TTargetb");
            check((nova::abi::invoke(intersect,{ptr(component.data()),ptr(shot.data()),ptr(target.data()),1})&255u)==1 && target[0x3c/4]==3,"native aimed region uses priority rather than first entry");
            check(*reinterpret_cast<float *>(&target[0x38/4])==6,"native aimed query exposes SP multiplier");
            set(definitions[0].data(),0x24,3);
            check((nova::abi::invoke(intersect,{ptr(component.data()),ptr(shot.data()),ptr(target.data()),1})&255u)==1 && target[0x3c/4]==3,"native aimed query biases the previous intersecting region");
            set(definitions[0].data(),0x24,100);
            check((nova::abi::invoke(intersect,{ptr(component.data()),ptr(shot.data()),ptr(target.data()),1})&255u)==1 && target[0x3c/4]==1,"native aimed preference is not an absolute lock");
            auto *singleton=reinterpret_cast<uint32_t *>(image.symbol("_ZN13GameMpManager9SingletonE"));
            const auto oldSingleton=*singleton;*singleton=1;shotMultiplayer=1;
            nova::abi::GuestHook multiplayer(image,"_ZN9MpManager17IsMultiplayerGameEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&shot_multiplayer)));
            nova::abi::GuestHook coop(image,"_ZN13GameMpManager14IsGameModeCoopEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&ammo_noop)));
            check((nova::abi::invoke(intersect,{ptr(component.data()),ptr(shot.data()),ptr(target.data()),0})&255u)==1 && target[0x3c/4]==1,"native MP impact uses first intersecting region");
            check(*reinterpret_cast<float *>(&target[0x38/4])==1,"native MP impact uses MP multiplier");
            set(rigid[0].data(),0x3c,5);
            check((nova::abi::invoke(intersect,{ptr(component.data()),ptr(shot.data()),ptr(target.data()),0})&255u)==1 && target[0x3c/4]==3 && *reinterpret_cast<float *>(&target[0x38/4])==3,"native MP head impact multiplier");
            *singleton=oldSingleton;shotMultiplayer=0;
        }
        {
            // Drive the original bullet event as a server-owned MP player hit.
            // Stop at the collision query so damage/presentation is not mocked
            // into a success. This distinguishes impact from the SP aim path.
            std::array<uint32_t,0x80/4> targetable{};
            std::array<uint32_t,0x160/4> owner{},shooter{};
            std::array<uint32_t,0x110/4> shooterComponents{};
            std::array<uint32_t,0x44/4> projectile{},event{};
            targetable[1]=ptr(owner.data());targetable[0x28/4]=1;targetable[0x30/4]=0xffffffff;
            reinterpret_cast<uint8_t *>(targetable.data())[0xd]=1;
            owner[0x158/4]=1;reinterpret_cast<uint8_t *>(owner.data())[0x8e]=1;
            shooter[0xe8/4]=ptr(shooterComponents.data());shooterComponents[0x108/4]=1;
            projectile[0x40/4]=1;event[1]=37;event[0x20/4]=ptr(shooter.data());event[0x24/4]=ptr(projectile.data());
            const std::array<float,7> capsule{10,0,0,20,0,0,.001f};
            std::memcpy(event.data()+2,capsule.data(),24);std::memcpy(event.data()+0x2c/4,capsule.data()+6,4);
            auto *singleton=reinterpret_cast<uint32_t *>(image.symbol("_ZN13GameMpManager9SingletonE"));
            const auto oldSingleton=*singleton;*singleton=1;
            nova::abi::GuestHook multiplayer(image,"_ZN9MpManager17IsMultiplayerGameEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&shot_multiplayer)));
            nova::abi::GuestHook server(image,"_ZN9MpManager8IsServerEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&shot_level)));
            nova::abi::GuestHook team(image,"_ZN13GameMpManager14IsTeamGameModeEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&ammo_noop)));
            nova::abi::GuestHook coop(image,"_ZN13GameMpManager14IsGameModeCoopEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&ammo_noop)));
            nova::abi::GuestHook peer(image,"_ZN13GameMpManager18GetMpPlayerFromObjEP11CGameObject",{0x55,0x89,0xe5,0x83,0xec,0x28},ptr(reinterpret_cast<void *>(&shot_level)));
            nova::abi::GuestHook dead(image,"_ZNK11CGameObject6IsDeadEv",{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},ptr(reinterpret_cast<void *>(&ammo_noop)));
            nova::abi::GuestHook health(image,"_ZNK11CGameObject9GetHealthEv",{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},ptr(reinterpret_cast<void *>(&target_health)));
            const char *onEvent="_ZN20CTargetableComponent7onEventEPK6IEventPK12EventManager";
            nova::abi::GuestCall level(image,onEvent,0x2a7,"_ZN6CLevel8GetLevelEv",ptr(reinterpret_cast<void *>(&shot_level)));
            nova::abi::GuestCall player(image,onEvent,0x2af,"_ZN6CLevel9GetPlayerEv",ptr(reinterpret_cast<void *>(&ammo_noop)));
            nova::abi::GuestHook query(image,"_ZN20CTargetableComponent21CheckCollisionWithRayERK7CapsuleR7TTargetb",{0x55,0x89,0xe5,0x57,0x56,0x53},ptr(reinterpret_cast<void *>(&target_query)));
            for(uint32_t mp:{0u,1u}) {
                shotMultiplayer=mp;targetQueryCount=0;
                nova::abi::invoke(function("_ZN20CTargetableComponent7onEventEPK6IEventPK12EventManager"),{ptr(targetable.data()),ptr(event.data()),0});
                check(targetQueryCount==1 && targetQueryMode==(mp?0u:1u),"native MP player bullet selects impact rather than aimed query");
            }
            *singleton=oldSingleton;shotMultiplayer=0;
        }
        for(unsigned kind : {0,1,6,7,12,13,21,22,23,25,40,44,58,68,71,93,101,102,103,114,117,121,138,140,142,145,146,153,154,155,162,166,167,168,174}) {
            const auto body=component(kind);
            const auto data=dictionary(body);
            std::array<uint32_t,14> stream{};
            nova::abi::invoke(ctor,{ptr(stream.data()),ptr(data.data()),static_cast<uint32_t>(data.size()),0});
            nova::abi::invoke(begin,{ptr(stream.data())});
            check(stream[3]==9,"original DICT body start");
            const auto object=nova::abi::invoke(generate,{kind,ptr(stream.data())});
            if(!object || stream[3]!=9+body.size()) {
                std::fprintf(stderr,"component %u: read %u expected %zu\n",kind,stream[3],9+body.size());
                throw std::runtime_error("original component boundary mismatch");
            }
            if(kind==0) {
                const auto *words=reinterpret_cast<uint32_t *>(object);
                check(words[1]==1234 && words[2]==0x3f800000 && words[10]==0x3f800000,"base placement offsets");
            }
            if(kind==13) {
                std::array<uint32_t,0x28/4> component{};
                std::array<uint32_t,0x80/4> owner{};
                component[1]=ptr(owner.data());component[2]=object;
                auto set=[](uint32_t address,unsigned offset,float value){ std::memcpy(reinterpret_cast<void *>(address+offset),&value,4); };
                set(ptr(owner.data()),0x70,2);set(ptr(owner.data()),0x74,3);
                set(ptr(owner.data()),0x54,10);set(ptr(owner.data()),0x58,20);set(ptr(owner.data()),0x5c,30);
                set(object,4,.5f);set(object,0x10,1);set(object,0x14,2);set(object,0x18,3);
                nova::abi::GuestHook create(image,"_ZN22CCollisionObjectSimpleC1EP11CGameObjectiRKN6glitch4core8vector3dIfEE",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&collision_construct)));
                nova::abi::GuestHook activation(image,"_ZN17btCollisionObject18setActivationStateEi",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&ammo_noop)));
                nova::abi::GuestHook dead(image,"_ZNK11CGameObject6IsDeadEv",{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},ptr(reinterpret_cast<void *>(&ammo_noop)));
                nova::abi::GuestHook position(image,"_ZN22CCollisionObjectSimple11setPositionERKN6glitch4core8vector3dIfEE",{0x55,0x89,0xe5,0x83,0xec,0x08},ptr(reinterpret_cast<void *>(&collision_position)));
                for(float height:{.25f,2.f}) {
                    set(object,8,height);
                    nova::abi::invoke(function("_ZN25CActorsCollisionComponent4LoadEP13CMemoryStream"),{ptr(component.data()),0});
                    const float total=height==.25f?2.f:6.f;
                    check(collisionDimensions==std::array<float,3>{1,total-2,0},"original radius/height scale and capsule clamp");
                    nova::abi::invoke(function("_ZN25CActorsCollisionComponent6UpdateEi"),{ptr(component.data()),0});
                    check(collisionPosition==std::array<float,3>{11,22,33+total*.5f},"original Z-up capsule center and offsets");
                    const std::array<uint32_t,1> args{ptr(component.data())};
                    check(nova::abi::call_guest_float(function("_ZNK25CActorsCollisionComponent9GetRadiusEv"),args.data(),1)==1,"original actor radius");
                    check(nova::abi::call_guest_float(function("_ZNK25CActorsCollisionComponent9GetHeightEv"),args.data(),1)==total+3,"original actor top height");
                    nova::abi::invoke(function("_ZdlPv"),{component[0x24/4]});
                }
            }
            if(kind==71) {
                std::array<uint32_t,0x300/4> weapon{};
                weapon[0x14/4]=object;
                weapon[0x50/4]=weapon[0x54/4]=weapon[0x23c/4]=0x3f800000;
                check(nova::abi::invoke(function("_ZN16CWeaponComponent21GetContinousFireDelayEi"),{ptr(weapon.data()),0})==20,"weapon fire-delay field");
                check(nova::abi::invoke(function("_ZN16CWeaponComponent9GetMaxAmoEv"),{ptr(weapon.data())})==13,"weapon reserve field");
                check(nova::abi::invoke(function("_ZN16CWeaponComponent17GetMaxRechargeAmoEv"),{ptr(weapon.data())})==14,"weapon magazine field");
                check(nova::abi::invoke(function("_ZN16CWeaponComponent11GetMaxRangeEv"),{ptr(weapon.data())})==1,"weapon range field");
            }
            if(kind==117) {
                const auto *fields=reinterpret_cast<uint32_t *>(object);
                check(fields[1]==1 && fields[4]==4 && fields[8]==8 && fields[35]==35,"original scoreboard gain field layout");
                std::array<uint32_t,0xc0/4> player{};
                const auto add=function("_ZN8MpPlayer15AddIngamePointsEii");
                nova::abi::invoke(add,{ptr(player.data()),10,5});
                nova::abi::invoke(add,{ptr(player.data()),0,99});
                nova::abi::invoke(add,{ptr(player.data()),3,2});
                check(player[0xb8/4]==13 && nova::abi::invoke(function("_ZN8MpPlayer14GetPlayerScoreEv"),{ptr(player.data())})==7,"original XP and base scoreboard are distinct");
            }
            if(kind==140) {
                const auto *fields=reinterpret_cast<uint32_t *>(object);
                const auto *skeleton=reinterpret_cast<uint32_t *>(fields[1]);
                check(fields[2]-fields[1]==2*0x1c && skeleton[0]==40,"native ragdoll skeleton layout");
                const auto *part=reinterpret_cast<uint32_t *>(skeleton[1]);
                check(skeleton[2]-skeleton[1]==2*0x30 && part[0]==1 && part[1]==0 && part[2]==0,"native ragdoll body layout");
                check(reinterpret_cast<const float *>(part)[0x14/4]==1 && reinterpret_cast<const float *>(part)[0x20/4]==4 && reinterpret_cast<const float *>(part)[0x28/4]==6,"native ragdoll radius MP multiplier and extension fields");
            }
            if(kind==40) {
                std::array<uint32_t,0x140/4> projectile{};
                projectile[0x1c/4]=object;
                projectile[0x110/4]=0x3f800000;
                const auto set=[](uint32_t address, unsigned offset, float value) {
                    std::memcpy(reinterpret_cast<void *>(address+offset),&value,4);
                };
                const auto value=[&](const char *name,uint32_t argument=0) {
                    const std::array<uint32_t,2> args{ptr(projectile.data()),argument};
                    return nova::abi::call_guest_float(function(name),args.data(),args.size());
                };
                set(object,0x24,800);set(object,0x48,50);
                check(std::abs(value("_ZN20CProjectileComponent8GetSpeedEv")-.8f)<1e-6f,"projectile speed per millisecond");
                check(std::abs(value("_ZN20CProjectileComponent23GetVehicleDamageProcentEv")-.5f)<1e-6f,"projectile vehicle percentage");
                set(object,8,10);set(object,12,30);set(object,16,20);set(object,20,5);
                for(const auto &[distance,expected]:std::array<std::pair<float,float>,5>{{{0,30},{5,30},{12.5f,20},{20,10},{30,10}}}) {
                    set(ptr(projectile.data()),0x9c,distance);
                    check(std::abs(value("_ZNK20CProjectileComponent9GetDamageEi",250)-expected)<1e-5f,"projectile distance damage");
                    *reinterpret_cast<uint8_t *>(object+0x84)=1;
                    check(std::abs(value("_ZNK20CProjectileComponent9GetDamageEi",250)-expected*.25f)<1e-5f,"projectile continuous damage duration");
                    *reinterpret_cast<uint8_t *>(object+0x84)=0;
                    *reinterpret_cast<uint8_t *>(object+0x90)=1;
                    check(std::abs(value("_ZNK20CProjectileComponent9GetDamageEi",250)-expected*.25f)<1e-5f,"railgun damage duration");
                    *reinterpret_cast<uint8_t *>(object+0x90)=0;
                }
            }
            const auto vtable=*reinterpret_cast<uint32_t *>(object);
            nova::abi::invoke(*reinterpret_cast<uint32_t *>(vtable+4),{object});
            nova::abi::invoke(destroy,{ptr(stream.data())});
        }
        {
            const char *shoot="_ZN16CWeaponComponent5ShootEP11CGameObjectbP16CComponentWeaponRKN6glitch4core8vector3dIfEES9_iffS1_iibbb";
            std::array<uint32_t,0x12c/4> weapon{},projectile{};
            std::array<uint32_t,0xa8/4> projectileTemplate{};
            std::array<uint32_t,0x58/4> mode{};
            mode[1]=200;weapon[0x1c/4]=ptr(mode.data());weapon[0x58/4]=0x44424000;weapon[0x64/4]=0x40000000;
            projectile[0x1c/4]=ptr(projectileTemplate.data());shotProjectile=ptr(projectile.data());
            const std::array<float,3> origin{10,0,0},target{20,0,0};
            auto *singleton=reinterpret_cast<uint32_t *>(image.symbol("_ZN13GameMpManager9SingletonE"));
            const auto previous=*singleton;*singleton=1;
            nova::abi::GuestCall level(image,shoot,0x183,"_ZN6CLevel8GetLevelEv",ptr(reinterpret_cast<void *>(&shot_level)));
            nova::abi::GuestHook pool(image,"_ZN10ILevelPool14GetElementByIdEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&shot_pool)));
            nova::abi::GuestHook component(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&shot_component)));
            nova::abi::GuestHook multiplayer(image,"_ZN9MpManager17IsMultiplayerGameEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&shot_multiplayer)));
            nova::abi::GuestCall clockA(image,shoot,0x1ff,"_ZN13GameMpManager12GetLocalTimeEv",ptr(reinterpret_cast<void *>(&shot_time)));
            nova::abi::GuestCall clockB(image,shoot,0x275,"_ZN13GameMpManager12GetLocalTimeEv",ptr(reinterpret_cast<void *>(&shot_time)));
            nova::abi::GuestHook launch(image,"_ZN20CProjectileComponent6LaunchERKN6glitch4core8vector3dIfEES5_iP11CGameObjectififbbb",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&shot_launch)));
            for(unsigned online:{0u,1u}) for(unsigned count:{0u,1u,3u}) {
                shotMultiplayer=online;
                shotCount=0;weapon[0x44/4]=count;
                nova::abi::invoke(function(shoot),{0x1111,0,ptr(weapon.data()),ptr(origin.data()),ptr(target.data()),100,0,0x3fa00000,0x2222,0,0,0,0,0});
                check(shotCount==count,"original number of projectiles per shot");
            }
            *singleton=previous;
        }
        {
            const char *reset="_ZN16CWeaponComponent11ResetWeaponEv";
            std::array<uint32_t,0x2a8/4> weapon{};
            std::array<uint32_t,0x54/4> definition{};
            std::array<uint32_t,0x180/4> level{};
            std::array<uint8_t,8> settings{};
            uint32_t reserve=0,magazine=0;
            weapon[0x14/4]=ptr(definition.data());weapon[0x38/4]=ptr(&reserve);weapon[0x3c/4]=ptr(&reserve)+4;
            weapon[0x44/4]=ptr(&magazine);weapon[0x48/4]=ptr(&magazine)+4;
            weapon[0x50/4]=weapon[0x54/4]=weapon[0x23c/4]=0x3f800000;
            weapon[0xf8/4]=weapon[0x104/4]=weapon[0x110/4]=0xffffffff;
            definition[0x50/4]=20;ammoLevel=ptr(level.data());
            auto *settingsCell=reinterpret_cast<uint32_t *>(image.symbol("_ZN13CGameSettings9SingletonE"));
            const auto oldSettings=*settingsCell;*settingsCell=ptr(settings.data());
            nova::abi::GuestHook state(image,"_ZN19CStatesSetComponent18SetStateOnSlotTestEiiii",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&ammo_noop)));
            nova::abi::GuestHook target(image,"_ZN6CLevel19StopTargetingEffectEv",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&ammo_noop)));
            nova::abi::GuestHook muzzle(image,"_ZN16CWeaponComponent17HideMuzzleObjectsEv",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&ammo_noop)));
            nova::abi::GuestCall levelA(image,reset,0x1ab,"_ZN6CLevel8GetLevelEv",ptr(reinterpret_cast<void *>(&ammo_level)));
            nova::abi::GuestCall levelB(image,reset,0x282,"_ZN6CLevel8GetLevelEv",ptr(reinterpret_cast<void *>(&ammo_level)));
            for(const auto &test:std::array<std::array<uint32_t,4>,4>{{{80,0,60,20},{80,4,64,20},{5,0,0,5},{0xffffffff,0,0xffffffff,20}}}) {
                definition[0x4c/4]=test[0];magazine=test[1];reserve=0;
                nova::abi::invoke(function(reset),{ptr(weapon.data())});
                check(reserve==test[2] && magazine==test[3],"original reset takes magazine refill from reserve");
            }
            for(const auto &test:std::array<std::array<uint32_t,4>,3>{{{60,0,40,20},{5,4,0,9},{0xffffffff,0,0xffffffff,20}}}) {
                reserve=test[0];magazine=test[1];
                nova::abi::invoke(function("_ZN16CWeaponComponent8DoReloadEi"),{ptr(weapon.data()),0});
                check(reserve==test[2] && magazine==test[3],"original refill conserves finite or infinite pool");
            }
            magazine=18;reserve=42;
            nova::abi::invoke(function("_ZN16CWeaponComponent8GiveAmmoEii"),{ptr(weapon.data()),0,10});
            check(magazine==20 && reserve==42,"original GiveAmmo is a capped magazine grant");
            *settingsCell=oldSettings;
        }
        {
            // Exercise the original equipment consumer with synthetic tuning.
            // Only owner/component lookup and the equipped add-on predicate are
            // replaced; arithmetic and reserve refill execute original code.
            std::array<uint32_t,0x150/4> manager{};
            std::array<uint32_t,6> entry{};
            std::array<uint32_t,0x2a8/4> weapon{};
            std::array<uint32_t,0x54/4> definition{};
            std::array<uint32_t,0x14/4> loadout{};
            std::array<uint32_t,0x84/4> tuning{};
            uint32_t reserve=0,magazine=0;
            manager[0x148/4]=ptr(entry.data());manager[0x14c/4]=ptr(entry.data()+entry.size());
            entry[0]=123;entry[3]=ptr(weapon.data());
            weapon[0x14/4]=ptr(definition.data());weapon[0x38/4]=ptr(&reserve);weapon[0x3c/4]=ptr(&reserve)+4;
            weapon[0x44/4]=ptr(&magazine);weapon[0x48/4]=ptr(&magazine)+4;
            weapon[0x50/4]=weapon[0x250/4]=0x3f800000;
            definition[0x4c/4]=80;definition[0x50/4]=20;
            loadout[0x10/4]=ptr(tuning.data());addonLoadout=ptr(loadout.data());
            tuning[0x68/4]=20;tuning[0x6c/4]=25;tuning[0x70/4]=0x3f000000;
            tuning[0x74/4]=40;tuning[0x78/4]=50;tuning[0x7c/4]=75;
            auto *singleton=reinterpret_cast<uint32_t *>(image.symbol("_ZN13GameMpManager9SingletonE"));
            const auto previous=*singleton;*singleton=1;shotMultiplayer=1;
            std::array<uint8_t,8> settings{};
            auto *settingsCell=reinterpret_cast<uint32_t *>(image.symbol("_ZN13CGameSettings9SingletonE"));
            const auto oldSettings=*settingsCell;*settingsCell=ptr(settings.data());
            nova::abi::GuestHook multiplayer(image,"_ZN9MpManager17IsMultiplayerGameEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&shot_multiplayer)));
            nova::abi::GuestHook component(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&addon_component)));
            nova::abi::GuestHook active(image,"_ZN18MPLoadoutComponent13IsAddOnActiveEii",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&addon_active)));
            for(unsigned mask:{0u,2u,4u,8u,16u,32u,64u,126u}) {
                addonMask=mask;reserve=0;magazine=3;weapon[0x250/4]=0x3f800000;
                nova::abi::invoke(function("_ZN14CWeaponManager19AddOnsModifyWeaponsEv"),{ptr(manager.data())});
                auto scalar=[&](unsigned offset) { float value;std::memcpy(&value,&weapon[offset/4],4);return value; };
                auto close=[](float a,float b) { return std::fabs(a-b)<0.000001f; };
                check(close(scalar(0x23c),mask&32?1.75f:1.f) && close(scalar(0x54),mask&32?1.75f:1.f),"original addon ammo capacity factors");
                check(close(scalar(0x244),mask&4?1.25f:1.f) && close(scalar(0x250),mask&4?.5f:1.f),"original addon fire speed factors");
                check(close(scalar(0x240),mask&16?1.5f:1.f),"original addon reload speed");
                check(close(scalar(0x248),mask&8?.6f:1.f),"original addon spread reduction");
                check(close(scalar(0x24c),mask&2?1.2f:1.f),"original addon damage multiplier");
                check((weapon[0x58/4]&255)==((mask&64)?1u:0u),"original addon suppressor flag");
                check(reserve==(mask&32?105u:60u) && magazine==(mask&32?35u:20u),"original addon pool transfer and magazine capacity");
            }
            *singleton=previous;
            *settingsCell=oldSettings;
        }
        {
            // Execute native max-health precedence with synthetic definitions.
            // Perk arithmetic, configuration fallback and health reset stay native.
            std::array<uint32_t,0x380/4> health{};
            std::array<uint32_t,4> definition{},configuration{};
            std::array<uint32_t,0x14/4> loadout{};
            std::array<uint32_t,3> tuning{};
            auto set=[](uint32_t &word,float value){std::memcpy(&word,&value,4);};
            set(definition[1],100);set(definition[3],120);
            set(configuration[1],80);set(tuning[2],25);
            health[0x10/4]=ptr(definition.data());
            loadout[0x10/4]=ptr(tuning.data());addonLoadout=ptr(loadout.data());
            nova::abi::GuestHook component(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&addon_component)));
            nova::abi::GuestHook perk(image,"_ZN18MPLoadoutComponent12IsPerkActiveEi",{0x55,0x31,0xc0,0x89,0xe5},ptr(reinterpret_cast<void *>(&health_perk)));
            for(bool configured:{false,true}) for(bool active:{false,true}) for(bool overrideHealth:{false,true}) {
                health[0x37c/4]=configured?ptr(configuration.data()):0;
                set(definition[3],overrideHealth?120.f:0.f);addonMask=active;
                health[3]=0x00ff0000;
                nova::abi::invoke(function("_ZN16CHealthComponent15MP_SetMaxHealthEv"),{ptr(health.data())});
                float current=0,maximum=0;
                std::memcpy(&current,&health[0x14/4],4);std::memcpy(&maximum,&health[0x18/4],4);
                const float base=configured?80.f:100.f;
                const float expected=active?base*1.25f:(overrideHealth?120.f:base);
                check(current==expected && maximum==expected,"original max-health configuration/perk precedence");
                check((health[3]&0x00ff0000)==0,"original max-health clears death flag");
            }
        }
        {
            // Execute the original authored-event time getter, including its
            // float32 division/truncation. No state machine or scripts run.
            std::array<uint32_t,0x150/4> component{};
            std::array<uint32_t,4> source{};
            std::array<uint32_t,0x44/4> state{};
            std::array<uint32_t,0x38/4> animation{};
            std::array<uint32_t,5> track{};
            std::array<uint32_t,5> event{};
            component[0x11c/4]=ptr(source.data());
            source[2]=ptr(state.data());source[3]=ptr(state.data()+state.size());
            state[0x2c/4]=ptr(animation.data());
            animation[1]=0x3f4ccccd; // Synthetic authored speed 0.8.
            animation[2]=ptr(track.data());animation[3]=ptr(track.data()+track.size());
            track[2]=ptr(event.data());track[3]=ptr(event.data()+event.size());
            event[1]=550;event[2]=551;event[3]=4;event[4]=7;
            uint32_t argument=0;
            const auto getter=function("_ZN19CStatesSetComponent17GetStateEventTimeEiiiRi");
            check(nova::abi::invoke(getter,{ptr(component.data()),0,0,4,ptr(&argument)})==687 && argument==7,"native refill event time");
            check(nova::abi::invoke(getter,{ptr(component.data()),0,0,5,ptr(&argument)})==0xffffffff,"native missing event time");
            std::array<uint32_t,8> slot{};
            std::array<uint32_t,9> listenerVtable{};
            std::array<uint32_t,4> listener{};
            component[0xf8/4]=ptr(slot.data());component[0xfc/4]=1;
            component[0x60/4]=ptr(listener.data());component[0xa8/4]=1;
            listener[0]=ptr(listenerVtable.data());listener[3]=0x100;
            listenerVtable[8]=ptr(reinterpret_cast<void *>(&authored_event));
            nova::abi::GuestHook speedSet(image,"_ZN19CAnimationComponent24SetCurrentAnimationSpeedEif",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&authored_speed_set)));
            nova::abi::GuestHook speedGet(image,"_ZN19CAnimationComponent30GetCurrentAnimationSpeedOnSlotEi",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&authored_speed_get)));
            nova::abi::GuestHook timeGet(image,"_ZN19CAnimationComponent29GetCurrentAnimationTimeOnSlotEi",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&authored_time_get)));
            nova::abi::GuestHook lengthGet(image,"_ZN19CAnimationComponent31GetCurrentAnimationLengthOnSlotEi",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&authored_length_get)));
            for(float multiplier:{1.f,1.25f}) {
                uint32_t bits=0;std::memcpy(&bits,&multiplier,4);
                nova::abi::invoke(function("_ZN19CStatesSetComponent18SetAnimSpeedFactorEf"),{ptr(component.data()),bits});
                check(authoredAnimationSpeed==.8f*multiplier,"original authored and equipment speed product");
                authoredEventCount=0;slot[4]=0;
                const uint32_t due=multiplier==1.f?688:550;
                authoredAnimationTime=static_cast<uint32_t>((due-1)*authoredAnimationSpeed);
                nova::abi::invoke(function("_ZN19CStatesSetComponent22UpdateTracksGameEventsEi"),{ptr(component.data()),0});
                check(authoredEventCount==0,"original refill does not occur before authored boundary");
                slot[4]=authoredAnimationTime+1;
                authoredAnimationTime=static_cast<uint32_t>(due*authoredAnimationSpeed);
                nova::abi::invoke(function("_ZN19CStatesSetComponent22UpdateTracksGameEventsEi"),{ptr(component.data()),0});
                check(authoredEventCount==1,"original refill at scaled authored boundary");
            }
        }
        {
            // Native timing APIs use the clip library without touching sample
            // tracks, a renderer or a game scene. One getter rounds the clip
            // start to its authored 30 fps boundary; do not conflate the APIs.
            std::array<uint32_t,0x60/4> animator{};
            std::array<uint32_t,0x90/4> animation_set{};
            std::array<uint32_t,5> database{};
            std::array<uint32_t,5> resource{};
            std::array<uint32_t,9> header{};
            std::array<uint32_t,0x40/4> root{};
            std::array<uint32_t,3> clip{};
            std::array<uint32_t,0x20/4> component{};
            animator[0x28/4]=ptr(animation_set.data());
            animation_set[0x44/4]=ptr(database.data());
            database[0]=ptr(resource.data());resource[4]=ptr(header.data());
            header[8]=ptr(root.data());root[14]=1;root[15]=ptr(clip.data());
            component[7]=ptr(animation_set.data());
            for(const auto &[start,end,rounded]:std::array<std::array<uint32_t,3>,4>{{{0,100,100},{15,115,115},{3833,5166,1333},{160000,162000,2000}}}) {
                clip[1]=start;clip[2]=end;
                const std::array<uint32_t,3> args{ptr(animator.data()),0,0};
                const auto length=nova::abi::call_guest_float(function("_ZN6glitch7collada21CSceneNodeAnimatorSet18getAnimationLengthEii"),args.data(),args.size());
                check(length==static_cast<float>(end-start),"native raw clip duration");
                check(nova::abi::invoke(function("_ZN19CAnimationComponent13GetAnimLengthEi"),{ptr(component.data()),0})==rounded,"native rounded clip duration");
            }
        }
        {
            // Native getters prove semantic order, not merely record length.
            // CTemplateSpawnpoint's only non-POD field is an empty GNU string.
            std::array<uint32_t,0x54/4> spawn{};
            spawn[0x34/4]=image.symbol("_ZNSbIcSt11char_traitsIcEN6glitch4core10SAllocatorIcLNS1_6memory13E_MEMORY_HINTE0EEEE4_Rep20_S_empty_rep_storageE")+12;
            auto body=base();body.insert(body.end(),{1,0,1});empty_string(body);
            for(uint32_t value:{11,22,33,44,55,2,77}) integer(body,value);
            const auto data=dictionary(body);std::array<uint32_t,14> stream{};
            nova::abi::invoke(ctor,{ptr(stream.data()),ptr(data.data()),static_cast<uint32_t>(data.size()),0});
            nova::abi::invoke(begin,{ptr(stream.data())});
            nova::abi::invoke(function("_ZN19CTemplateSpawnpoint4LoadEP13CMemoryStream"),{ptr(spawn.data()),ptr(stream.data())});
            check(stream[3]==9+body.size(),"spawn reader boundary");
            std::array<uint32_t,0x278/4> object{};object[0x26c/4]=ptr(spawn.data());
            for(const auto &[symbol,value]:std::array<std::pair<const char *,uint32_t>,6>{{
                {"_ZN17CSpawnPointObject8GetCarIDEv",11},{"_ZN17CSpawnPointObject10GetCarWPIDEv",22},
                {"_ZN17CSpawnPointObject15GetMachineGunIDEv",33},{"_ZN17CSpawnPointObject9GetBikeIDEv",44},
                {"_ZN17CSpawnPointObject9GetMechIDEv",55},{"_ZN17CSpawnPointObject9GetTeamIDEv",2}}}) {
                check(nova::abi::invoke(function(symbol),{ptr(object.data())})==value,"spawn semantic field order");
            }
            nova::abi::invoke(function("_ZN19CTemplateSpawnpointD1Ev"),{ptr(spawn.data())});
            nova::abi::invoke(destroy,{ptr(stream.data())});
        }
        {
            // Check the matrix convention used by the static collision body:
            // degrees, column vectors, Rz * Ry * Rx, no axis remapping.
            const auto rotate=function("_ZN6glitch4core8CMatrix4IfE18setRotationDegreesERKNS0_8vector3dIfEE");
            for(const auto &angles:std::array<std::array<float,3>,5>{{{0,0,0},{90,0,0},{0,90,0},{0,0,90},{17,-31,64}}}) {
                std::array<float,16> matrix{};matrix[15]=1;
                nova::abi::invoke(rotate,{ptr(matrix.data()),ptr(angles.data())});
                const double k=3.14159265358979323846/180;
                const double sx=std::sin(angles[0]*k),cx=std::cos(angles[0]*k);
                const double sy=std::sin(angles[1]*k),cy=std::cos(angles[1]*k);
                const double sz=std::sin(angles[2]*k),cz=std::cos(angles[2]*k);
                const std::array<double,9> expected{cy*cz,cy*sz,-sy,sx*sy*cz-cx*sz,sx*sy*sz+cx*cz,sx*cy,cx*sy*cz+sx*sz,cx*sy*sz-sx*cz,cx*cy};
                for(size_t i=0;i<9;++i) check(std::abs(matrix[(i/3)*4+i%3]-expected[i])<2e-6,"native collision rotation convention");
            }
        }
        {
            // The shot quantization domain is built from Bullet mesh AABBs,
            // not from tight world-space triangle bounds. Exercise the actual
            // shape constructor and getAabb with a synthetic triangle.
            alignas(16) std::array<uint8_t,256> mesh{},shape{};
            std::array<int32_t,3> indices{0,1,2};
            std::array<float,9> vertices{-1,-2,0,3,4,0,-1,4,2};
            alignas(16) std::array<float,16> transform{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0};
            alignas(16) std::array<float,4> low{},high{};
            nova::abi::invoke(function("_ZN26btTriangleIndexVertexArrayC1EiPiiiPfi"),
                {ptr(mesh.data()),1,ptr(indices.data()),12,3,ptr(vertices.data()),12});
            nova::abi::invoke(function("_ZN22btBvhTriangleMeshShapeC1EP23btStridingMeshInterfacebb"),
                {ptr(shape.data()),ptr(mesh.data()),1,1});
            nova::abi::invoke(function("_ZNK19btTriangleMeshShape7getAabbERK11btTransformR9btVector3S4_"),
                {ptr(shape.data()),ptr(transform.data()),ptr(low.data()),ptr(high.data())});
            for(size_t axis=0;axis<3;++axis) {
                const std::array<float,3> minimum{-1,-2,0},maximum{3,4,2};
                check(std::abs(low[axis]-minimum[axis])<2e-6f &&
                    std::abs(high[axis]-maximum[axis])<2e-6f,"native mesh AABB has zero margin");
            }
            nova::abi::invoke(function("_ZN22btBvhTriangleMeshShapeD1Ev"),{ptr(shape.data())});
            nova::abi::invoke(function("_ZN26btTriangleIndexVertexArrayD1Ev"),{ptr(mesh.data())});
        }
        {
            // This predicate does not dereference the physics-manager receiver.
            // Check the full serialized material domain, including signed bytes.
            const auto invisible=function("_ZN15CPhysicsManager17isColMatInvisibleEi");
            for(int material=-128;material<128;++material) {
                const auto result=nova::abi::invoke(invisible,{0,static_cast<uint32_t>(material)});
                check((result&255)==(material==18 || material==19),"native invisible collision material");
            }
            std::array<uint32_t,0x130/4> controller{};
            nova::abi::invoke(function("_ZN20CCharacterControllerC1Ev"),{ptr(controller.data())});
            check(controller[0xe4/4]==0xf926e001,"native character two-sided collision filter");
            nova::abi::invoke(function("_ZN20CCharacterControllerD1Ev"),{ptr(controller.data())});
        }
        {
            // Exercise the original menu's non-sequential mode-bit mapping
            // with a synthetic level entry, without constructing any menu.
            auto *cell=reinterpret_cast<uint32_t *>(image.symbol("_ZN14CLevelsManager9SingletonE"));
            const auto saved=*cell;
            struct Restore { uint32_t *cell; uint32_t value; ~Restore(){*cell=value;} } restore{cell,saved};
            std::array<uint32_t,8> manager{};
            std::array<uint32_t,14> entry{};
            manager[7]=ptr(entry.data());*cell=ptr(manager.data());
            const auto supported=function("_ZN12CMenuManager28IsMPLevelAvaiableForGameModeEii");
            const std::array<uint32_t,8> bits{2,4,8,32,64,16,128,1};
            for(uint32_t mode=0;mode<10;++mode) {
                for(uint32_t bit=1;bit<=128;bit<<=1) {
                    entry[13]=bit;
                    const auto result=nova::abi::invoke(supported,{0,0,mode});
                    check((result&255)==(mode<bits.size() && bits[mode]==bit),"native map mode bit mapping");
                }
            }
        }
        std::puts("PASS component readers, weapon/projectile semantics, spawn semantics, collision rotations and filters; no game, graphics or network started");
        return 0;
    } catch(const std::exception &e) {std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
