#pragma once
#include "jni/runtime.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>

namespace nova::input {
struct TextInputApi {
    void *user=nullptr;
    bool (*start)(void *,SDL_Window *,bool password)=nullptr;
    bool (*stop)(void *,SDL_Window *)=nullptr;
};
// UI-thread event translator. Java callbacks may arrive from guest threads.
class Input {
  public:
    Input(jni::Runtime &runtime, SDL_Window *window, int surface_width, int surface_height,
          const TextInputApi *text_api=nullptr);
    ~Input();
    void handle(const SDL_Event &event, uint64_t completed_frames);
    void sync_keyboard(bool force=false);
    void suspend_keyboard(bool suspended);
    void invalidate_keyboard() { keyboard_dirty_=true; }
    void release_all();
    // UI-only inverse presentation transform for absolute menu touches. Relative
    // gameplay mouse counts and their sensitivity never pass through this path.
    void window_viewport(SDL_FRect viewport);
    void surface_extent(int width,int height); // UI: after actual render acknowledgement.
    bool keyboard_visible() { std::lock_guard lock(keyboard_mutex_); return visible_; }
    static int android_key(SDL_Keycode key);
    struct DiagnosticTouchReceipt { uint64_t down=0,up=0; bool held=false; };
    DiagnosticTouchReceipt diagnostic_touch_receipt() const { return {touch_down_,touch_up_,pressed_}; }
    bool diagnostic_center(int width,int height,float &x,float &y) const;


  private:
    void native(const char *name, std::vector<uint32_t> args = {});
    void touch(int action, int x, int y);
    void finish_text(bool hide);
    jni::Runtime &runtime_;
    jni::Handle klass_;
    SDL_Window *window_;
    SDL_WindowID window_id_;
    int width_, height_, x_ = 0, y_ = 0;
    bool pressed_ = false;
    uint64_t touch_down_=0,touch_up_=0;
    bool custom_viewport_=false;
    SDL_FRect viewport_{};
    std::set<int> keys_;
    std::mutex keyboard_mutex_;
    std::u16string text_;
    bool visible_ = false, sdl_text_ = false, password_ = false;
    int max_length_ = 250;
    uint64_t keyboard_revision_ = 0, applied_revision_ = 0;
    TextInputApi text_api_;
    bool keyboard_suspended_=false, keyboard_dirty_=true;
};
} // namespace nova::input
