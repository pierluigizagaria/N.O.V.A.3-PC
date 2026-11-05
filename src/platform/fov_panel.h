#pragma once
// Host-only overlay. All methods require their documented owning lane.
#include "camera/native_world_fov.h"
#include "platform/fov_config.h"
#include "input/bindings.h"
#include "platform/window_modes.h"
#include "platform/render_catalogue.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <memory>
#include <mutex>

// Exclusive UI ownership persists independently of the guest gameplay flag.
#include <SDL3/SDL.h>
#include <set>

namespace nova::platform {
struct PanelRoute {
    bool consume=false, panel_event=false, visibility_changed=false;
    bool release_owned=false, invalidate_text=false, invalidate_draw=false;
};
class PanelInputOwner {
  public:
    bool visible() const { return visible_; }
    bool focused() const { return focused_; }
    void relative_applied(bool enabled) {
        if(relative_!=enabled) { discard_motion_=true; motion_drain_=true; }
        relative_=enabled;
        policy_acknowledged_=true;
    }
    // A full queue drain AFTER capture acknowledgement is the rearm boundary.
    void events_drained() {
        if(motion_drain_ && policy_acknowledged_ && focused_ && !visible_) {
            discard_motion_=false; motion_drain_=false;
        }
    }
    PanelRoute set_visible(bool value) {
        PanelRoute result;
        if (value==visible_) return result;
        visible_=value;
        quarantine();
        result.visibility_changed=result.release_owned=result.invalidate_text=
            result.invalidate_draw=true;
        discard_motion_=true; motion_drain_=true; policy_acknowledged_=false;
        return result;
    }
    // Called after source/window filtering, before Controls or Input dispatch.
    PanelRoute route(const SDL_Event &event,bool binding_capture=false) {
        PanelRoute result;
        if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST) {
            focused_=false; quarantine(); discard_motion_=true; motion_drain_=true; policy_acknowledged_=false;
            result.release_owned=result.invalidate_text=result.invalidate_draw=true;
            result.panel_event=visible_;
            return result;
        }
        if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) {
            focused_=true; activation_pair_=true; discard_motion_=true; motion_drain_=true; policy_acknowledged_=false;
            result.invalidate_text=true; result.panel_event=visible_;
            return result;
        }
        const bool key_down=event.type==SDL_EVENT_KEY_DOWN;
        const bool key_up=event.type==SDL_EVENT_KEY_UP;
        const bool mouse_down=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
        const bool mouse_up=event.type==SDL_EVENT_MOUSE_BUTTON_UP;
        if(key_down || key_up) {
            const auto key=event.key.key;
            const bool first=key_down && keys_.insert(key).second;
            const bool owned=quarantined_keys_.contains(key);
            if(key==SDLK_INSERT) {
                if(first && !event.key.repeat && focused_) result=set_visible(!visible_);
                result.consume=true;
            } else if(first && !event.key.repeat && !owned && key==SDLK_ESCAPE && visible_ && !binding_capture) {
                result=set_visible(false); quarantined_keys_.insert(key);
                result.consume=true;
            } else {
                result.consume=visible_ || !focused_ || owned;
                result.panel_event=visible_ && focused_;
                if(key_down && result.consume) quarantined_keys_.insert(key);
            }
            if(key_up) { keys_.erase(key); quarantined_keys_.erase(key); }
            return result;
        }
        if(mouse_down || mouse_up) {
            const auto button=event.button.button;
            if(mouse_down) buttons_.insert(button);
            const bool owned=quarantined_buttons_.contains(button);
            const bool activation=activation_pair_ && mouse_down;
            if(activation) activation_pair_=false;
            result.consume=visible_ || !focused_ || owned || activation;
            result.panel_event=visible_ && focused_ && !activation;
            if(mouse_down && result.consume) quarantined_buttons_.insert(button);
            if(mouse_up) { buttons_.erase(button); quarantined_buttons_.erase(button); }
            return result;
        }
        if(event.type==SDL_EVENT_MOUSE_MOTION) {
            result.consume=visible_ || !focused_ || discard_motion_;
            result.panel_event=visible_ && focused_;
        } else if(event.type==SDL_EVENT_MOUSE_WHEEL || event.type==SDL_EVENT_TEXT_INPUT ||
                  event.type==SDL_EVENT_TEXT_EDITING) {
            result.consume=visible_ || !focused_;
            result.panel_event=visible_ && focused_;
        }
        return result;
    }
    // Controls::clear intentionally has no access to this physical ownership.
  private:
    void quarantine() {
        quarantined_keys_.insert(keys_.begin(),keys_.end());
        quarantined_buttons_.insert(buttons_.begin(),buttons_.end());
    }
    bool visible_=false, focused_=true, activation_pair_=false, discard_motion_=false;
    bool relative_=false,motion_drain_=false,policy_acknowledged_=false;
    std::set<SDL_Keycode> keys_,quarantined_keys_;
    std::set<Uint8> buttons_,quarantined_buttons_;
};
} // namespace nova::platform

// Every backend effect can be replaced before initialization by a synthetic spy.
#include <SDL3/SDL.h>
#include <stdexcept>

namespace nova::platform {
// Install spies before InitForOther. No backend mutation can bypass this table.
struct PanelSdlApi {
    decltype(&SDL_CreateSystemCursor) create_cursor=&SDL_CreateSystemCursor;
    decltype(&SDL_DestroyCursor) destroy_cursor=&SDL_DestroyCursor;
    decltype(&SDL_CaptureMouse) capture=&SDL_CaptureMouse;
    decltype(&SDL_SetCursor) cursor=&SDL_SetCursor;
    decltype(&SDL_ShowCursor) show=&SDL_ShowCursor;
    decltype(&SDL_HideCursor) hide=&SDL_HideCursor;
    decltype(&SDL_SetWindowRelativeMouseMode) relative=&SDL_SetWindowRelativeMouseMode;
    decltype(&SDL_StartTextInput) start_text=&SDL_StartTextInput;
    decltype(&SDL_StopTextInput) stop_text=&SDL_StopTextInput;
    decltype(&SDL_SetTextInputArea) text_area=&SDL_SetTextInputArea;
    decltype(&SDL_GetGlobalMouseState) global_mouse=&SDL_GetGlobalMouseState;
    decltype(&SDL_GetKeyboardFocus) keyboard_focus=&SDL_GetKeyboardFocus;
    decltype(&SDL_GetWindowPosition) window_position=&SDL_GetWindowPosition;
    decltype(&SDL_GetWindowRelativeMouseMode) relative_state=&SDL_GetWindowRelativeMouseMode;
    decltype(&SDL_GetClipboardText) get_clipboard=&SDL_GetClipboardText;
    decltype(&SDL_SetClipboardText) set_clipboard=&SDL_SetClipboardText;
    decltype(&SDL_SetWindowMouseGrab) grab=&SDL_SetWindowMouseGrab;
    decltype(&SDL_GetWindowMouseGrab) grab_state=&SDL_GetWindowMouseGrab;
    decltype(&SDL_GetWindowFlags) flags=&SDL_GetWindowFlags;
};
struct PanelBackendEffects {
    PanelSdlApi api;
    bool synthetic=false;
    bool capture=false, cursor_visible=true, wants_text=false;
    SDL_Cursor *cursor=nullptr;
    SDL_Rect area{};
    int area_cursor=0;
    SDL_Window *ime_window=nullptr;
    // TLS is set only for the UI lane before backend init, and cleared after
    // backend shutdown. The GL lane cannot accidentally mutate platform state.
    static inline thread_local PanelBackendEffects *bound=nullptr;
    void bind() {
        if(bound && bound!=this) throw std::logic_error("SDL effects owner already bound");
        if(!api.create_cursor || !api.destroy_cursor || !api.capture || !api.cursor ||
           !api.show || !api.hide || !api.relative || !api.start_text || !api.stop_text ||
           !api.text_area || !api.global_mouse || !api.keyboard_focus ||
           !api.window_position || !api.relative_state || !api.get_clipboard || !api.set_clipboard ||
           !api.grab || !api.grab_state || !api.flags)
            throw std::logic_error("Incomplete SDL effects adapter");
        if(synthetic && (api.create_cursor==&SDL_CreateSystemCursor ||
           api.destroy_cursor==&SDL_DestroyCursor || api.capture==&SDL_CaptureMouse ||
           api.cursor==&SDL_SetCursor || api.show==&SDL_ShowCursor || api.hide==&SDL_HideCursor ||
           api.relative==&SDL_SetWindowRelativeMouseMode || api.start_text==&SDL_StartTextInput ||
           api.stop_text==&SDL_StopTextInput || api.text_area==&SDL_SetTextInputArea ||
           api.global_mouse==&SDL_GetGlobalMouseState || api.keyboard_focus==&SDL_GetKeyboardFocus ||
           api.window_position==&SDL_GetWindowPosition || api.relative_state==&SDL_GetWindowRelativeMouseMode ||
           api.get_clipboard==&SDL_GetClipboardText || api.set_clipboard==&SDL_SetClipboardText ||
           api.grab==&SDL_SetWindowMouseGrab || api.grab_state==&SDL_GetWindowMouseGrab ||
           api.flags==&SDL_GetWindowFlags))
            throw std::logic_error("Synthetic SDL adapter must intercept every reachable call");
        bound=this;
    }
    void unbind() { if(bound!=this) throw std::logic_error("Wrong SDL effects owner"); bound=nullptr; }
    static PanelBackendEffects &current() {
        if(!bound) throw std::logic_error("SDL backend operation outside UI effects scope");
        return *bound;
    }
    // On ownership transition only: IME callbacks are change-driven, so text
    // intent must persist through ordinary frames without a new callback.
    void clear_requests() { capture=false; wants_text=false; cursor=nullptr; cursor_visible=true; }
};
// Guest text start/stop is provided by Input through an intercepted callback.
// force=true bypasses applied_revision==keyboard_revision after panel/focus use.
struct GuestTextSync {
    void *user=nullptr;
    void (*suspend)(void *)=nullptr;
    void (*sync)(void *,bool force)=nullptr;
};
class PanelEffectsArbiter {
  public:
    explicit PanelEffectsArbiter(PanelBackendEffects &backend):backend_(backend) {}
    void release_pointer(SDL_Window *window) {
        require(backend_.api.grab(window,false));
        require(backend_.api.relative(window,false));
        require(backend_.api.capture(false));
        require(backend_.api.show());
        require(!backend_.api.grab_state(window) && !backend_.api.relative_state(window));
    }
    // Called OUTSIDE the ImGui mutex after its final Render/IME callback.
    bool apply(SDL_Window *window,bool overlay,bool focused,bool gameplay,
               bool guest_editor,const GuestTextSync &guest) {
        const auto flags=window ? backend_.api.flags(window) : SDL_WindowFlags{};
        focused=focused && window && backend_.api.keyboard_focus()==window &&
            (flags&SDL_WINDOW_INPUT_FOCUS) && !(flags&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED));
        const bool takeover=overlay || !focused;
        const bool changed=takeover!=takeover_;
        if(takeover && changed && guest.suspend) guest.suspend(guest.user);
        const bool relative=focused && gameplay && !guest_editor && !overlay;
        // Confine only while controlling gameplay. Native menus, text entry and
        // Settings release the cursor; ImGui drag capture remains independent.
        require(backend_.api.grab(window,relative));
        require(backend_.api.relative(window,relative));
        require(backend_.api.grab_state(window)==relative);
        require(backend_.api.relative_state(window)==relative);
        require(backend_.api.capture(focused && overlay && backend_.capture));
        if(relative) require(backend_.api.hide());
        else {
            if(overlay && backend_.cursor) require(backend_.api.cursor(backend_.cursor));
            require(backend_.api.show());
        }
        if(overlay && focused && backend_.wants_text) {
            require(backend_.api.text_area(window,&backend_.area,backend_.area_cursor));
            if(!overlay_text_) require(backend_.api.start_text(window));
            overlay_text_=true;
        } else if(overlay_text_) {
            require(backend_.api.stop_text(window)); overlay_text_=false;
            restore_guest_=true;
        }
        if(!takeover && guest.sync) {
            guest.sync(guest.user,changed || restore_guest_);
            restore_guest_=false;
        }
        takeover_=takeover;
        return relative; // Acknowledge the observed result, not desired policy.
    }
  private:
    static void require(bool result) { if(!result) throw std::runtime_error("Cannot apply host input ownership"); }
    PanelBackendEffects &backend_;
    bool takeover_=false, overlay_text_=false, restore_guest_=false;
};
} // namespace nova::platform


struct ImGuiContext;
namespace nova::platform {
struct FovUiFrame {
    float width=1280, height=720, framebuffer_scale_x=1, framebuffer_scale_y=1;
    double guest_aspect=16.0/9.0; // Optical reference follows the guest target, not letterbox bars.
    float delta_seconds=1.f/60.f;
    float dpi_scale=1;
    bool focused=true, minimized=false, running_gameplay=false;
    camera::NativeWorldFov::LiveStatus optical;
    FovSaveResult last_save;
    std::string weapon_position_error;
    std::string preview_error;
    DisplaySettings display;
    GuestExtent render_extent{},active_render_extent{};
    std::vector<RenderChoice> render_choices;
    bool render_available=false,render_busy=false,render_unsaved=false;
    bool render_confirming=false;
    int render_seconds_remaining=0;
    std::string render_error;
    std::vector<DisplayChoice> display_choices;
    std::string display_error;
    std::string controls_error;
    std::string movement_wobble_error;
    std::string ui_scale_error;
    std::string graphics_quality_error;
};
struct FovCommands {
    bool changed=false, save=false, closed=false;
    std::optional<DisplaySettings> display;
    std::optional<GuestExtent> render_extent;
    bool apply_render=false;
    bool keep_render=false,revert_render=false;
    std::optional<input::ControlsConfig> bindings;
    std::optional<input::MouseSensitivity> sensitivity;
    std::optional<movement_wobble::Strength> movement_wobble;
    std::optional<int> ui_scale;
    bool save_ui_scale=false;
    std::optional<GraphicsQuality> graphics_quality;
    std::optional<weapon_placement::Position> weapon_position;
    bool retry_bindings_save=false;
    camera::NativeWorldFov::Options visible;
    uint64_t visible_revision=0;
    bool wants_text=false;
    bool preview_pending=true,preview_applied=false,saved_current=false;
};
// Exact UI-owner handoff used by play and the installed native composition test.
// Call after ui_frame has released the ImGui mutex; never from a guest scope.
void dispatch_fov_commands(camera::NativeWorldFov &,const FovCommands &,
                          const std::filesystem::path &,FovSaveResult &,
                          std::string &preview_error,const FovConfigIo *injected=nullptr);
enum class FovControl { world_slider,weapon_slider,reset,movement_slider,count };
enum class SensitivityControl { normal,ads,reset,count };
enum class DisplayControl { gameplay_tab,display_tab,graphics_tab,fullscreen,resolution,first_resolution,full_hd_resolution,apply,keep,revert,ui_scale,ui_scale_reset,lod_distance,lod_original,texture_filtering,texture_original,lod_reset,texture_reset,graphics_retry,count };
struct FovUiRect { float x1=0,y1=0,x2=0,y2=0; };
class FovPanel {
  public:
    explicit FovPanel(camera::NativeWorldFov::Options initial,input::ControlsConfig bindings={},
                      movement_wobble::Strength movement={},int ui_scale=ui::default_percent);
    // Before ui_initialize only; separate from O08's constructor-owned setting.
    void initialize_weapon_position(weapon_placement::Position);
    void initialize_graphics_quality(GraphicsQuality);
    ~FovPanel(); // Both owning-lane shutdown methods must already have run.
    void ui_initialize(SDL_Window *window,PanelBackendEffects *effects=nullptr);
    void gl_initialize(bool fail_after_prewarm=false);
    void ui_shutdown();
    void gl_shutdown();
    void ui_event(const SDL_Event &event); // Only exclusive panel-owned events.
    bool ui_binding_capture(); // UI owner queries before PanelInputOwner routing.
    void ui_visible(bool visible);
    void ui_window_transition(); // Finalize a valid edit and retire held backend input.
    bool visible() const { return visible_.load(); }
    bool renderer_ready() const { return renderer_ready_.load(); }
    FovCommands ui_frame(const FovUiFrame &frame);
    void gl_draw(int framebuffer_width,int framebuffer_height);
    void gl_presentable(bool enabled);
    void gl_recreate_resources(); // Owning-lane lifecycle seam; same state guard.
    void invalidate_draw(); // UI: minimize/focus loss/close/no-swap policy.
    FovUiRect control_rect(FovControl control); // Copied geometry for synthetic input.
    FovUiRect display_control_rect(DisplayControl control);
    FovUiRect binding_control_rect(input::Action action);
    FovUiRect controls_tab_rect();
    FovUiRect controls_reset_rect();
    FovUiRect controls_cancel_rect();
    FovUiRect sensitivity_control_rect(SensitivityControl);
    std::array<float,2> displayed_values(); // Copies the actual SliderFloat storage.
  private:
    void select() const; // Mutex held by every caller.
    void release_input_locked(); // UI lane and mutex held.
    void release_backend_input_locked(); // Keeps capture physical tracking intact.
    void require_ui() const;
    void require_gl() const;
    std::mutex mutex_;
    ImGuiContext *context_=nullptr;
    SDL_Window *window_=nullptr;
    uint32_t ui_lane_=0,gl_lane_=0;
    bool platform_initialized_=false,renderer_initialized_=false;
    PanelBackendEffects default_effects_;
    PanelBackendEffects *effects_=nullptr;
    std::atomic<bool> visible_{false},renderer_ready_{false};
    bool drawable_=false;
    bool presentable_=true;
    uint64_t ui_serial_=0,published_serial_=0,drawn_serial_=0;
    camera::NativeWorldFov::Options options_;
    uint64_t visible_revision_=0;
    float world_seed_=0,weapon_seed_=0;
    int movement_wobble_seed_=100;
    int ui_scale_seed_=ui::default_percent;
    bool ui_scale_pending_=false;
    GraphicsQuality graphics_startup_{},graphics_selected_{};
    bool display_tab_active_=false;
    bool graphics_tab_active_=false;
    bool movement_edit_pending_=false,movement_save_pending_=false;
    bool world_invalid_=false,weapon_invalid_=false;
    bool movement_gameplay_tab_active_=true;
    bool edit_pending_=false,save_pending_=false;
    input::ControlsConfig bindings_;
    input::MouseSensitivityEdit sensitivity_edit_;
    std::array<FovUiRect,static_cast<size_t>(SensitivityControl::count)> sensitivity_rects_{};
    input::BindingCapture binding_capture_;
    weapon_placement::RequestedPosition weapon_position_requested_;
    weapon_placement::Position weapon_position_draft_;
    bool position_gameplay_tab_active_=true;
    bool controls_tab_active_=false;
    FovUiRect controls_tab_rect_{},binding_cancel_rect_{},controls_reset_rect_{},title_rect_{};
    std::array<FovUiRect,input::action_count> binding_rects_{};
    std::array<FovUiRect,static_cast<size_t>(FovControl::count)> controls_{};
    std::array<FovUiRect,static_cast<size_t>(DisplayControl::count)> display_controls_{};
};
} // namespace nova::platform
