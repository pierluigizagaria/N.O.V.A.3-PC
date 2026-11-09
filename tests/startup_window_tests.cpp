#include "platform/angle.h"
#include "platform/window_modes.h"
#include "platform/startup_presentation.h"
#include <windows.h>
#include <cstdio>
#include <stdexcept>
namespace {
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
}
int main() {
    try {
        check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        for(bool fullscreen:{false,true}) {
            auto *window=SDL_CreateWindow("N.O.V.A. 3 startup test",640,360,SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
            check(window!=nullptr,SDL_GetError());
            auto hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
            check(hwnd && !IsWindowVisible(hwnd),"Native HWND remains hidden at creation");
            nova::platform::WindowSurfaceMailbox surface;
            nova::platform::WindowModes modes(window,surface);
            nova::platform::DisplaySettings settings; settings.fullscreen=fullscreen;
            check(modes.startup(settings).error.empty(),"Saved mode prepared while hidden");
            check(!IsWindowVisible(hwnd),"Display preparation must not reveal intermediate window");
            {
                nova::platform::Angle angle("d3d11"); angle.create(hwnd,640,360,0);
                glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
                angle.present();
                check(!IsWindowVisible(hwnd),"Successful first swap does not reveal HWND");
                nova::platform::StartupPresentation gate; gate.presented();
                check(gate.needs_reveal(),"Successful swap permits reveal");
                check(modes.reveal().error.empty(),"Deferred display mode activates at reveal");
                gate.revealed();
                check(gate.visible() && IsWindowVisible(hwnd),"Prepared native HWND becomes visible");
                check(modes.fullscreen()==fullscreen,"Saved fullscreen/windowed state retained");
                auto actual=surface.snapshot();
                check(actual.pixel_width>0 && actual.pixel_height>0,"Visible surface dimensions published");
                angle.presentation_size(actual.pixel_width,actual.pixel_height); angle.present();
            }
            SDL_DestroyWindow(window);
        }
        SDL_Quit(); std::puts("PASS hidden construction, first ANGLE swap and window/fullscreen reveal"); return 0;
    } catch(const std::exception &error) { SDL_Quit(); std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
