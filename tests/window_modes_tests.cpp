#include "platform/window_modes.h"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace nova::platform;
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
struct Fake {
    SDL_Window *window=reinterpret_cast<SDL_Window *>(uintptr_t(0x1000));
    SDL_WindowID id=7;
    SDL_ThreadID lane=1;
    SDL_WindowFlags flags=SDL_WINDOW_RESIZABLE|SDL_WINDOW_INPUT_FOCUS;
    int x=80,y=90,w=1000,h=600,pw=1500,ph=900;
    float scale=1.5f;
    std::string fail;
    bool denied=false;
    bool wrong_current=false,deny_resize=false,deny_maximize=false;
    SDL_DisplayID display=4;
    SDL_DisplayMode desktop{4,SDL_PIXELFORMAT_XRGB8888,1920,1080,1,60,60,1,nullptr};
    SDL_DisplayMode selected{};
    bool exclusive=false;
    std::vector<SDL_DisplayMode> modes{
        {4,SDL_PIXELFORMAT_XRGB8888,1280,720,1,60,60,1,nullptr},
        {4,SDL_PIXELFORMAT_XRGB8888,1920,1080,1,60,60,1,nullptr},
        {4,SDL_PIXELFORMAT_XRGB8888,1920,1080,1,144,144,1,nullptr},
        {4,SDL_PIXELFORMAT_XRGB8888,1920,1080,2,60,60,1,nullptr}};
    std::vector<SDL_DisplayMode *> pointers;
    std::vector<SDL_DisplayMode> listed_modes;
    bool list_live=false;
    unsigned allocated_lists=0,freed_lists=0;
    Fake() {
        for(size_t i=0;i<modes.size();++i)
            modes[i].internal=reinterpret_cast<decltype(modes[i].internal)>(uintptr_t(0x4000+i));
    }
    std::vector<std::string> calls;
    void expect(SDL_Window *value) const { check(value==window,"Only fake window may be used"); }
    bool effect(const char *name) { calls.emplace_back(name); return fail!=name; }
};
Fake *state=nullptr;
SDL_ThreadID SDLCALL lane() { return state->lane; }
SDL_WindowID SDLCALL id(SDL_Window *window) { state->expect(window); return state->id; }
SDL_WindowFlags SDLCALL flags(SDL_Window *window) { state->expect(window); return state->flags; }
bool SDLCALL position(SDL_Window *window,int *x,int *y) { state->expect(window); *x=state->x; *y=state->y; return true; }
bool SDLCALL size(SDL_Window *window,int *w,int *h) { state->expect(window); *w=state->w; *h=state->h; return true; }
bool SDLCALL pixels(SDL_Window *window,int *w,int *h) { state->expect(window); *w=state->pw; *h=state->ph; return true; }
float SDLCALL scale(SDL_Window *window) { state->expect(window); return state->scale; }
bool SDLCALL desktop(SDL_Window *window,const SDL_DisplayMode *mode) {
    state->expect(window);
    if(!state->effect(mode ? "exclusive" : "desktop")) return false;
    state->exclusive=mode!=nullptr;
    if(mode) state->selected=*mode;
    return true;
}
bool SDLCALL fullscreen(SDL_Window *window,bool enabled) {
    state->expect(window);
    if(!state->effect(enabled ? "enter" : "leave")) return false;
    if(!state->denied) {
        if(enabled) {
            state->flags|=SDL_WINDOW_FULLSCREEN;
            state->x=state->y=0; state->w=state->exclusive ? state->selected.w : 1920; state->h=state->exclusive ? state->selected.h : 1080;
            state->pw=state->exclusive ? state->selected.w : 3840;
            state->ph=state->exclusive ? state->selected.h : 2160; state->scale=2;
        } else state->flags&=~SDL_WINDOW_FULLSCREEN;
    }
    return true;
}
bool SDLCALL restore(SDL_Window *window) {
    state->expect(window);
    if(!state->effect("restore")) return false;
    state->flags&=~(SDL_WINDOW_MAXIMIZED|SDL_WINDOW_MINIMIZED); return true;
}
bool SDLCALL maximize(SDL_Window *window) {
    state->expect(window);
    if(!state->effect("maximize")) return false;
    if(!state->deny_maximize) state->flags|=SDL_WINDOW_MAXIMIZED;
    return true;
}
bool SDLCALL move(SDL_Window *window,int x,int y) {
    state->expect(window);
    if(!state->effect("move")) return false;
    state->x=x; state->y=y; return true;
}
bool SDLCALL resize(SDL_Window *window,int w,int h) {
    state->expect(window);
    if(!state->effect("resize")) return false;
    if(!state->deny_resize) { state->w=w; state->h=h; state->pw=w*2; state->ph=h*2; } return true;
}
bool SDLCALL sync(SDL_Window *window) { state->expect(window); return state->effect("sync"); }
void prepare(void *user) { check(user==state,"Expected host hook owner"); state->calls.emplace_back("release"); }
SDL_DisplayID SDLCALL display(SDL_Window *w) { state->expect(w); return state->display; }
SDL_DisplayMode **SDLCALL modes(SDL_DisplayID d,int *count) {
    check(d==state->display,"Query current fake display");
    check(!state->list_live,"Previous SDL mode array must be freed before another enumeration");
    state->list_live=true; ++state->allocated_lists;
    state->listed_modes=state->modes; state->pointers.clear();
    for(auto &mode:state->listed_modes) state->pointers.push_back(&mode);
    *count=static_cast<int>(state->pointers.size()); state->pointers.push_back(nullptr);
    return state->pointers.data();
}
const SDL_DisplayMode *SDLCALL desktop_mode(SDL_DisplayID) { return &state->desktop; }
bool SDLCALL bounds(SDL_DisplayID,SDL_Rect *out) { *out={0,0,1920,1080}; return true; }
const SDL_DisplayMode *SDLCALL current_mode(SDL_DisplayID) {
    return state->exclusive && !state->wrong_current && (state->flags&SDL_WINDOW_FULLSCREEN) ? &state->selected : &state->desktop;
}
const SDL_DisplayMode *SDLCALL selected(SDL_Window *w) { state->expect(w); return state->exclusive ? &state->selected : nullptr; }
void SDLCALL free_modes(void *allocation) {
    check(state->list_live && allocation==state->pointers.data(),"Free exactly the returned SDL mode allocation");
    // The returned array owns its mode records. Poison them on free: only a
    // complete value copy (including opaque SDL data) survives this boundary.
    for(auto &mode:state->listed_modes) mode={};
    state->list_live=false; ++state->freed_lists;
}
WindowModeApi api() {
    WindowModeApi value;
    value.display=display; value.modes=modes; value.desktop=desktop_mode;
    value.current=current_mode; value.selected=selected; value.free=free_modes;
    value.bounds=bounds;
    value.thread=lane; value.id=id; value.flags=flags; value.position=position;
    value.size=size; value.pixels=pixels; value.scale=scale;
    value.desktop_mode=desktop; value.fullscreen=fullscreen;
    value.restore=restore; value.maximize=maximize; value.move=move; value.resize=resize; value.sync=sync;
    return value;
}
SDL_Event key(Uint32 type,SDL_Keycode code,SDL_Keymod mod=SDL_KMOD_NONE,bool repeat=false,SDL_WindowID window=7) {
    SDL_Event e{}; e.type=type; e.key.windowID=window; e.key.key=code; e.key.mod=mod; e.key.repeat=repeat; return e;
}
SDL_Event window_event(Uint32 type,SDL_WindowID window=7) {
    SDL_Event e{}; e.type=type; e.window.windowID=window; return e;
}
SDL_Event button(Uint32 type) {
    SDL_Event e{}; e.type=type; e.button.windowID=7; e.button.button=SDL_BUTTON_LEFT; return e;
}
void release_enter(WindowModes &modes) {
    check(modes.route(key(SDL_EVENT_KEY_UP,SDLK_RETURN)).consume,"Shortcut release never reaches game/editor");
}
void normal_restore() {
    Fake fake; state=&fake; WindowSurfaceMailbox mailbox;
    WindowModes modes(fake.window,mailbox,api(),{&fake,prepare});
    const auto first=mailbox.snapshot();
    check(first.width==1000 && first.pixel_width==1500 && first.display_scale==1.5f,"Initial logical/pixel DPI snapshot");
    modes.refresh(); check(mailbox.snapshot().revision==first.revision,"Idle does not publish new revision");
    check(!modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT,false,8)).consume && fake.calls.empty(),"Foreign shortcut ignored");
    check(!modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN)).consume,"Plain Enter remains native");
    modes.route(key(SDL_EVENT_KEY_UP,SDLK_RETURN));
    modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_W)); modes.route(button(SDL_EVENT_MOUSE_BUTTON_DOWN));
    auto route=modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT));
    check(route.consume && route.changed && route.release_owned && route.invalidate_draw && route.error.empty(),"Borderless transition route");
    check(fake.calls==std::vector<std::string>{"release","desktop","enter","sync"},"Release ownership before SDL mutation");
    check(modes.fullscreen(),"Observed fullscreen state");
    modes.refresh(); auto full=mailbox.snapshot();
    check(full.pixel_width==3840 && full.pixel_height==2160 && full.width==1920 && full.display_scale==2,"Real pixel size crosses mailbox independently of guest size");
    const auto count=fake.calls.size();
    check(modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT,true)).consume && fake.calls.size()==count,"Repeat cannot toggle");
    check(modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_W,SDL_KMOD_NONE,true)).consume,"Held command remains quarantined");
    check(modes.route(key(SDL_EVENT_KEY_UP,SDLK_W)).consume,"Quarantined key release consumed");
    check(!modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_W)).consume,"Fresh press rearms key");
    check(modes.route(button(SDL_EVENT_MOUSE_BUTTON_UP)).consume,"Held mouse release consumed");
    SDL_Event motion{}; motion.type=SDL_EVENT_MOUSE_MOTION; motion.motion.windowID=7;
    check(modes.route(motion).consume,"Transition motion discarded");
    modes.events_drained(); check(modes.route(motion).consume,"Drain before input policy acknowledgement cannot rearm motion");
    modes.input_applied(); modes.events_drained(); check(!modes.route(motion).consume,"Acknowledged queue drain rearms motion");
    release_enter(modes); route=modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT));
    check(route.changed && route.error.empty() && !modes.fullscreen(),"Return windowed");
    check(fake.x==80 && fake.y==90 && fake.w==1000 && fake.h==600,"Normal placement and logical client size restored");
    modes.refresh(); check(mailbox.snapshot().display_scale==2,"Current monitor DPI is observed, never overwritten by saved scale");
}
void maximized_restore() {
    Fake fake; state=&fake; WindowSurfaceMailbox mailbox; WindowModes modes(fake.window,mailbox,api());
    fake.x=120; fake.y=130; fake.w=1100; fake.h=650; modes.refresh();
    fake.flags|=SDL_WINDOW_MAXIMIZED; fake.x=0; fake.y=0; fake.w=1900; fake.h=1040; modes.refresh();
    check(modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT)).changed,"Maximized enter");
    release_enter(modes);
    check(modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT)).changed,"Maximized leave");
    check((fake.flags&SDL_WINDOW_MAXIMIZED) && fake.x==120 && fake.y==130 && fake.w==1100 && fake.h==650,"Restore saved normal rect before maximizing");
    check(fake.calls.back()=="desktop" && fake.calls[fake.calls.size()-2]=="sync" &&
          fake.calls[fake.calls.size()-3]=="maximize","Maximize after placement restoration, then clear exclusive preference");
}
void failures_focus_and_minimize() {
    Fake fake; state=&fake; WindowSurfaceMailbox mailbox; WindowModes modes(fake.window,mailbox,api());
    fake.fail="enter"; auto route=modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT));
    check(!route.changed && !route.error.empty() && route.release_owned && !modes.fullscreen(),"Failed request reports failure and retains windowed state");
    release_enter(modes); fake.fail.clear(); fake.denied=true;
    route=modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT));
    check(!route.changed && !route.error.empty(),"Successful SDL request is not proof of applied mode");
    release_enter(modes); fake.denied=false;
    modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_R));
    route=modes.route(window_event(SDL_EVENT_WINDOW_FOCUS_LOST));
    check(route.release_owned && modes.route(key(SDL_EVENT_KEY_UP,SDLK_R)).consume,"Focus loss releases and quarantines commands");
    const auto count=fake.calls.size();
    modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT));
    check(fake.calls.size()==count,"Unfocused Alt+Enter cannot mutate window");
    modes.route(key(SDL_EVENT_KEY_UP,SDLK_RETURN)); modes.route(window_event(SDL_EVENT_WINDOW_FOCUS_GAINED));
    check(modes.route(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_ALT)).changed,"Focused fresh shortcut works");
    fake.flags|=SDL_WINDOW_MINIMIZED; modes.refresh();
    check(mailbox.snapshot().minimized && mailbox.snapshot().pixel_width==0,"Minimized cached nonzero pixels are not presentable");
    fake.flags&=~SDL_WINDOW_MINIMIZED; fake.pw=fake.ph=0; modes.refresh();
    check(mailbox.snapshot().minimized,"Zero drawable is safely non-presentable");
    fake.pw=3840; fake.ph=2160; modes.refresh(); check(!mailbox.snapshot().minimized,"Restore republishes current pixels");
    fake.lane=2; bool wrong_lane=false;
    try { modes.refresh(); } catch(const std::logic_error &) { wrong_lane=true; }
    check(wrong_lane,"SDL video calls reject wrong lane");
}
void mailbox_concurrency() {
    WindowSurfaceMailbox mailbox; std::atomic<bool> done=false,coherent=true;
    std::thread reader([&] {
        while(!done.load()) {
            auto value=mailbox.snapshot();
            if(value.revision && (value.height!=value.width+1 || value.pixel_width!=value.width*2 ||
               value.pixel_height!=value.height*2 || value.display_scale!=2)) coherent=false;
        }
    });
    for(int n=1;n<=10000;++n) {
        WindowSurface value; value.width=n; value.height=n+1;
        value.pixel_width=n*2; value.pixel_height=(n+1)*2; value.display_scale=2; mailbox.publish(value);
    }
    done=true; reader.join();
    const auto last=mailbox.snapshot();
    check(coherent && last.width==10000 && last.revision==10000,"Cross-thread snapshots are coherent and coalesce to latest size");
}
void presentation_rectangles() {
    WindowSurface surface; surface.width=2560; surface.height=1080;
    surface.pixel_width=2560; surface.pixel_height=1080;
    auto view=window_presentation(surface);
    check(view.guest_width==1280 && view.guest_height==720 && view.pixels.x==320 && view.pixels.y==0 &&
          view.pixels.w==1920 && view.pixels.h==1080,"21:9 window retains 16:9 guest content with pillarboxes");
    surface.width=1280; surface.height=540; surface.display_scale=2;
    view=window_presentation(surface);
    check(view.logical.x==160 && view.logical.y==0 && view.logical.w==960 && view.logical.h==540,
          "Pixel letterbox transforms to logical window coordinates independently of content DPI");
    surface.width=1024; surface.height=768; surface.pixel_width=1024; surface.pixel_height=768;
    view=window_presentation(surface);
    check(view.pixels.x==0 && view.pixels.y==96 && view.pixels.w==1024 && view.pixels.h==576,"4:3 content gets top and bottom bars");
    surface.width=1920; surface.height=1200; surface.pixel_width=3840; surface.pixel_height=2400;
    view=window_presentation(surface);
    check(view.pixels.y==120 && view.pixels.w==3840 && view.pixels.h==2160 && view.logical.y==60,"16:10 high-DPI letterbox uses integer pixels");
    surface.minimized=true; view=window_presentation(surface);
    check(view.pixels.w==0 && view.logical.w==0,"No transform for minimized window");
}
void display_settings_and_rollback() {
    Fake fake; state=&fake; WindowSurfaceMailbox mailbox;
    WindowModes controller(fake.window,mailbox,api(),{&fake,prepare});
    const auto choices=controller.choices(true);
    check(choices.size()==3 && choices[0].resolution==OutputResolution{} &&
          choices[1].resolution==OutputResolution{1280,720},"Enumerate actual modes, deduplicate refresh and reject high-density variants");
    fake.wrong_current=true;
    DisplaySettings denied; denied.fullscreen=true; denied.fullscreen_resolution={1280,720};
    const auto failed_current=controller.apply(denied);
    check(!failed_current.changed && !failed_current.error.empty() && !controller.fullscreen(),
          "Requested SDL selection without actual output mode is rejected and rolled back");
    fake.wrong_current=false; fake.calls.clear();
    DisplaySettings request; request.fullscreen=true; request.fullscreen_resolution={1280,720};
    controller.route(key(SDL_EVENT_KEY_DOWN,SDLK_W)); controller.route(button(SDL_EVENT_MOUSE_BUTTON_DOWN));
    auto route=controller.apply(request);
    check(route.changed && route.error.empty() && fake.exclusive && fake.selected.w==1280 &&
          controller.settings()==request && fake.selected.internal==fake.modes[0].internal &&
          mailbox.snapshot().pixel_width==1280 && mailbox.snapshot().pixel_height==720 &&
          !fake.list_live && fake.allocated_lists==fake.freed_lists,
          "Exclusive output uses a complete supported mode copy after freeing its enumeration array");
    check(fake.calls[0]=="release" && fake.calls[1]=="exclusive","Input cleanup precedes resolution mutation");
    check(controller.route(key(SDL_EVENT_KEY_UP,SDLK_W)).consume &&
          controller.route(button(SDL_EVENT_MOUSE_BUTTON_UP)).consume,"UI display change quarantines held physical inputs");
    const auto count=fake.calls.size(); route=controller.apply(request);
    check(!route.changed && fake.calls.size()==count,"Idle/same settings do not mutate or trigger persistence");
    request.fullscreen_resolution={1920,1080}; route=controller.apply(request);
    check(route.changed && fake.selected.refresh_rate==60,"Choose supported refresh nearest desktop");
    const auto previous=controller.settings(); fake.fail="exclusive";
    request.fullscreen_resolution={1280,720}; route=controller.apply(request);
    check(!route.changed && !route.error.empty() && controller.settings()==previous,"Failed exclusive request never reports a successful setting");
    fake.fail.clear(); request=previous; request.fullscreen=false; route=controller.apply(request);
    check(route.changed && fake.x==80 && fake.y==90 && fake.w==1000 && fake.h==600 && !fake.exclusive,
          "Exit exclusive restores original DPI-independent normal placement");
    request.window_size={1280,720}; route=controller.apply(request);
    check(route.changed && fake.w==1280 && fake.h==720,"Window-size selection applies actual client size");
    const auto stable=controller.settings(); request.window_size={1600,900};
    const auto calls=fake.calls.size(); route=controller.apply(request);
    check(!route.changed && !route.error.empty() && fake.calls.size()==calls && controller.settings()==stable,
          "Unavailable resolution refused before ownership or window mutation");
    request=stable; request.fullscreen=true; request.fullscreen_resolution={9999,9999};
    route=controller.startup(request);
    check(route.changed && !fake.exclusive && controller.settings().fullscreen_resolution==OutputResolution{},
          "Stale persisted monitor mode falls back to desktop fullscreen");
    fake.display=8; fake.desktop.displayID=8; fake.modes.clear();
    controller.refresh();
    check(controller.choices(true).size()==1,"Removed-monitor list cannot retain stale modes");
    request=controller.settings(); request.fullscreen=false; route=controller.apply(request);
    check(route.changed && route.error.empty() && !controller.fullscreen() &&
          controller.settings().window_size==OutputResolution{},
          "Leaving fullscreen after monitor replacement falls back to preserved window placement");
}
void failed_restore_retry() {
    Fake fake; state=&fake; WindowSurfaceMailbox mailbox; WindowModes controller(fake.window,mailbox,api());
    fake.flags|=SDL_WINDOW_MAXIMIZED; controller.refresh();
    DisplaySettings request; request.fullscreen=true;
    check(controller.apply(request).changed,"Maximized fullscreen preparation");
    fake.fail="resize"; request.fullscreen=false;
    auto route=controller.apply(request);
    check(!route.changed && !route.error.empty() && controller.fullscreen(),"Partial exit failure rolls back to fullscreen");
    fake.fail.clear(); route=controller.apply(request);
    check(route.changed && fake.w==1000 && fake.h==600 && (fake.flags&SDL_WINDOW_MAXIMIZED),
          "Retry preserves original normal geometry and maximized intent");
}
void startup_window_size_and_denied_restore() {
    Fake fake; state=&fake; WindowSurfaceMailbox mailbox;
    WindowModes controller(fake.window,mailbox,api());
    DisplaySettings request; request.fullscreen=true;
    request.fullscreen_resolution={1280,720}; request.window_size={1920,1080};
    check(controller.startup(request).changed && controller.fullscreen(),"Start fullscreen with independent saved window size");
    request.fullscreen=false;
    auto route=controller.apply(request);
    check(route.changed && route.error.empty() && fake.w==1920 && fake.h==1080,
          "First fullscreen exit restores the saved window size rather than constructor defaults");
    request.fullscreen=true; check(controller.apply(request).changed,"Prepare denied restore");
    fake.deny_resize=true; request.fullscreen=false;
    route=controller.apply(request);
    check(!route.changed && !route.error.empty() && controller.fullscreen(),
          "Successful SDL resize return without observed normal geometry is rejected and rolled back");
    fake.deny_resize=false;
    check(controller.apply(request).changed,"Observed normal geometry can be retried after denied restore");
    fake.flags|=SDL_WINDOW_MAXIMIZED; controller.refresh(); request.fullscreen=true;
    check(controller.apply(request).changed,"Prepare maximized intent restore");
    fake.deny_maximize=true; request.fullscreen=false; route=controller.apply(request);
    check(!route.changed && !route.error.empty() && controller.fullscreen(),
          "Successful maximize return without observed maximized state cannot claim restore success");
    fake.deny_maximize=false;
    check(controller.apply(request).changed && (fake.flags&SDL_WINDOW_MAXIMIZED),"Maximized intent survives retry");
}
void disconnected_placement() {
    Fake fake; fake.x=5000; fake.y=4000; state=&fake;
    WindowSurfaceMailbox mailbox; WindowModes controller(fake.window,mailbox,api());
    DisplaySettings request; request.fullscreen=true;
    check(controller.apply(request).changed,"Off-screen saved placement enters fullscreen");
    request.fullscreen=false;
    check(controller.apply(request).changed && fake.x==460 && fake.y==240 && fake.w==1000 && fake.h==600,
          "Disconnected monitor placement recovers inside current display while retaining client size");
}

}
int main() {
    try {
        // Deliberately no SDL_Init/CreateWindow: every reachable video effect is
        // replaced; this executable cannot fullscreen/capture the real desktop.
        normal_restore(); maximized_restore(); failures_focus_and_minimize(); mailbox_concurrency(); presentation_rectangles(); failed_restore_retry(); display_settings_and_rollback(); startup_window_size_and_denied_restore(); disconnected_placement();
        std::puts("PASS platform-window-modes (fake window, zero desktop effects)"); return 0;
    } catch(const std::exception &error) {
        std::fprintf(stderr,"window modes test: %s\n",error.what()); return 1;
    }
}
