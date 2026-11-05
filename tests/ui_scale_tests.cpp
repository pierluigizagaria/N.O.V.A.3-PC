#include "ui/native_scale.h"
#include "ui/native_hud_scale.h"
#include "ui/native_hud_nodes.h"
#include "ui/native_menu_fonts.h"
#include "ui/menu_surfaces.h"
#include "ui/native_pixel_projection.h"
#include "menu/native_menu_observer.h"
#include "camera/native_world_fov.h"
#include "abi/guest_hook.h"
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include "audio/opensl.h"
#include "platform/angle.h"
#include <GLES3/gl3.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>
using namespace nova;
namespace {
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
bool close_enough(float a,float b) { return std::abs(a-b)<.0001f; }
void pixel_math() {
    // A perspective camera and geometry at different depths distinguish a
    // true pixel similarity from changing the object's XYZ scale.
    const ui::PixelMatrix view{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const ui::PixelMatrix projection{.8f,0,0,0,0,1.4f,0,0,0,0,-1,-1,0,0,-2,0};
    const ui::PixelVector pivot{2,3,-10,1};
    const auto anchor=ui::project_vector(projection,pivot);
    for(unsigned h:{360u,720u,1080u,1440u,2160u,720u,1080u}) {
        const auto factor=ui::resolution_pixel_factor(h);
        const auto adjusted=ui::pixel_projection(projection,view,pivot,factor);
        check(bool(adjusted),"valid pixel projection");
        for(const ui::PixelVector point:std::array<ui::PixelVector,3>{{pivot,{4,1,-8,1},{-2,5,-14,1}}}) {
            const auto before=ui::project_vector(projection,point),after=ui::project_vector(*adjusted,point);
            for(unsigned axis=0;axis<2;++axis) {
                const float a=anchor[axis]/anchor[3];
                check(std::abs((after[axis]/after[3]-a)*h-(before[axis]/before[3]-a)*1080)<.001f,"projected pixel offsets independent of resolution at all depths");
            }
            check(before[2]==after[2] && before[3]==after[3],"depth and perspective division preserved");
        }
        if(h==1080)check(*adjusted==projection,"reference resolution is exact identity");
    }
    check(!ui::pixel_projection(projection,view,{0,0,0,1},2),"zero homogeneous pivot rejected");
}
uint32_t addr(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t& word(uint32_t p,unsigned off=0) { return *reinterpret_cast<uint32_t*>(p+off); }
std::array<uint32_t,300> driver{};
std::array<uint32_t,60> vtable{};
std::array<uint32_t,40> target{},rtt{},scene{},screen{},device{};
std::array<uint32_t,2> stack{};
std::array<uint32_t,4> clip{1,2,3,4};
std::array<float,16> gpu{},expected{};
uint32_t device_global=0,width=1920,height=1080;
unsigned raster=0,draws=0,flushes=0;
bool miss=false,nested_font=false,nested_radar=false;
constexpr uint64_t result=0x12345678abcd0001ull;
void matrix() {
    gpu={}; gpu[0]=2.f/width; gpu[5]=-2.f/height; gpu[10]=-1; gpu[12]=-1; gpu[13]=1; gpu[15]=1;
    std::memcpy(driver.data()+0x1ac/4,gpu.data(),sizeof gpu);
}
uint64_t __cdecl setter(uint32_t d,uint32_t mode,uint32_t m,uint32_t flag) {
    check(d==addr(driver.data()) && mode==1 && !flag,"original transform setter ABI");
    std::memcpy(gpu.data(),reinterpret_cast<void*>(m),sizeof gpu); return result;
}
uint64_t __cdecl flush(uint32_t d) { check(d==addr(driver.data()),"flush receiver"); ++flushes; return result; }
uint64_t __cdecl pixel_setter(uint32_t d,uint32_t mode,uint32_t m,uint32_t) {
    check(d==addr(driver.data()) && mode<=2,"pixel driver receiver/mode");
    if(mode==1)std::memcpy(gpu.data(),reinterpret_cast<void*>(m),sizeof gpu);
    return result;
}
void pixel_native(const ElfImage& image) {
    const auto publish=image.symbol("_ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj");
    std::array<uint32_t,80> group{},child{},foreign{},backing{};
    const auto g=addr(group.data()),c=addr(child.data());
    group[0xd4/4]=c+4;child[1]=g+0xd4;child[0xd4/4]=c+0xd4;
    const auto background=addr(backing.data());backing[0xd4/4]=background+0xd4;
    ui::PixelMatrix world{1,0,0,0,0,1,0,0,0,0,1,0,2,3,-10,1};
    std::memcpy(group.data()+4,world.data(),sizeof world);
    std::memcpy(child.data()+4,world.data(),sizeof world);
    const ui::PixelMatrix view{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const ui::PixelMatrix projection{.8f,0,0,0,0,1.4f,0,0,0,0,-1,-1,0,0,-2,0};
    const auto saved_vtable=vtable[0x58/4];vtable[0x58/4]=addr(reinterpret_cast<void*>(&pixel_setter));
    for(unsigned h:{720u,1080u,2160u,720u}) {
        abi::invoke(publish,{addr(driver.data()),0,addr(view.data()),0});
        abi::invoke(publish,{addr(driver.data()),1,addr(projection.data()),0});
        const auto saved_group=group,saved_child=child;
        {
            ui::NativePixelProjection::Scope scope({g,background},ui::resolution_pixel_factor(h),{{background,g}});
            abi::invoke(publish,{addr(driver.data()),2,c+0x10,0});
            const auto expected_projection=ui::pixel_projection(projection,view,{2,3,-10,1},ui::resolution_pixel_factor(h));
            check(gpu==*expected_projection,"original driver consumes anchored resolution projection for child geometry");
            abi::invoke(publish,{addr(driver.data()),2,background+0x10,0});
            check(gpu==*expected_projection,"health backing at the origin shares the visual shield pivot");
            abi::invoke(publish,{addr(driver.data()),2,addr(foreign.data())+0x10,0});
            check(gpu==projection,"unowned scene geometry restores native projection");
            abi::invoke(publish,{addr(driver.data()),2,c+0x10,0});
        }
        check(gpu==projection && group==saved_group && child==saved_child,"GUI scope restores GPU/CPU projection without node mutation");
        check(!std::memcmp(driver.data()+0x1ac/4,projection.data(),sizeof projection),"driver projection cache restored");
    }
    vtable[0x58/4]=saved_vtable;
}
ui::PixelMatrix input_projection{},input_view{};
uint32_t __cdecl input_p(uint32_t){return addr(input_projection.data());}
uint32_t __cdecl input_v(uint32_t){return addr(input_view.data());}
void __cdecl input_position(uint32_t,uint32_t){}
void pixel_input_native(const ElfImage& image) {
    std::array<uint32_t,420> level{};
    std::array<uint32_t,90> hud{},group{},dummy{},button{},camera{};
    std::array<uint32_t,100> camera_vtable{},button_vtable{};
    std::array<uint32_t,4> entry{addr(group.data()),0,0,0};
    const auto level_slot=image.symbol("_ZN6CLevel10m_instanceE"),wslot=image.symbol("g_ScreenW"),hslot=image.symbol("g_ScreenH");
    const auto previous_level=word(level_slot),previous_w=word(wslot),previous_h=word(hslot);
    word(level_slot)=addr(level.data());level[0x638/4]=addr(hud.data());word(wslot)=1920;word(hslot)=1080;
    hud[0x34/4]=addr(camera.data());hud[0xd0/4]=addr(entry.data());hud[0xd4/4]=addr(entry.data()+4);
    camera[0]=addr(camera_vtable.data());camera_vtable[0xf8/4]=addr(reinterpret_cast<void*>(&input_p));camera_vtable[0xfc/4]=addr(reinterpret_cast<void*>(&input_v));
    input_projection={.8f,0,0,0,0,1.4f,0,0,0,0,-1,-1,0,0,-2,0};
    input_view={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    auto world=input_view;world[12]=2;world[13]=3;world[14]=-10;
    std::memcpy(group.data()+4,world.data(),sizeof world);group[0xd0/4]=addr(dummy.data());dummy[3]=addr("dummy_hud");
    const auto clip=ui::project_vector(input_projection,{2,3,-10,1});
    const float ax=(1+clip[0]/clip[3])*.5f,ay=(1-clip[1]/clip[3])*.5f;
    const auto x=static_cast<int16_t>(std::lround(ax*1920)),y=static_cast<int16_t>(std::lround(ay*1080));
    const std::array<int16_t,4> rect{static_cast<int16_t>(x-20),static_cast<int16_t>(y-10),static_cast<int16_t>(x+20),static_cast<int16_t>(y+10)};
    button[0]=addr(button_vtable.data());button_vtable[0x48/4]=image.symbol("_ZN7CButton7SetRectERKN6glitch4core4rectIsEE");
    const std::array<int16_t,4> full{-32768,-32768,32767,32767};std::memcpy(reinterpret_cast<uint8_t*>(button.data())+0x10,full.data(),sizeof full);
    for(bool sweep:{false,true}) {
    word(wslot)=1920;word(hslot)=1080;
    if(sweep) {
        button_vtable[0x38/4]=button_vtable[0x3c/4]=addr(reinterpret_cast<void*>(&input_position));
        std::memcpy(reinterpret_cast<uint8_t*>(group.data())+0x13c,rect.data(),sizeof rect);
        abi::invoke(image.symbol("_ZN15CButtonHudSweep4InitEP4CHudP20CHudSweepControlNodeiii"),{addr(button.data()),addr(hud.data()),addr(group.data()),0,0,0});
        check(button[0xc8/4]==10,"native sweep threshold starts as one quarter of its width");
    } else abi::invoke(image.symbol("_ZN10CButtonHud4InitEP4CHudiRKN6glitch4core4rectIsEE"),{addr(button.data()),addr(hud.data()),0,addr(rect.data())});
    const auto saved=button;
    for(unsigned h:{720u,1080u,2160u,720u}) {
        word(hslot)=h;word(wslot)=h*16/9;
        for(int offset:{-25,0,19,25}) {
            const std::array<int16_t,2> point{static_cast<int16_t>(std::lround(ax*word(wslot))+offset),static_cast<int16_t>(std::lround(ay*h))};
            const auto inside=abi::invoke(image.symbol("_ZN15TouchScreenBase13IsTouchInsideERKN6glitch4core8vector2dIsEE"),{addr(button.data()),addr(point.data())});
            check(bool(inside&255)==(offset>=-20 && offset<=20),"original HUD input matches fixed-pixel bounds across resize");
            check(button==saved,"input normalization leaves native bounds unchanged");
        }
    }
    }
    word(level_slot)=previous_level;word(wslot)=previous_w;word(hslot)=previous_h;
}
uint64_t __cdecl projection(uint32_t d) {
    check(d==addr(driver.data()),"projection receiver");
    gpu={}; gpu[0]=2.f/960; gpu[5]=-2.f/640; gpu[10]=-1; gpu[12]=-1; gpu[13]=1; gpu[15]=1;
    std::memcpy(driver.data()+0x1ac/4,gpu.data(),sizeof gpu); return result;
}
uint64_t __cdecl viewport(uint32_t d,uint32_t rect) {
    check(d==addr(driver.data()) && word(rect,8)==960,"native viewport stays logical"); raster=ui::raster_factor; return result;
}
uint64_t __cdecl render(uint32_t s) {
    if(!word(s,0x4c) && !word(s,0x50)) return result;
    ++draws;
    ui::NativeScale::fixture_projection(addr(driver.data()));
    const std::array<uint32_t,4> rect{0,0,960,640};
    return ui::NativeScale::fixture_viewport(addr(driver.data()),addr(rect.data()));
}
uint64_t __cdecl hit(uint32_t,uint32_t x,uint32_t y,uint32_t ox,uint32_t oy) {
    word(ox)=x; word(oy)=y; return miss ? result&~uint64_t(255) : result;
}
uint64_t __cdecl death(uint32_t) { check(ui::menu_draw_depth>0,"pause/manager scope active"); return result; }
std::array<uint32_t,8> frame_args{1,2,std::bit_cast<uint32_t>(640.f),std::bit_cast<uint32_t>(360.f),3,0,0,255};
uint64_t __cdecl frame(uint32_t s,uint32_t f,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t a) {
    check(std::array<uint32_t,8>{s,f,x,y,flags,ox,oy,a}==frame_args,"all native sprite args unchanged");
    for(size_t i=0;i<gpu.size();++i) check(close_enough(gpu[i],expected[i]),"GPU matrix uses one anchored scale");
    return result;
}
uint64_t __cdecl font(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) {
    return ui::NativeHudScale::fixture_frame(frame_args);
}
uint64_t __cdecl radar(uint32_t) { return ui::NativeHudScale::fixture_frame(frame_args); }
uint64_t __cdecl crosshair(uint32_t receiver,uint32_t dt,uint32_t option) {
    check(receiver==123 && dt==17 && option==1,"crosshair full argument forwarding");
    return ui::NativeHudScale::fixture_frame(frame_args);
}
uint64_t __cdecl menu_content(uint32_t) {
    check(ui::menu_draw_depth>0,"menu content scope active");
    ui::NativeHudScale::fixture_font({1,2,640,360,3,255,0,0,0,0});
    return ui::NativeHudScale::fixture_frame(frame_args);
}
uint64_t __cdecl aframe(uint32_t s,uint32_t a,uint32_t f,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t alpha) {
    check(s==1 && a==2 && f==3 && x==frame_args[2] && y==frame_args[3] && flags==4 && ox==5 && oy==6 && alpha==255,"animation arguments preserved");
    for(size_t i=0;i<gpu.size();++i) check(close_enough(gpu[i],expected[i]),"animation uses shared anchored factor");
    return ui::NativeHudScale::fixture_frame(frame_args);
}
uint64_t __cdecl fmodule(uint32_t s,uint32_t f,uint32_t m,uint32_t x,uint32_t y,uint32_t flags,uint32_t ox,uint32_t oy,uint32_t alpha,uint32_t pal) {
    check(pal==8,"module palette preserved"); return aframe(s,f,m,x,y,flags,ox,oy,alpha);
}
uint64_t __cdecl module(uint32_t s,uint32_t m,uint32_t x,uint32_t y,uint32_t flags,uint32_t alpha,uint32_t pal) {
    check(s==1 && m==2 && x==frame_args[2] && y==frame_args[3] && flags==3 && alpha==255 && pal==7,"direct module arguments preserved");
    for(size_t i=0;i<gpu.size();++i) check(close_enough(gpu[i],expected[i]),"module uses shared anchored factor");
    return result;
}
int percent=100;
unsigned sprite_kind=0;
bool nested_crosshair=false,menu_inside_hud=false;
uint64_t __cdecl hud(uint32_t receiver) {
    check(receiver==123 && width*9==height*16 && target[7]==width && target[8]==height && clip==std::array<uint32_t,4>{1,2,3,4},
          "HUD and persistent resources see the actual dimensions and original clip");
    driver[0xec/4]=1; matrix(); expected=gpu;
    if(!ui::menu_draw_depth) ui::scale_projection(expected,nested_radar ? ui::fitted_anchor(width,height,{39,87}) : nested_crosshair ? ui::Point{width*.5f,height*.5f} : ui::Point{640,360},ui::hud_factor(percent));
    uint64_t answer;
    if(menu_inside_hud) {
        expected=gpu;
        answer=ui::NativeScale::fixture_pause(123);
        check(ui::menu_draw_depth==0,"nested menu scope retires before further HUD work");
    } else if(nested_crosshair) answer=ui::NativeHudScale::fixture_crosshair(123,17,1);
    else if(nested_font) answer=ui::NativeHudScale::fixture_font({1,2,640,360,3,255,0,0,0,0});
    else if(nested_radar) answer=ui::NativeHudScale::fixture_radar(123);
    else if(sprite_kind) {
        std::array<uint32_t,10> a{1,2,3,std::bit_cast<uint32_t>(640.f),std::bit_cast<uint32_t>(360.f),4,5,6,255,8};
        if(sprite_kind==8) a={1,2,std::bit_cast<uint32_t>(640.f),std::bit_cast<uint32_t>(360.f),3,255,7};
        answer=ui::NativeHudScale::fixture_sprite(sprite_kind,a);
    } else answer=ui::NativeHudScale::fixture_frame(frame_args);
    check(close_enough(gpu[0],2.f/width) && close_enough(gpu[12],-1),"draw restores CPU and GPU projection before subsequent work");
    return answer;
}
void setup() {
    driver[0]=addr(vtable.data()); vtable[0x58/4]=addr(reinterpret_cast<void*>(&setter)); vtable[0xb8/4]=addr(reinterpret_cast<void*>(&flush));
    stack[0]=stack[1]=addr(target.data());
    driver[0x114/4]=addr(stack.data()); driver[0x118/4]=addr(stack.data()+2);
    scene[0x10/4]=addr(driver.data()); rtt[1]=addr(scene.data()); rtt[2]=addr(target.data()); rtt[0x28/4]=960; rtt[0x2c/4]=640;
    screen[0x44/4]=addr(rtt.data()); device[2]=addr(driver.data()); device_global=addr(device.data());
}
std::array<std::array<uint32_t,80>,10> nodes{};
std::array<uint32_t,8> node_table{};
std::array<uint32_t,80> node_manager{};
unsigned node_updates=0,node_releases=0;
float node_factor=1;
extern "C" uint32_t __cdecl find_node_impl(uint32_t out,uint32_t manager,uint32_t name,uint32_t root) {
    check(manager==addr(node_manager.data()) && word(root)==addr(nodes[0].data()),"HUD lookup retains root and manager ABI");
    const auto text=reinterpret_cast<const char*>(name);
    unsigned i=0;
    if(!std::strcmp(text,"Dummy_hud")) i=1;
    else if(!std::strcmp(text,"weapons")) i=2;
    else if(!std::strcmp(text,"shield")) i=4;
    else if(!std::strcmp(text,"pause_button")) i=5;
    else if(!std::strcmp(text,"background_top")) i=3;
    else if(!std::strcmp(text,"background_shield")) i=7;
    word(out)=i ? addr(nodes[i].data()) : 0;
    return out;
}
__declspec(naked) uint32_t __cdecl find_node() {
    __asm {
        push ebp
        mov ebp,esp
        and esp,0xfffffff0
        sub esp,16
        mov eax,[ebp+8]
        mov [esp],eax
        mov eax,[ebp+12]
        mov [esp+4],eax
        mov eax,[ebp+16]
        mov [esp+8],eax
        mov eax,[ebp+20]
        mov [esp+12],eax
        call find_node_impl
        mov esp,ebp
        pop ebp
        ret 4
    }
}
uint32_t __cdecl node_set(uint32_t node,uint32_t scale) {
    std::memcpy(reinterpret_cast<void*>(node+0xac),reinterpret_cast<void*>(scale),12); return 0;
}
uint32_t __cdecl node_update(uint32_t root,uint32_t recursive) {
    check(root==addr(nodes[0].data()) && recursive==1,"refresh complete HUD transform tree"); ++node_updates; return 0;
}
uint32_t __cdecl node_release(uint32_t) { ++node_releases; return 0; }
uint64_t __cdecl nodes_draw(uint32_t manager,uint32_t ref) {
    check(manager==addr(node_manager.data()) && word(ref)==addr(nodes[0].data()),"native HUD draw arguments unchanged");
    for(unsigned i=0;i<nodes.size();++i) {
        // Direct siblings 2/3 and a separate named pause group 5 scale once.
        // Shield 4 is inside weapons 2: its inherited scale must not double.
        const bool scales=i==2 || i==3 || i==5;
        for(unsigned c=0;c<3;++c)
            check(close_enough(std::bit_cast<float>(nodes[i][0xac/4+c]),(c+1)*(scales ? node_factor : 1.f)),"all HUD groups share factor without double scaling descendants");
        check(nodes[i][0xa0/4]==0x12340000+i,"native anchor position unchanged");
    }
    return result;
}
void nodes_synthetic() {
    ui::RequestedScale requested;
    const auto ptr=[](unsigned i) { return addr(nodes[i].data()); };
    for(unsigned i=0;i<nodes.size();++i) {
        nodes[i][0]=addr(node_table.data()+4); // IReferenceCounted adjustment=0.
        nodes[i][0xa0/4]=0x12340000+i;
        for(unsigned c=0;c<3;++c) nodes[i][0xac/4+c]=std::bit_cast<uint32_t>(float(c+1));
    }
    nodes[1][0xd4/4]=ptr(2)+4;
    nodes[2][1]=ptr(3)+4; nodes[3][1]=ptr(6)+4; nodes[6][1]=ptr(1)+0xd4;
    nodes[6][0xd0/4]=ptr(1); node_manager[0xdc/4]=ptr(6);
    nodes[1][0xd0/4]=ptr(0); nodes[2][0xd0/4]=nodes[3][0xd0/4]=ptr(1);
    nodes[4][0xd0/4]=ptr(2); nodes[5][0xd0/4]=ptr(0);
    uint32_t root=ptr(0);
    const auto saved=nodes;
    ui::NativeHudNodes scale(requested,{addr(reinterpret_cast<void*>(&nodes_draw)),addr(reinterpret_cast<void*>(&find_node)),
        addr(reinterpret_cast<void*>(&node_set)),addr(reinterpret_cast<void*>(&node_update)),addr(reinterpret_cast<void*>(&node_release)),addr(&height)});
    for(unsigned h:{360u,720u,1080u,2160u}) for(int p:{50,100,300}) {
        height=h; requested.set(p); node_factor=1;
        check(ui::NativeHudNodes::fixture_draw(addr(node_manager.data()),addr(&root))==result,"3D HUD preserves return registers");
        check(nodes==saved,"HUD node state restored exactly after native draw");
        { ui::MenuDrawScope menu; node_factor=1;
          ui::NativeHudNodes::fixture_draw(addr(node_manager.data()),addr(&root));
          check(nodes==saved,"pause menu excludes3D HUD customization"); }

    }
    check(node_updates==0 && node_releases>0,"pixel projection leaves authored transforms intact and releases retained nodes");
}
uint32_t health_publish=0;
ui::PixelMatrix health_view{},health_projection{};
uint64_t __cdecl health_layers_draw(uint32_t,uint32_t) {
    abi::invoke(health_publish,{addr(driver.data()),0,addr(health_view.data()),0});
    abi::invoke(health_publish,{addr(driver.data()),1,addr(health_projection.data()),0});
    for(unsigned i:{4u,3u,7u}) {
        abi::invoke(health_publish,{addr(driver.data()),2,addr(nodes[i].data())+0x10,0});
        check(gpu==expected,"health bar and both sibling backings use the exact same projected pivot and factor");
    }
    for(unsigned i:{8u,9u}) {
        abi::invoke(health_publish,{addr(driver.data()),2,addr(nodes[i].data())+0x10,0});
        check(gpu==health_projection,"unowned visor/scratch coverage keeps native projection");
    }
    return result;
}
void health_layers_native(const ElfImage& image) {
    setup();nodes={};node_manager={};
    for(unsigned i=0;i<nodes.size();++i) {
        nodes[i][0]=addr(node_table.data()+4);
        nodes[i][0xd4/4]=addr(nodes[i].data())+0xd4;
        if(i)nodes[i][0xd0/4]=addr(nodes[0].data());
        ui::PixelMatrix world{1,0,0,0,0,1,0,0,0,0,1,0,float(i*3),5,-15,1};
        if(i==4){world[12]=2;world[13]=3;world[14]=-10;}
        std::memcpy(nodes[i].data()+4,world.data(),sizeof world);
    }
    health_view={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    health_projection={.8f,0,0,0,0,1.4f,0,0,0,0,-1,-1,0,0,-2,0};
    health_publish=image.symbol("_ZN6glitch5video12IVideoDriver12setTransformENS0_22E_TRANSFORMATION_STATEERKNS_4core8CMatrix4IfEEj");
    const auto saved=nodes;
    ui::RequestedScale requested;
    ui::NativePixelProjection projection_owner(image);
    ui::NativeHudNodes selection(requested,{addr(reinterpret_cast<void*>(&health_layers_draw)),addr(reinterpret_cast<void*>(&find_node)),
        addr(reinterpret_cast<void*>(&node_set)),addr(reinterpret_cast<void*>(&node_update)),addr(reinterpret_cast<void*>(&node_release)),addr(&height)});
    vtable[0x58/4]=addr(reinterpret_cast<void*>(&pixel_setter));
    uint32_t root=addr(nodes[0].data());
    for(unsigned h:{720u,1080u,2160u,720u})for(int p:{50,100,150}) {
        height=h;requested.set(p);
        expected=*ui::pixel_projection(health_projection,health_view,{2,3,-10,1},ui::resolution_pixel_factor(h)*ui::hud_factor(p));
        check(ui::NativeHudNodes::fixture_draw(addr(node_manager.data()),addr(&root))==result,"complete health-group draw preserves native return");
        check(nodes==saved && gpu==health_projection,"health-group draw restores driver state and leaves authored matrices unchanged");
    }
    vtable[0x58/4]=addr(reinterpret_cast<void*>(&setter));
}
void synthetic() {
    setup(); ui::RequestedScale requested;
    check(requested.percent()==100 && !ui::parse_percent("nan") && !ui::parse_percent("100x") && !ui::parse_percent("49"),"strict default/range parsing");
    {
        ui::NativeScale scale(requested,{addr(reinterpret_cast<void*>(&viewport)),addr(reinterpret_cast<void*>(&projection)),addr(reinterpret_cast<void*>(&render)),addr(reinterpret_cast<void*>(&hit)),addr(reinterpret_cast<void*>(&death)),addr(reinterpret_cast<void*>(&death))},addr(&height));
        ui::MenuSurfaces::publish({1,1,addr(rtt.data()),addr(target.data()),addr(driver.data()),2,960,640,100});
        for(unsigned h:{360u,720u,1080u,2160u}) for(int p:{100,75,300,50,100}) {
            height=h; screen[0x4c/4]=1;
            requested.set(p); check(ui::NativeScale::fixture_screen(addr(screen.data()))==result,"menu register return preserved");
            check(raster==2 && ui::raster_factor==1 && screen[0x4c/4]==1 && ui::menu_draw_depth==0,"density scoped; native dirty counter untouched");
            screen[0x4c/4]=0; const auto before=draws; ui::NativeScale::fixture_screen(addr(screen.data())); check(draws==before,"unchanged scale does not redraw cached menu");
            check(close_enough(gpu[0],2.f/960),"menu native projection ignores HUD slider and output resolution");
            const ui::Point old{250,125}; const auto visible=old;
            float x=0,y=0;
            check(ui::NativeScale::fixture_hit(addr(screen.data()),std::bit_cast<uint32_t>(visible.x),std::bit_cast<uint32_t>(visible.y),addr(&x),addr(&y))==result && close_enough(x,old.x) && close_enough(y,old.y),"native menu click coordinates are forwarded unchanged");
            requested.set(p==100 ? 120 : 100);
            ui::NativeScale::fixture_hit(addr(screen.data()),std::bit_cast<uint32_t>(visible.x),std::bit_cast<uint32_t>(visible.y),addr(&x),addr(&y));
            check(close_enough(x,old.x) && close_enough(y,old.y),"HUD slider cannot change menu hit coordinates");
        }
        ui::NativeScale::fixture_pause(1); ui::NativeScale::fixture_manager(1);
        check(!ui::menu_draw_depth,"all menu scopes restore nesting depth");
        ui::MenuSurfaces::retire(1,1);
    }
    {
        ui::NativeHudScale scale(requested,{{addr(reinterpret_cast<void*>(&hud)),addr(reinterpret_cast<void*>(&frame)),0,addr(reinterpret_cast<void*>(&font)),0,addr(reinterpret_cast<void*>(&radar)),addr(reinterpret_cast<void*>(&aframe)),addr(reinterpret_cast<void*>(&fmodule)),addr(reinterpret_cast<void*>(&module)),addr(reinterpret_cast<void*>(&crosshair))},addr(&device_global),addr(&width),addr(&height),addr(clip.data())});
        for(unsigned w:{640u,1280u,1920u,3840u,1280u,1920u,640u}) for(int p:{50,100,150,300}) {
            width=w; height=w*9/16; target[7]=width; target[8]=height; percent=p; requested.set(p);
            for(unsigned kind=0;kind<7;++kind) {
                nested_font=kind==1; nested_radar=kind==2; sprite_kind=kind>=3 && kind<6 ? kind+3 : 0; nested_crosshair=kind==6;
                check(ui::NativeHudScale::fixture_hud(123)==result,"HUD return registers preserved");
                check(clip==std::array<uint32_t,4>{1,2,3,4} && width==w && height==w*9/16 && target[7]==width && target[8]==height,"HUD never changes real display metrics or clip");
            }
        }
        {
            ui::NativeScale menus(requested,{addr(reinterpret_cast<void*>(&viewport)),addr(reinterpret_cast<void*>(&projection)),addr(reinterpret_cast<void*>(&menu_content)),addr(reinterpret_cast<void*>(&hit)),addr(reinterpret_cast<void*>(&menu_content)),addr(reinterpret_cast<void*>(&menu_content))});
            requested.set(300); percent=300; nested_font=nested_radar=nested_crosshair=false; sprite_kind=0;
            menu_inside_hud=true; ui::NativeHudScale::fixture_hud(123); menu_inside_hud=false;
            expected=gpu; ui::NativeScale::fixture_screen(123); ui::NativeScale::fixture_manager(123);
            { ui::MenuDrawScope menu; ui::NativeHudScale::fixture_hud(123); }
            check(!ui::menu_draw_depth,"HUD and menu scopes restore after interleaving");
        }
        // At100% do not even query host-side HUD target/resource state.
        requested.set(100); percent=100; nested_font=nested_radar=nested_crosshair=false; sprite_kind=0;
        const auto old_device=device_global; device_global=0; const auto old_flushes=flushes;
        ui::NativeHudScale::fixture_hud(123);
        check(flushes==old_flushes,"100 percent is the original draw without projection or resource edits");
        device_global=old_device;
        expected=gpu; ui::NativeHudScale::fixture_frame(frame_args);
        check(flushes>0,"transformed draws flush batches on both sides");
    }
}
void image_pattern() {
    // A real sampled image, enlarged RTT and final fitted composition. Unlike
    // the solid coverage quad, this observes size, anchor and stroke contrast.
    platform::Angle angle("d3d11"); angle.create(nullptr,2048,2048,0);
    setup(); ui::RequestedScale requested;
    ui::NativeScale scale(requested,{addr(reinterpret_cast<void*>(&viewport)),addr(reinterpret_cast<void*>(&projection)),
        addr(reinterpret_cast<void*>(&render)),addr(reinterpret_cast<void*>(&hit)),addr(reinterpret_cast<void*>(&death)),addr(reinterpret_cast<void*>(&death))},addr(&height));
    ui::MenuSurfaces::publish({1,1,addr(rtt.data()),addr(target.data()),addr(driver.data()),2,960,640,100});
    auto shader=[](GLenum type,const char* source) {
        auto id=glCreateShader(type); glShaderSource(id,1,&source,nullptr); glCompileShader(id);
        GLint ok=0; glGetShaderiv(id,GL_COMPILE_STATUS,&ok); check(ok!=0,"pattern shader compiles"); return id;
    };
    const auto vs=shader(GL_VERTEX_SHADER,"#version 300 es\nuniform mat4 matrix; uniform vec4 rect; uniform vec4 uvrect; out vec2 uv; void main(){vec2 q=vec2(float(gl_VertexID&1),float(gl_VertexID>>1)); gl_Position=matrix*vec4(mix(rect.xy,rect.zw,q),0,1); uv=mix(uvrect.xy,uvrect.zw,q);}");
    const auto fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision mediump float; uniform sampler2D source; in vec2 uv; out vec4 color; void main(){color=texture(source,uv);}");
    const auto program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
    GLint linked=0; glGetProgramiv(program,GL_LINK_STATUS,&linked); check(linked!=0,"pattern program links");
    glUseProgram(program); glUniform1i(glGetUniformLocation(program,"source"),0);
    GLuint vao=0,texture[2]{},fbo=0; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenTextures(2,texture); glActiveTexture(GL_TEXTURE0);
    std::array<uint8_t,40*40*4> pixels{};
    for(unsigned y=0;y<40;++y) for(unsigned x=0;x<40;++x) {
        const auto i=4*(y*40+x); const uint8_t color=((x/4)%2) ? 255 : 0;
        pixels[i]=pixels[i+1]=pixels[i+2]=color; pixels[i+3]=255;
    }
    for(unsigned i=0;i<2;++i) {
        glBindTexture(GL_TEXTURE_2D,texture[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,i ? 2048 : 40,i ? 2048 : 40,0,GL_RGBA,GL_UNSIGNED_BYTE,i ? nullptr : pixels.data());
    }
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture[1],0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"pattern RTT complete");
    glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glClearColor(0,0,0,0);
    const auto matrix_loc=glGetUniformLocation(program,"matrix"),rect_loc=glGetUniformLocation(program,"rect"),uv_loc=glGetUniformLocation(program,"uvrect");
    for(unsigned h:{360u,720u,1080u}) for(int p:{50,100,300}) {
        height=h; requested.set(p); screen[0x4c/4]=1; ui::NativeScale::fixture_screen(addr(screen.data()));
        glBindFramebuffer(GL_FRAMEBUFFER,fbo); glViewport(0,0,1920,1280); glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D,texture[0]); glUniformMatrix4fv(matrix_loc,1,GL_FALSE,gpu.data());
        // 40 reference pixels at720p in a fitted960x640 logical menu.
        constexpr float half=20.f*640/720;
        glUniform4f(rect_loc,480-half,320-half,480+half,320+half); glUniform4f(uv_loc,0,0,1,1);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        const unsigned w=h*16/9;
        glBindFramebuffer(GL_FRAMEBUFFER,0); glViewport(0,0,w,h); glClear(GL_COLOR_BUFFER_BIT);
        std::array<float,16> output{}; output[0]=2.f/w; output[5]=-2.f/h; output[10]=-1; output[12]=-1; output[13]=1; output[15]=1;
        glUniformMatrix4fv(matrix_loc,1,GL_FALSE,output.data());
        const float left=(w-h*1.5f)*.5f;
        glUniform4f(rect_loc,left,0,w-left,float(h)); glUniform4f(uv_loc,0,.625f,.9375f,0);
        glBindTexture(GL_TEXTURE_2D,texture[1]); glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        std::vector<uint8_t> row(w*4),column(h*4);
        glReadPixels(0,h/2,w,1,GL_RGBA,GL_UNSIGNED_BYTE,row.data());
        glReadPixels(w/2,0,1,h,GL_RGBA,GL_UNSIGNED_BYTE,column.data());
        for(const auto* line:{&row,&column}) {
            const auto count=static_cast<int>(line->size()/4); int first=count,last=-1;
            for(int i=0;i<count;++i) if((*line)[i*4+3]>=128) { first=std::min(first,i); last=i; }
            check(std::abs((last-first+1)-40*int(h)/720)<=2,"menu image follows native proportions regardless of HUD slider");
            check(std::abs((first+last+1)*.5f-count*.5f)<=1,"final composed image retains center anchor");
        }
        int low=255,high=0;
        for(unsigned x=w/2-unsigned(15*h/720);x<w/2+unsigned(15*h/720);++x) {
            low=std::min(low,int(row[x*4])); high=std::max(high,int(row[x*4]));
        }
        check(high-low>=160,"sampled four-pixel strokes retain contrast through both filtering passes");
        check(glGetError()==GL_NO_ERROR,"pattern GPU operations valid");
    }
    glDeleteFramebuffers(1,&fbo); glDeleteTextures(2,texture); glDeleteVertexArrays(1,&vao);
    glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs); ui::MenuSurfaces::retire(1,1);
}
void real(const char* path) {
    ImportRegistry imports; bionic::register_boot(imports); audio::register_opensl(imports); gles::register_imports(imports); bionic::CxaRegistry cxa(imports);
    ElfImage image(path,imports);
    std::vector<std::pair<uint32_t,std::vector<uint8_t>>> saved;
    for(const auto& segment:image.segments()) if(segment.flags&1) {
        const auto p=image.address(segment.vaddr,segment.filesz);
        saved.push_back({p,{reinterpret_cast<uint8_t*>(p),reinterpret_cast<uint8_t*>(p)+segment.filesz}});
    }
    setup();
    std::array<uint8_t,16> projection_prefix{},hit_prefix{};
    std::memcpy(projection_prefix.data(),reinterpret_cast<void*>(image.address(0x8c1c80)),projection_prefix.size());
    std::memcpy(hit_prefix.data(),reinterpret_cast<void*>(image.address(0x547fc0)),hit_prefix.size());
    {
        platform::Angle angle("d3d11"); angle.create(nullptr,2048,2048,0);
        health_layers_native(image);
        menu::NativeMenuObserver observer(image,false,true); ui::RequestedScale requested;
        ui::NativeScale scale(image,requested); ui::NativeHudScale hud_scale(image,requested); ui::NativeHudNodes hud_nodes(image,requested);
        ui::NativeMenuFonts menu_fonts(image);
        camera::NativeWorldFov optics(image,{},nullptr,nullptr,{},true);
        pixel_native(image);
        pixel_input_native(image);
        check(!std::memcmp(projection_prefix.data(),reinterpret_cast<void*>(image.address(0x8c1c80)),projection_prefix.size()) &&
              !std::memcmp(hit_prefix.data(),reinterpret_cast<void*>(image.address(0x547fc0)),hit_prefix.size()),"logical menu projection and input stay native until individual widget anchors are bound");
        const auto device_slot=image.symbol("g_device"),old_device=word(device_slot);
        word(device_slot)=addr(device.data());
        for(unsigned w:{640u,1280u,1920u,3840u,1280u,1920u}) {
            target[7]=w; target[8]=w*9/16;
            const auto anchor=ui::fitted_anchor(w,w*9/16,{39,87});
            const auto x=abi::invoke(abi::require_function(image,"_ZN11Application13GetScaledPosXEi"),{0,39});
            const auto y=abi::invoke(abi::require_function(image,"_ZN11Application13GetScaledPosYEi"),{0,87});
            check(anchor.x==float(x) && anchor.y==float(y),"radar anchor agrees with original native calculation across resize roundtrips");
        }
        word(device_slot)=old_device;
        // The production controls owner hooks CHud::Render separately. Its
        // entry detour must remain compatible with our single internal call.
        abi::GuestHook controls_hud(image,"_ZN4CHud6RenderEv",{0x55,0x89,0xe5,0x57,0x56,0x53},addr(reinterpret_cast<void*>(&death)));
        std::array<uint32_t,80> node{};
        const std::array<float,3> node_scale{.5f,1.f,3.f};
        abi::invoke(abi::require_function(image,"_ZN6glitch5scene10ISceneNode8setScaleERKNS_4core8vector3dIfEE"),{addr(node.data()),addr(node_scale.data())});
        check(!std::memcmp(node.data()+0xac/4,node_scale.data(),sizeof node_scale) && (node[0xf4/4]&0x120)==0x120,"actual native setter stores scale and invalidates transforms");
        check(abi::invoke(abi::require_function(image,"_ZNK6glitch5scene10ISceneNode8getScaleEv"),{addr(node.data())})==addr(node.data())+0xac,"actual native scale layout agrees");
        check(abi::invoke(abi::require_function(image,"_ZNK6glitch5scene10ISceneNode11getChildrenEv"),{addr(node.data())})==addr(node.data())+0xd4,"actual native child list layout agrees");
        target[7]=960; target[8]=640;
        abi::invoke(abi::require_function(image,"_ZN6glitch5video19CCommonGLDriverBase15set2DProjectionEv"),{addr(driver.data())});
        check(close_enough(gpu[0],2.f/960) && close_enough(gpu[5],-2.f/640),"actual original projection consumes logical target rectangle");
        // Isolated native viewport body: its exact capability singleton read is
        // supplied by an inert eight-byte object, without guest initialization.
        std::array<uint8_t,8> caps{};
        const auto caps_global=word(image.address(0x113e2ec,4));
        const auto old_caps=word(caps_global); word(caps_global)=addr(caps.data());
        const auto native_viewport=abi::require_function(image,"_ZN6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE15setViewportImplERKNS_4core4rectIiEE");
        ui::MenuSurfaces::publish({1,1,addr(rtt.data()),addr(target.data()),addr(driver.data()),2,960,640,100});
        const std::array<uint32_t,4> rect{0,0,960,640};
        driver[0xec/4]=1; driver[0x374/4]=1;
        abi::invoke(native_viewport,{addr(driver.data()),addr(rect.data())});
        std::array<GLint,4> vp{}; glGetIntegerv(GL_VIEWPORT,vp.data());
        check(vp==std::array<GLint,4>{0,0,1920,1280} && driver[0x36c/4]==960 && driver[0x370/4]==640,
              "actual native viewport and GLES wrapper increase pixel density without changing cached logical layout");
        auto shader=[](GLenum type,const char* source) {
            auto id=glCreateShader(type); glShaderSource(id,1,&source,nullptr); glCompileShader(id);
            GLint ok=0; glGetShaderiv(id,GL_COMPILE_STATUS,&ok); check(ok!=0,"UI fixture shader compiles"); return id;
        };
        const auto vs=shader(GL_VERTEX_SHADER,"#version 300 es\nuniform mat4 matrix; void main(){vec2 p=vec2(float(gl_VertexID&1)*960.,float(gl_VertexID>>1)*640.); gl_Position=matrix*vec4(p,0,1);}");
        const auto fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision mediump float; out vec4 color; void main(){color=vec4(1,0,0,1);}");
        const auto program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
        GLint linked=0; glGetProgramiv(program,GL_LINK_STATUS,&linked); check(linked!=0,"UI fixture program links");
        glUseProgram(program); glUniformMatrix4fv(glGetUniformLocation(program,"matrix"),1,GL_FALSE,gpu.data());
        GLuint vao=0; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
        glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        std::array<uint8_t,4> pixel{}; glReadPixels(1800,1200,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
        check(pixel[0]==255 && pixel[1]==0,"logical menu reaches the enlarged lower-right viewport, not just the upper-left quarter");
        glReadPixels(2000,1200,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
        check(pixel[0]==0,"native logical menu remains inside its UV-covered viewport");
        glDeleteVertexArrays(1,&vao); glDeleteProgram(program); glDeleteShader(vs); glDeleteShader(fs);
        const std::array<uint32_t,4> allocation{0,0,2048,2048}; driver[0x374/4]=1;
        abi::invoke(native_viewport,{addr(driver.data()),addr(allocation.data())});
        glGetIntegerv(GL_VIEWPORT,vp.data());
        check(vp==std::array<GLint,4>{0,0,2048,2048},"initial allocation-sized bind is never scaled twice");
        ui::MenuSurfaces::retire(1,1); word(caps_global)=old_caps;
    }
    for(const auto& item:saved) check(!std::memcmp(reinterpret_cast<void*>(item.first),item.second.data(),item.second.size()),"every original executable byte restored after coexistence");
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2) { if(!std::filesystem::exists(argv[1])) return 77; real(argv[1]); }
        else { pixel_math(); synthetic(); nodes_synthetic(); image_pattern(); }
        std::puts("UI scale checks passed; no game initializers or gameplay executed"); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"UI scale FAIL %s\n",e.what()); return 1; }
}
