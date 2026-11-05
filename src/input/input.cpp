#include "input/input.h"
#include "input/script.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <windows.h>

namespace nova::input {
namespace {
bool start_text(void *,SDL_Window *window,bool password) {
    const auto properties=SDL_CreateProperties();
    if(!properties) return false;
    const bool configured=SDL_SetNumberProperty(properties,SDL_PROP_TEXTINPUT_TYPE_NUMBER,
        password ? SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN : SDL_TEXTINPUT_TYPE_TEXT);
    const bool started=configured && SDL_StartTextInputWithProperties(window,properties);
    SDL_DestroyProperties(properties);
    return started;
}
bool stop_text(void *,SDL_Window *window) { return SDL_StopTextInput(window); }
constexpr auto lib = "com/gameloft/android/ANMP/GloftN3HM/GL2JNILib";
constexpr auto prefix = "Java_com_gameloft_android_ANMP_GloftN3HM_GL2JNILib_";
std::u16string utf16(const char *text) {
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!size)
        throw std::invalid_argument("Invalid UTF-8 input text");
    std::wstring wide(static_cast<size_t>(size), 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide.data(), size);
    return {reinterpret_cast<const char16_t *>(wide.data()), static_cast<size_t>(size - 1)};
}
std::string utf8(const std::u16string &text) {
    if (text.empty())
        return {};
    auto data = reinterpret_cast<const wchar_t *>(text.data());
    auto length = static_cast<int>(text.size());
    int size = WideCharToMultiByte(CP_UTF8, 0, data, length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), 0);
    WideCharToMultiByte(CP_UTF8, 0, data, length, result.data(), size, nullptr, nullptr);
    return result;
}
bool map_coordinate(float value, int surface, double window, int &result) {
    double scaled = static_cast<double>(value) * surface / window;
    if (!std::isfinite(scaled) || scaled < std::numeric_limits<int>::min() ||
        scaled > std::numeric_limits<int>::max())
        return false;
    result = static_cast<int>(scaled); // Java casts toward zero; origin is top left.
    return true;
}
} // namespace
Input::Input(jni::Runtime &runtime, SDL_Window *window, int width, int height,
             const TextInputApi *text_api)
    : runtime_(runtime), klass_(runtime.find_class(lib)), window_(window),
      window_id_(SDL_GetWindowID(window)), width_(width), height_(height) {
    text_api_=text_api ? *text_api : TextInputApi{nullptr,start_text,stop_text};
    if(!text_api_.start || !text_api_.stop)
        throw std::invalid_argument("Text input requires complete platform effects");
    if (!window_id_ || width <= 0 || height <= 0)
        throw std::invalid_argument("Input requires a window and positive surface dimensions");
    runtime_.register_method(
        lib, "SetKeyboard", "(ILjava/lang/String;I)V", [this](auto &rt, auto, const auto &a) {
            auto object = rt.object(a[1].l);
            std::lock_guard lock(keyboard_mutex_);
            visible_ = a[0].i != 0;
            if (visible_) {
                text_ = object ? object->text : std::u16string{};
                password_ = a[0].i == 2;
                max_length_ = std::max(0, a[2].i);
            }
            ++keyboard_revision_;
            std::printf("input keyboard visible=%d password=%d max_length=%d\n", visible_,
                        password_, max_length_);
            return jni::Value{};
        });
    runtime_.register_method(lib, "IsKeyboardVisible", "()I", [this](auto &, auto, const auto &) {
        std::lock_guard lock(keyboard_mutex_);
        jni::Value value;
        value.i = visible_ ? 1 : 0;
        return value;
    });
    runtime_.register_method(lib, "GetKeyboardText", "()[B", [this](auto &rt, auto, const auto &) {
        std::u16string text;
        {
            std::lock_guard lock(keyboard_mutex_);
            // String.trim() in this Android version removes characters <= U+0020.
            size_t begin = 0, end = text_.size();
            while (begin < end && text_[begin] <= 32)
                ++begin;
            while (end > begin && text_[end - 1] <= 32)
                --end;
            text = text_.substr(begin, end - begin);
        }
        auto bytes = utf8(text); // Java getBytes(): standard UTF-8, not JNI modified UTF-8.
        jni::Value value;
        value.l = rt.bytes(bytes.data(), bytes.size());
        return value;
    });
}
Input::~Input() { runtime_.release(klass_, 1); }
void Input::surface_extent(int width,int height) {
    if(width<=0 || height<=0) throw std::invalid_argument("Invalid input surface extent");
    if(width_==width && height_==height) return;
    release_all(); // Finish any old-coordinate contact before publishing its inverse.
    width_=width; height_=height; x_=y_=0;
}
void Input::native(const char *name, std::vector<uint32_t> args) {
    auto env = runtime_.attach();
    env->frames.emplace_back();
    auto cleanup = [&] {
        auto refs = std::move(env->frames.back());
        env->frames.pop_back();
        for (auto ref : refs)
            runtime_.release(ref, 1);
    };
    try {
        runtime_.native(std::string(prefix) + name, klass_, args);
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
}
void Input::touch(int action, int x, int y) {
    native("touchEvent",
           {static_cast<uint32_t>(action), static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0});
    if(action==1) ++touch_down_;
    if(action==0) ++touch_up_;
}
int Input::android_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE:
        return 4; // BACK
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return 66;
    case SDLK_BACKSPACE:
        return 67;
    default:
        return 0; // Movement/controller keys belong to T07.
    }
}
void Input::suspend_keyboard(bool suspended) {
    if(keyboard_suspended_==suspended) return;
    if(suspended && sdl_text_) {
        if(!text_api_.stop(text_api_.user,window_))
            throw std::runtime_error("Cannot suspend guest text input");
        sdl_text_=false;
    }
    keyboard_suspended_=suspended;
    keyboard_dirty_=true;
}
void Input::sync_keyboard(bool force) {
    if(keyboard_suspended_) return;
    bool visible, password;
    uint64_t revision;
    {
        std::lock_guard lock(keyboard_mutex_);
        visible = visible_;
        password = password_;
        revision = keyboard_revision_;
    }
    if (!force && !keyboard_dirty_ && applied_revision_ == revision)
        return;
    if (sdl_text_ && !text_api_.stop(text_api_.user,window_))
        throw std::runtime_error(SDL_GetError());
    sdl_text_ = false;
    if (visible) {
        bool ok=text_api_.start(text_api_.user,window_,password);
        if (!ok)
            throw std::runtime_error(SDL_GetError());
        sdl_text_ = true;
    }
    applied_revision_ = revision;
    keyboard_dirty_=false;
}
void Input::finish_text(bool hide) {
    // Do not hold a lock while entering the guest: it calls GetKeyboardText synchronously.
    native("OnKeyboardFinish");
    if (hide) {
        std::lock_guard lock(keyboard_mutex_);
        visible_ = false;
        ++keyboard_revision_;
    }
}
void Input::release_all() {
    if (pressed_) {
        touch(0, x_, y_);
        pressed_ = false;
    }
    for (int key : keys_)
        native("OnKeyUp", {static_cast<uint32_t>(key)});
    keys_.clear();
}
void Input::window_viewport(SDL_FRect viewport) {
    if(!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
       !std::isfinite(viewport.w) || !std::isfinite(viewport.h) || viewport.w<0 || viewport.h<0)
        throw std::invalid_argument("Invalid absolute input viewport");
    if(!custom_viewport_ || viewport_.x!=viewport.x || viewport_.y!=viewport.y ||
       viewport_.w!=viewport.w || viewport_.h!=viewport.h) release_all();
    viewport_=viewport; custom_viewport_=true;
}
bool Input::diagnostic_center(int width,int height,float &x,float &y) const {
    int ww=0,wh=0;
    if(pressed_ || !keys_.empty() || width!=width_ || height!=height_ || width<=0 || height<=0 ||
       !SDL_GetWindowSize(window_,&ww,&wh) || ww<=0 || wh<=0) return false;
    const auto r=custom_viewport_?viewport_:SDL_FRect{0,0,float(ww),float(wh)};
    if(r.w<=0 || r.h<=0 || r.x<0 || r.y<0 || r.x+r.w>ww || r.y+r.h>wh) return false;
    x=r.x+r.w/2;y=r.y+r.h/2;
    int nx=0,ny=0;
    return map_coordinate(x-r.x,width_,r.w,nx) && map_coordinate(y-r.y,height_,r.h,ny) &&
        nx>0 && nx<width && ny>0 && ny<height;
}
void Input::handle(const SDL_Event &incoming, uint64_t frames) {
    SDL_Event event = incoming;
    Script::unwrap(event);
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == window_id_) {
        release_all(); // Release owned presses only; no pause/resume or focus lifecycle policy.
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_MOUSE_MOTION) {
        bool motion = event.type == SDL_EVENT_MOUSE_MOTION;
        if ((motion ? event.motion.windowID : event.button.windowID) != window_id_ || frames < 10)
            return;
        if (!motion && event.button.button != SDL_BUTTON_LEFT)
            return;
        int ww = 0, wh = 0, x = 0, y = 0;
        if (!SDL_GetWindowSize(window_, &ww, &wh) || ww <= 0 || wh <= 0)
            return;
        const SDL_FRect rect=custom_viewport_ ? viewport_ : SDL_FRect{0,0,float(ww),float(wh)};
        const float absolute_x=motion ? event.motion.x : event.button.x;
        const float absolute_y=motion ? event.motion.y : event.button.y;
        if(rect.w<=0 || rect.h<=0 ||
            !map_coordinate(absolute_x-rect.x,width_,rect.w,x) ||
            !map_coordinate(absolute_y-rect.y,height_,rect.h,y))
            return;
        x_ = x;
        y_ = y;
        if (absolute_x<rect.x || absolute_y<rect.y || absolute_x>rect.x+rect.w || absolute_y>rect.y+rect.h) {
            if (pressed_)
                touch(0, x, y); // Original Activity releases pointers outside its surface.
            pressed_ = false;
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !pressed_) {
            pressed_ = true;
            touch(1, x, y);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && pressed_) {
            pressed_ = false;
            touch(0, x, y);
        } else if (motion && pressed_) {
            touch(2, x, y);
        }
        return;
    }
    bool visible;
    {
        std::lock_guard lock(keyboard_mutex_);
        visible = visible_;
    }
    if (event.type == SDL_EVENT_TEXT_INPUT && event.text.windowID == window_id_ && visible) {
        auto addition = utf16(event.text.text);
        {
            std::lock_guard lock(keyboard_mutex_);
            // Provisional desktop editor: honor the requested UTF-16 length, never split a pair.
            size_t room = text_.size() < static_cast<size_t>(max_length_)
                              ? static_cast<size_t>(max_length_) - text_.size()
                              : 0;
            if (addition.size() > room)
                addition.resize(room);
            if (!addition.empty() && addition.back() >= 0xd800 && addition.back() <= 0xdbff)
                addition.pop_back();
            if (addition.empty())
                return;
            text_ += addition;
        }
        finish_text(false); // Normal Java EditText's TextWatcher reports every committed edit.
        return;
    }
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
        return;
    if (event.key.windowID != window_id_)
        return;
    bool down = event.type == SDL_EVENT_KEY_DOWN;
    if (frames < (down ? 4u : 10u))
        return;
    int key = android_key(event.key.key);
    if (!key)
        return;
    if (down && visible) {
        if (key == 4 || key == 66) {
            if (!event.key.repeat)
                finish_text(true);
        } else if (key == 67) {
            bool changed = false;
            {
                std::lock_guard lock(keyboard_mutex_);
                if (!text_.empty()) {
                    char16_t tail = text_.back();
                    text_.pop_back();
                    if (tail >= 0xdc00 && tail <= 0xdfff && !text_.empty() &&
                        text_.back() >= 0xd800 && text_.back() <= 0xdbff)
                        text_.pop_back();
                    changed = true;
                }
            }
            if (changed)
                finish_text(false);
        }
        return;
    }
    if (down) {
        if (keys_.insert(key).second)
            native("OnKeyDown", {static_cast<uint32_t>(key)});
    } else {
        // Activity forwards key-up even if the keyboard consumed key-down.
        keys_.erase(key);
        native("OnKeyUp", {static_cast<uint32_t>(key)});
    }
}
} // namespace nova::input
