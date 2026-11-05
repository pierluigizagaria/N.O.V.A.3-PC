#pragma once
#include <SDL3/SDL.h>
#include "platform/display_settings.h"
#include <vector>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>

namespace nova::platform {
struct WindowSurface {
    int width=0,height=0,pixel_width=0,pixel_height=0;
    float display_scale=1;
    bool minimized=false;
    uint64_t revision=0;
};
struct WindowPresentation {
    int guest_width=1280,guest_height=720;
    SDL_Rect pixels{}; // Top-left origin, native window pixels.
    SDL_FRect logical{}; // Same content rectangle in SDL window coordinates.
};
WindowPresentation window_presentation(const WindowSurface &,int guest_width=1280,int guest_height=720);
// UI publishes complete snapshots; GL consumes without calling SDL video APIs.
class WindowSurfaceMailbox {
  public:
    void publish(WindowSurface value);
    WindowSurface snapshot() const;
  private:
    mutable std::mutex mutex_;
    WindowSurface value_;
};
// Every SDL video operation is replaceable. Tests provide a wholly fake window.
struct WindowModeApi {
    decltype(&SDL_GetCurrentThreadID) thread=&SDL_GetCurrentThreadID;
    decltype(&SDL_GetWindowID) id=&SDL_GetWindowID;
    decltype(&SDL_GetWindowFlags) flags=&SDL_GetWindowFlags;
    decltype(&SDL_GetWindowPosition) position=&SDL_GetWindowPosition;
    decltype(&SDL_GetWindowSize) size=&SDL_GetWindowSize;
    decltype(&SDL_GetWindowSizeInPixels) pixels=&SDL_GetWindowSizeInPixels;
    decltype(&SDL_GetWindowDisplayScale) scale=&SDL_GetWindowDisplayScale;
    decltype(&SDL_SetWindowFullscreenMode) desktop_mode=&SDL_SetWindowFullscreenMode;
    decltype(&SDL_SetWindowFullscreen) fullscreen=&SDL_SetWindowFullscreen;
    decltype(&SDL_RestoreWindow) restore=&SDL_RestoreWindow;
    decltype(&SDL_MaximizeWindow) maximize=&SDL_MaximizeWindow;
    decltype(&SDL_SetWindowPosition) move=&SDL_SetWindowPosition;
    decltype(&SDL_SetWindowSize) resize=&SDL_SetWindowSize;
    decltype(&SDL_SyncWindow) sync=&SDL_SyncWindow;
    decltype(&SDL_GetDisplayForWindow) display=&SDL_GetDisplayForWindow;
    decltype(&SDL_GetFullscreenDisplayModes) modes=&SDL_GetFullscreenDisplayModes;
    decltype(&SDL_GetDesktopDisplayMode) desktop=&SDL_GetDesktopDisplayMode;
    decltype(&SDL_GetCurrentDisplayMode) current=&SDL_GetCurrentDisplayMode;
    decltype(&SDL_GetDisplayUsableBounds) bounds=&SDL_GetDisplayUsableBounds;
    decltype(&SDL_GetWindowFullscreenMode) selected=&SDL_GetWindowFullscreenMode;
    decltype(&SDL_free) free=&SDL_free;
};
struct DisplayChoice {
    OutputResolution resolution;
    std::string label;
};
struct WindowModeRoute {
    bool consume=false,release_owned=false,invalidate_draw=false,changed=false;
    std::string error;
};
struct WindowModeHooks {
    void *user=nullptr;
    // UI owner releases gameplay/menu input and OS capture before SDL mutates
    // the window. Its existing arbiter reapplies the current policy afterwards.
    void (*prepare)(void *)=nullptr;
};
class WindowModes {
  public:
    // Construct immediately after normal window creation, on the SDL UI thread.
    WindowModes(SDL_Window *,WindowSurfaceMailbox &,WindowModeApi api={},WindowModeHooks hooks={});
    WindowModeRoute route(const SDL_Event &);
    // Poll once after draining events, so coalesced DPI/move/size events publish
    // the current SDL result rather than a stale event's requested dimensions.
    void refresh();
    void input_applied() { require_ui(); policy_applied_=true; }
    void events_drained() {
        require_ui();
        if(policy_applied_ && focused_) { discard_motion_=false; policy_applied_=false; }
    }
    bool fullscreen() const { return fullscreen_; }
    DisplaySettings settings() const { require_ui(); return settings_; }
    std::vector<DisplayChoice> choices(bool fullscreen) const;
    OutputResolution desktop_resolution() const;
    WindowModeRoute apply(DisplaySettings);
    // Startup invalid/removed-display choices fall back without rewriting config.
    WindowModeRoute startup(DisplaySettings);

  private:
    struct Placement { int x=0,y=0,width=0,height=0; float scale=1; } normal_;
    void require_ui() const;
    void remember_normal(SDL_WindowFlags);
    void quarantine();
    WindowModeRoute toggle();
    bool enact(const DisplaySettings &,const SDL_DisplayMode *);
    bool restore_placement();
    DisplaySettings settings_;

    SDL_Window *window_;
    SDL_WindowID id_;
    SDL_ThreadID ui_;
    WindowSurfaceMailbox &surface_;
    WindowModeApi api_;
    WindowModeHooks hooks_;
    bool fullscreen_=false,restore_maximized_=false,focused_=true,discard_motion_=false;
    bool policy_applied_=false,restore_pending_=false;
    std::set<SDL_Keycode> keys_,quarantined_keys_;
    std::set<Uint8> buttons_,quarantined_buttons_;
};
} // namespace nova::platform
