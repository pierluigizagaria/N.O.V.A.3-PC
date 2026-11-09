#include "platform/window_modes.h"
#include "platform/presentation.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace nova::platform {
WindowPresentation window_presentation(const WindowSurface &surface,int guest_width,int guest_height) {
    if(guest_width<=0 || guest_height<=0) throw std::invalid_argument("Invalid guest presentation dimensions");
    WindowPresentation result; result.guest_width=guest_width; result.guest_height=guest_height;
    if(surface.minimized || surface.width<=0 || surface.height<=0 || surface.pixel_width<=0 || surface.pixel_height<=0)
        return result;
    const auto fitted=fit_presentation(surface.pixel_width,surface.pixel_height,guest_width,guest_height);
    auto &rect=result.pixels; rect={fitted.x,fitted.y,fitted.w,fitted.h};
    const auto xscale=double(surface.width)/surface.pixel_width,yscale=double(surface.height)/surface.pixel_height;
    result.logical={static_cast<float>(rect.x*xscale),static_cast<float>(rect.y*yscale),
                    static_cast<float>(rect.w*xscale),static_cast<float>(rect.h*yscale)};
    return result;
}
void WindowSurfaceMailbox::publish(WindowSurface value) {
    std::lock_guard lock(mutex_);
    if(value.width==value_.width && value.height==value_.height &&
       value.pixel_width==value_.pixel_width && value.pixel_height==value_.pixel_height &&
       value.display_scale==value_.display_scale && value.minimized==value_.minimized) return;
    value.revision=value_.revision+1;
    value_=value;
}
WindowSurface WindowSurfaceMailbox::snapshot() const {
    std::lock_guard lock(mutex_);
    return value_;
}
WindowModes::WindowModes(SDL_Window *window,WindowSurfaceMailbox &surface,WindowModeApi api,WindowModeHooks hooks)
    :window_(window),id_(0),ui_(0),surface_(surface),api_(api),hooks_(hooks) {
    if(!window || !api.thread || !api.id || !api.flags || !api.position || !api.size ||
       !api.pixels || !api.scale || !api.desktop_mode || !api.fullscreen || !api.restore ||
       !api.maximize || !api.move || !api.resize || !api.sync || !api.show || !api.display || !api.modes ||
       !api.desktop || !api.current || !api.bounds || !api.selected || !api.free)
        throw std::invalid_argument("Incomplete window mode adapter");
    ui_=api_.thread(); id_=api_.id(window_);
    if(!id_) throw std::invalid_argument("Window mode controller requires a window");
    const auto flags=api_.flags(window_);
    if(flags&(SDL_WINDOW_FULLSCREEN|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_MINIMIZED))
        throw std::invalid_argument("Initialize window modes before changing normal placement");
    remember_normal(flags);
    refresh();
}
void WindowModes::require_ui() const {
    if(api_.thread()!=ui_) throw std::logic_error("Window operation outside SDL UI thread");
}
void WindowModes::remember_normal(SDL_WindowFlags flags) {
    if(restore_pending_ || (flags&(SDL_WINDOW_FULLSCREEN|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_MINIMIZED))) return;
    Placement value;
    if(!api_.position(window_,&value.x,&value.y) || !api_.size(window_,&value.width,&value.height))
        throw std::runtime_error("Cannot read normal window placement");
    if(value.width<=0 || value.height<=0) return;
    value.scale=api_.scale(window_);
    if(!std::isfinite(value.scale) || value.scale<=0) throw std::runtime_error("Invalid window display scale");
    normal_=value;
}
void WindowModes::refresh() {
    require_ui();
    const auto flags=api_.flags(window_);
    fullscreen_=(flags&SDL_WINDOW_FULLSCREEN)!=0;
    settings_.fullscreen=fullscreen_;
    if(fullscreen_) {
        const auto *mode=api_.selected(window_);
        settings_.fullscreen_resolution=mode ? OutputResolution{mode->w,mode->h} : OutputResolution{};
    }
    remember_normal(flags);
    WindowSurface value;
    if(!api_.size(window_,&value.width,&value.height) || !api_.pixels(window_,&value.pixel_width,&value.pixel_height))
        throw std::runtime_error("Cannot read window surface dimensions");
    value.display_scale=api_.scale(window_);
    if(!std::isfinite(value.display_scale) || value.display_scale<=0)
        throw std::runtime_error("Invalid window display scale");
    value.minimized=(flags&SDL_WINDOW_MINIMIZED)!=0;
    // Minimized windows may keep a nonzero cached drawable size. Publish an
    // explicit non-presentable state without losing the normal placement.
    if(value.minimized || value.width<=0 || value.height<=0 || value.pixel_width<=0 || value.pixel_height<=0) {
        value.minimized=true; value.pixel_width=0; value.pixel_height=0;
    }
    surface_.publish(value);
}
void WindowModes::quarantine() {
    quarantined_keys_.insert(keys_.begin(),keys_.end());
    quarantined_buttons_.insert(buttons_.begin(),buttons_.end());
    discard_motion_=true;
    policy_applied_=false;
}
std::vector<DisplayChoice> WindowModes::choices(bool fullscreen) const {
    require_ui();
    std::vector<DisplayChoice> out{{{},fullscreen ? "Desktop" : "Current"}};
    const auto display=api_.display(window_);
    if(!display) return out;
    int count=0;
    auto **modes=api_.modes(display,&count);
    if(!modes) return out;
    for(int i=0;i<count;++i) {
        const auto *mode=modes[i];
        // Windows display modes use pixel density 1. Other density variants
        // must not masquerade as an output pixel resolution or window size.
        if(!mode || mode->displayID!=display || mode->pixel_density!=1 ||
           mode->w<640 || mode->h<360 || mode->w>16384 || mode->h>16384) continue;
        const OutputResolution size{mode->w,mode->h};
        if(std::none_of(out.begin(),out.end(),[&](const auto &v) { return v.resolution==size; }))
            out.push_back({size,std::to_string(size.width)+" x "+std::to_string(size.height)});
    }
    api_.free(modes);
    std::sort(out.begin()+1,out.end(),[](const auto &a,const auto &b) {
        return a.resolution.width==b.resolution.width ? a.resolution.height<b.resolution.height :
            a.resolution.width<b.resolution.width;
    });
    return out;
}
OutputResolution WindowModes::desktop_resolution() const {
    require_ui();
    const auto display=api_.display(window_);
    const auto *mode=display ? api_.desktop(display) : nullptr;
    if(!mode || mode->pixel_density!=1 || mode->w<=0 || mode->h<=0) return {};
    return {mode->w,mode->h};
}
bool WindowModes::restore_placement() {
    restore_pending_=true;
    SDL_Rect bounds{};
    // Preserve normal placement across DPI changes. Only relocate a completely
    // disconnected/off-screen rectangle after its monitor has gone away.
    const auto display=api_.display(window_);
    if(display && api_.bounds(display,&bounds) && bounds.w>0 && bounds.h>0 &&
       (normal_.x+normal_.width<=bounds.x || normal_.y+normal_.height<=bounds.y ||
        normal_.x>=bounds.x+bounds.w || normal_.y>=bounds.y+bounds.h)) {
        normal_.x=bounds.x+std::max(0,(bounds.w-normal_.width)/2);
        normal_.y=bounds.y+std::max(0,(bounds.h-normal_.height)/2);
    }
    if(!api_.restore(window_) || !api_.sync(window_) ||
       !api_.move(window_,normal_.x,normal_.y) || !api_.resize(window_,normal_.width,normal_.height) ||
       !api_.sync(window_) || (restore_maximized_ && (!api_.maximize(window_) || !api_.sync(window_))))
        return false;
    const auto flags=api_.flags(window_);
    int width=0,height=0;
    if(flags&(SDL_WINDOW_FULLSCREEN|SDL_WINDOW_MINIMIZED) ||
       (restore_maximized_ ? !(flags&SDL_WINDOW_MAXIMIZED) :
        (flags&SDL_WINDOW_MAXIMIZED) || !api_.size(window_,&width,&height) ||
         width!=normal_.width || height!=normal_.height)) return false;
    restore_pending_=false;
    return true;
}
bool WindowModes::enact(const DisplaySettings &request,const SDL_DisplayMode *mode) {
    const bool was_full=(api_.flags(window_)&SDL_WINDOW_FULLSCREEN)!=0;
    if(request.fullscreen) {
        if(!api_.desktop_mode(window_,mode) || !api_.fullscreen(window_,true) || !api_.sync(window_)) return false;
        const auto *observed=api_.selected(window_);
        const auto *current=api_.current(api_.display(window_));
        return (api_.flags(window_)&SDL_WINDOW_FULLSCREEN) &&
            (mode ? observed && observed->displayID==mode->displayID && observed->w==mode->w &&
                    observed->h==mode->h && observed->pixel_density==mode->pixel_density &&
                    ((api_.flags(window_)&SDL_WINDOW_HIDDEN) ||
                    (current && current->w==mode->w && current->h==mode->h &&
                    current->pixel_density==mode->pixel_density && current->format==mode->format &&
                    (!mode->refresh_rate || !current->refresh_rate ||
                     std::abs(current->refresh_rate-mode->refresh_rate)<1.f))) : observed==nullptr);
    }
    if(was_full && (!api_.fullscreen(window_,false) || !api_.sync(window_))) return false;
    if((was_full || restore_pending_) && !restore_placement()) return false;
    if(!api_.desktop_mode(window_,nullptr)) return false;
    if(request.window_size!=settings_.window_size && request.window_size.width) {
        if(!api_.restore(window_) || !api_.sync(window_) ||
           !api_.resize(window_,request.window_size.width,request.window_size.height) || !api_.sync(window_)) return false;
        int w=0,h=0;
        if(!api_.size(window_,&w,&h) || w!=request.window_size.width || h!=request.window_size.height) return false;
    }
    return !(api_.flags(window_)&SDL_WINDOW_FULLSCREEN);
}
WindowModeRoute WindowModes::apply(DisplaySettings request) {
    require_ui();
    WindowModeRoute result;
    if(request==settings_ && request.fullscreen==fullscreen_ && !restore_pending_) return result;
    const auto available=choices(request.fullscreen);
    auto resolution=request.fullscreen ? request.fullscreen_resolution : request.window_size;
    if(request.fullscreen!=fullscreen_ &&
       std::none_of(available.begin(),available.end(),[&](const auto &v) { return v.resolution==resolution; })) {
        // A disconnected monitor must never prevent leaving fullscreen.
        (request.fullscreen ? request.fullscreen_resolution : request.window_size)={};
        resolution={};
    }
    if(std::none_of(available.begin(),available.end(),[&](const auto &v) { return v.resolution==resolution; })) {
        result.error="Resolution is no longer available. Choose another resolution.";
        return result;
    }
    const auto previous=settings_;
    const auto flags=api_.flags(window_);
    remember_normal(flags);
    const auto placement=normal_;
    const auto maximized=restore_maximized_;
    if(!(flags&SDL_WINDOW_FULLSCREEN) && !restore_pending_) restore_maximized_=(flags&SDL_WINDOW_MAXIMIZED)!=0;
    SDL_DisplayMode old_mode{},new_mode{};
    const auto *old_ptr=api_.selected(window_);
    const bool old_exclusive=old_ptr!=nullptr;
    if(old_ptr) old_mode=*old_ptr;
    bool selected=false;
    if(request.fullscreen && resolution.width) {
        const auto display=api_.display(window_);
        const auto *desktop=api_.desktop(display);
        int count=0; auto **modes=api_.modes(display,&count);
        // Pick the supported refresh nearest the desktop, never invent a mode.
        for(int i=0;modes && i<count;++i) if(modes[i] && modes[i]->displayID==display &&
            modes[i]->w==resolution.width && modes[i]->h==resolution.height && modes[i]->pixel_density==1) {
            if(!selected || (desktop && std::abs(modes[i]->refresh_rate-desktop->refresh_rate)<
                                          std::abs(new_mode.refresh_rate-desktop->refresh_rate))) {
                new_mode=*modes[i]; selected=true;
            }
        }
        if(modes) api_.free(modes);
        if(!selected) { result.error="Resolution is no longer available. Choose another resolution."; return result; }
    }
    result.release_owned=result.invalidate_draw=true;
    quarantine();
    if(hooks_.prepare) hooks_.prepare(hooks_.user);
    if(enact(request,selected ? &new_mode : nullptr)) {
        fullscreen_=request.fullscreen; settings_=request;
        if(!fullscreen_) remember_normal(api_.flags(window_));
        result.changed=true;
    } else {
        result.error="Could not apply display settings. Previous settings restored.";
        // Restore the complete old state, including normal placement after a
        // partial window resize. Failed rollback is explicit and never saved.
        normal_=placement;
        const bool old_max=restore_maximized_;
        bool rollback=enact(previous,old_exclusive ? &old_mode : nullptr);
        if(!previous.fullscreen) rollback=restore_placement() && rollback;
        restore_maximized_=old_max;
        if(!rollback) result.error="Could not apply or restore display settings. Try windowed mode.";
        fullscreen_=(api_.flags(window_)&SDL_WINDOW_FULLSCREEN)!=0;
        settings_=previous; settings_.fullscreen=fullscreen_;
        if(fullscreen_) {
            const auto *actual=api_.selected(window_);
            settings_.fullscreen_resolution=actual ? OutputResolution{actual->w,actual->h} : OutputResolution{};
        }
        if(!rollback) restore_maximized_=maximized;
    }
    refresh();
    return result;
}
WindowModeRoute WindowModes::startup(DisplaySettings request) {
    require_ui();
    const auto full=choices(true),windowed=choices(false);
    auto supported=[](const auto &list,OutputResolution resolution) {
        return std::any_of(list.begin(),list.end(),[&](const auto &v) { return v.resolution==resolution; });
    };
    if(!supported(full,request.fullscreen_resolution)) request.fullscreen_resolution={};
    if(!supported(windowed,request.window_size)) request.window_size={};
    if(request.fullscreen && request.window_size.width) {
        // Establish the independent saved window size before fullscreen caches
        // normal placement. Startup never persists this intermediate request.
        auto windowed_request=request; windowed_request.fullscreen=false;
        const auto initial=apply(windowed_request);
        if(!initial.error.empty()) return initial;
    }
    return apply(request);
}
WindowModeRoute WindowModes::toggle() {
    auto request=settings_; request.fullscreen=!fullscreen_;
    auto result=apply(request); result.consume=true;
    return result;
}
WindowModeRoute WindowModes::reveal() {
    require_ui();
    SDL_DisplayMode selected{};
    const auto *mode=api_.selected(window_);
    const bool exclusive=mode!=nullptr;
    if(mode) selected=*mode;
    if(!api_.show(window_) || !api_.sync(window_) || (api_.flags(window_)&SDL_WINDOW_HIDDEN))
        throw std::runtime_error("Cannot reveal the prepared game window");
    // SDL queues fullscreen while hidden. It changes the monitor only on show;
    // checking the monitor earlier would incorrectly reject saved exclusive modes.
    WindowModeRoute result;
    if(!enact(settings_,exclusive ? &selected : nullptr)) {
        auto fallback=settings_;
        fallback.fullscreen_resolution={};
        if(!enact(fallback,nullptr))
            throw std::runtime_error("Cannot activate the prepared display mode");
        settings_=fallback;
        result.error="Saved fullscreen mode is unavailable. Using desktop fullscreen.";
    }
    refresh();
    return result;
}
WindowModeRoute WindowModes::route(const SDL_Event &event) {
    require_ui();
    WindowModeRoute result;
    const bool down=event.type==SDL_EVENT_KEY_DOWN,up=event.type==SDL_EVENT_KEY_UP;
    if(down || up) {
        if(event.key.windowID!=id_) return result;
        const auto key=event.key.key;
        const bool first=down && keys_.insert(key).second;
        const bool owned=quarantined_keys_.contains(key);
        if(first && !event.key.repeat && !owned && focused_ && key==SDLK_RETURN &&
           (event.key.mod&SDL_KMOD_ALT) && !(event.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI)))
            result=toggle();
        else result.consume=owned;
        if(up) { keys_.erase(key); quarantined_keys_.erase(key); }
        return result;
    }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP) {
        if(event.button.windowID!=id_) return result;
        const auto button=event.button.button;
        if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN) buttons_.insert(button);
        result.consume=quarantined_buttons_.contains(button);
        if(event.type==SDL_EVENT_MOUSE_BUTTON_UP) { buttons_.erase(button); quarantined_buttons_.erase(button); }
    } else if(event.type==SDL_EVENT_MOUSE_MOTION && event.motion.windowID==id_) {
        result.consume=discard_motion_;
    } else if(event.type>=SDL_EVENT_WINDOW_FIRST && event.type<=SDL_EVENT_WINDOW_LAST && event.window.windowID==id_) {
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST) {
            focused_=false; quarantine(); result.release_owned=result.invalidate_draw=true;
        } else if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) {
            focused_=true; discard_motion_=true; policy_applied_=false;
        } else if(event.type==SDL_EVENT_WINDOW_MINIMIZED) {
            quarantine(); result.release_owned=result.invalidate_draw=true;
        }
    }
    return result;
}
} // namespace nova::platform
