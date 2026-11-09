#include "launcher/skin.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace nova::launcher;
namespace {
struct Surface {
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    uint32_t *pixels=nullptr;
    Surface(int w,int h) {
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void **>(&pixels),nullptr,0);
        if(!dc || !bitmap || !pixels)throw std::runtime_error("Cannot allocate paint fixture.");
        previous=SelectObject(dc,bitmap);
    }
    ~Surface(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}
};
}
int main(int argc,char **) {
    try {
        Skin skin;skin.load("local/test-profiles/launcher-paint-no-art");
        for(const int width:{800,1200,983}) {
            const int height=MulDiv(width,canvas_height,canvas_width);
            Surface reference(width,height),child(width,height);
            for(const bool error:{false,true}) for(const int interaction:{0,1,2,3}) {
                SkinState state;state.error=error;state.select_label=L"Select APK";
                state.heading=L"Welcome";state.description=L"Owner-draw regression fixture";
                const auto controls=skin_controls(state);
                for(const int id:{select_id,retry_id,language_first_id,language_first_id+5}) {
                    const auto box=id==select_id?controls.select:id==retry_id?controls.retry:controls.language[id-language_first_id];
                    if(box.empty())continue;
                    state.hover=interaction==1?id:0;state.pressed=interaction==2?id:0;
                    state.focused=interaction==3?id:0;
                    const POINT origin{MulDiv(box.x,width,canvas_width),MulDiv(box.y,height,canvas_height)};
                    const RECT bounds{0,0,MulDiv(box.w,width,canvas_width),MulDiv(box.h,height,canvas_height)};
                    skin.draw(reference.dc,width,height,state);
                    GdiFlush();
                    constexpr uint32_t sentinel=0x003917c5;
                    std::fill_n(child.pixels,static_cast<size_t>(width)*height,sentinel);
                    // Deliberately leave the DC unclipped: WM_DRAWITEM must not
                    // assume a control-sized clip or alter neighboring pixels.
                    SetViewportOrgEx(child.dc,origin.x,origin.y,nullptr);
                    if(argc>1)skin.draw(child.dc,width,height,state,origin.x,origin.y);
                    else skin.draw_control(child.dc,width,height,state,origin,bounds);
                    SetViewportOrgEx(child.dc,0,0,nullptr);GdiFlush();
                    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                        const bool inside=x>=origin.x && x<origin.x+bounds.right && y>=origin.y && y<origin.y+bounds.bottom;
                        const auto index=static_cast<size_t>(y)*width+x;
                        const auto expected=inside?reference.pixels[index]:sentinel;
                        if((child.pixels[index]&0xffffff)!=(expected&0xffffff))
                            throw std::runtime_error(inside?"Control differs from parent composition.":"Control paint escaped its bounds.");
                    }
                }
            }
        }
        std::cout<<"PASS: bounded control painting matches parent at 100%, 150% and fractional scale; idle/error, hover/press/focus.\n";
        return 0;
    } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
