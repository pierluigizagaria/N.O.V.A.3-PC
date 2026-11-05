#include "input/controls.h"
#include "input/bindings.h"
#include <charconv>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace nova::input {
namespace {
bool panel_movement(Action a) {
    return a==Action::forward || a==Action::back || a==Action::left ||
        a==Action::right || a==Action::sprint || a==Action::jump;
}
constexpr const char *names[] = {"forward", "left", "back", "right", "sprint", "jump",
    "reload", "grenade", "interact", "melee", "power", "fire", "aim", "next", "previous",
    "slot1", "slot2", "slot3", "slot4", "slot5", "chat", "scores"};
Action action(const std::string &name) {
    for (size_t i = 0; i < action_count; ++i)
        if (name == names[i]) return static_cast<Action>(i);
    throw std::invalid_argument("Unknown control action: " + name);
}
}
const char *action_name(Action a) { return names[static_cast<size_t>(a)]; }
SDL_Keycode key_code(const std::string &name) {
    if (name == "BACK" || name == "ESCAPE") return SDLK_ESCAPE;
    if (name == "ENTER") return SDLK_RETURN;
    if (name == "BACKSPACE") return SDLK_BACKSPACE;
    if (name == "SHIFT") return SDLK_LSHIFT;
    if (name == "RSHIFT") return SDLK_RSHIFT;
    if (name == "SPACE") return SDLK_SPACE;
    if (name == "TAB") return SDLK_TAB;
    if (name.size() == 1) {
        auto c = name[0];
        if (c >= 'A' && c <= 'Z') return static_cast<SDL_Keycode>(SDLK_A + c - 'A');
        if (c >= '0' && c <= '9') return static_cast<SDL_Keycode>(SDLK_0 + c - '0');
    }
    if (name.starts_with("KEY_")) {
        uint32_t code=0;
        const auto first=name.data()+4,last=name.data()+name.size();
        const auto parsed=std::from_chars(first,last,code);
        if(parsed.ec==std::errc{} && parsed.ptr==last && bindable_key(code)) return code;
    }
    return 0;
}
ControlsConfig::ControlsConfig() {
    keys = {{SDLK_W,Action::forward},{SDLK_A,Action::left},{SDLK_S,Action::back},
        {SDLK_D,Action::right},{SDLK_LSHIFT,Action::sprint},{SDLK_RSHIFT,Action::sprint},
        {SDLK_SPACE,Action::jump},{SDLK_R,Action::reload},{SDLK_G,Action::grenade},
        {SDLK_E,Action::interact},{SDLK_F,Action::melee},{SDLK_Q,Action::power},
        {SDLK_1,Action::slot1},{SDLK_2,Action::slot2},{SDLK_3,Action::slot3},
        {SDLK_4,Action::slot4},{SDLK_5,Action::slot5},
        {SDLK_V,Action::chat},{SDLK_TAB,Action::scores}};
    buttons = {{SDL_BUTTON_LEFT,Action::fire},{SDL_BUTTON_RIGHT,Action::aim}};
}
ControlsConfig ControlsConfig::parse(std::istream &stream) {
    ControlsConfig result;
    std::string line;
    size_t n = 0;
    bool replacement=false,binding_seen=false;
    while (std::getline(stream,line)) {
        ++n;
        line.erase(line.find('#') == std::string::npos ? line.size() : line.find('#'));
        std::replace(line.begin(),line.end(),'=',' ');
        std::istringstream words(line);
        std::string key, value, extra;
        if (!(words >> key)) continue;
        auto fail = [&] { throw std::invalid_argument("controls.conf line " + std::to_string(n)); };
        if (!(words >> value) || words >> extra) fail();
        if (key == "sensitivity" || key == "aim_multiplier" || key == "ads_sensitivity") {
            size_t end = 0;
            float parsed=0;
            try { parsed=std::stof(value,&end); } catch (...) { fail(); }
            // O12 accepted multipliers up to10. Clamp that legacy range only
            // while loading; new runtime edits and saves are limited to0..2.
            const bool valid=key=="aim_multiplier" ?
                std::isfinite(parsed) && parsed>=0 && parsed<=10 : valid_sensitivity(parsed);
            if(end!=value.size() || !valid) fail();
            if(key=="sensitivity") result.sensitivity=parsed;
            else if(key=="aim_multiplier") result.aim_multiplier=std::min(parsed,aim_multiplier_max);
            // O12: validate but retire the old absolute ADS setting. There is no
            // weapon-independent conversion to the original dynamic factor.
        } else if (key == "invert_y" || key == "touch_autoaim") {
            if (value != "true" && value != "false") fail();
            (key == "invert_y" ? result.invert_y : result.touch_autoaim) = value == "true";
        } else if (key == "bindings") {
            if(value != "replace" || replacement || binding_seen) fail();
            replacement=true;
            result.keys.clear(); result.buttons.clear();
            result.wheel_up=result.wheel_down=Action::count;
        } else if (key.starts_with("bind.")) {
            binding_seen=true;
            auto name = key.substr(5);
            auto a = action(value);
            if (name == "MOUSE1") result.buttons[SDL_BUTTON_LEFT] = a;
            else if (name == "MOUSE2") result.buttons[SDL_BUTTON_RIGHT] = a;
            else if (name == "MOUSE3") result.buttons[SDL_BUTTON_MIDDLE] = a;
            else if (name == "MOUSE4") result.buttons[SDL_BUTTON_X1] = a;
            else if (name == "MOUSE5") result.buttons[SDL_BUTTON_X2] = a;
            else if (name == "WHEELUP") result.wheel_up = a;
            else if (name == "WHEELDOWN") result.wheel_down = a;
            else {
                auto code = key_code(name);
                if (!bindable_key(code)) fail();
                result.keys[code] = a;
            }
        } else fail();
    }
    if (stream.bad()) throw std::runtime_error("Cannot read controls.conf");
    return result;
}
ControlsConfig ControlsConfig::read(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) return {};
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open controls.conf");
    return parse(stream);
}
Look look_delta(float dx, float dy, float pitch, float limit, const MouseLookConfig &config) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(pitch) ||
        !std::isfinite(limit) || limit <= 0) return {};
    float yaw = -dx * config.sensitivity;
    float desired = pitch + dy * config.sensitivity * (config.invert_y ? 1.0f : -1.0f);
    return {yaw, std::clamp(desired,-limit,limit) - pitch};
}
bool input_event(Uint32 type) {
    return type == SDL_EVENT_MOUSE_MOTION || type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        type == SDL_EVENT_MOUSE_BUTTON_UP || type == SDL_EVENT_MOUSE_WHEEL ||
        type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP || type == SDL_EVENT_TEXT_INPUT ||
        type == SDL_EVENT_GAMEPAD_AXIS_MOTION || type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || type == SDL_EVENT_GAMEPAD_BUTTON_UP;
}
bool permit_input(bool scripted, bool allow, bool source, Uint32 type) {
    return !scripted || allow || source || !input_event(type);
}
bool capture_policy(bool gameplay, bool focused, bool text) { return gameplay && focused && !text; }
void Controls::clear() {
    std::lock_guard lock(mutex_); pending_ = {}; keys_down_.clear(); buttons_down_.clear();
    ++edge_epoch_;
    // Native temporary permission also uses clear(). Discard stale one-shots,
    // but do not reinterpret it as release of a physically held controller.
    gamepad_pressed_.clear();
}
void Controls::clear_except_movement() {
    std::lock_guard lock(mutex_);
    const bool retain=active();
    std::erase_if(keys_down_,[&](auto key) { return !retain || !panel_movement(config.keys.at(key)); });
    std::erase_if(buttons_down_,[&](auto button) { return !retain || !panel_movement(config.buttons.at(button)); });
    pending_={};gamepad_pressed_.clear();++edge_epoch_;
    for(auto key:keys_down_) pending_.held.set(static_cast<size_t>(config.keys.at(key)));
    for(auto button:buttons_down_) pending_.held.set(static_cast<size_t>(config.buttons.at(button)));
}
void Controls::clear_gamepad() {
    std::lock_guard lock(mutex_); gamepad_={}; gamepad_pressed_.clear(); gamepad_rearm_=true; ++gamepad_revision_;
}
void Controls::submit_gamepad(const GamepadSample &sample) {
    std::lock_guard lock(mutex_);
    if(gamepad_.device!=sample.device || gamepad_.generation!=sample.generation) {
        gamepad_={}; gamepad_pressed_.clear(); gamepad_rearm_=true; ++gamepad_revision_;
    }
    const bool source_allowed=focused_.load() && !text_.load() && !overlay_.load();
    if(!sample.device || !source_allowed) {
        gamepad_={}; gamepad_pressed_.clear(); gamepad_rearm_=true; ++gamepad_revision_; return;
    }
    if(gamepad_rearm_) {
        gamepad_.device=sample.device; gamepad_.generation=sample.generation;
        if(sample.neutral) gamepad_rearm_=false;
        return;
    }
    if(active() && gamepad_receiver_supported_) for(size_t i=0;i<action_count;++i)
        if(sample.held[i] && !gamepad_.held[i] && !pending_.held[i])
            gamepad_pressed_.push_back(static_cast<Action>(i));
    gamepad_=sample;
}
ControlsConfig Controls::snapshot_config() const {
    std::lock_guard lock(mutex_);
    auto result=config;
    result.sensitivity=sensitivity_.normal; result.aim_multiplier=sensitivity_.aim_multiplier;
    return result;
}
MouseLookConfig Controls::look_config(float native_factor,bool native_aim) const {
    std::lock_guard lock(mutex_);
    // The game owns this factor, including entry and continuous sniper zoom.
    // Never write it back: doing so would compound the user's multiplier.
    if(!std::isfinite(native_factor) || native_factor<0) native_factor=1.f;
    const float effective=sensitivity_.normal*native_factor*
        (native_aim ? sensitivity_.aim_multiplier : 1.f);
    return {std::isfinite(effective) ? effective : sensitivity_.normal,config.invert_y};
}
void Controls::replace_sensitivity(MouseSensitivity value) {
    if(!valid_sensitivity(value)) throw std::invalid_argument("Invalid mouse sensitivity");
    std::lock_guard lock(mutex_); sensitivity_=value;
    // Changing sensitivity never releases held controls or replaces mappings.
}
void Controls::replace_bindings(const ControlsConfig &value) {
    // Validate/copy before locking; scalar settings are never written while
    // NativeControls reads them on GL. Pending action state cannot cross a map.
    (void)serialize_bindings(value);
    auto keys=value.keys;
    auto buttons=value.buttons;
    std::lock_guard lock(mutex_);
    ++edge_epoch_;
    rearm_keys_.insert(keys_down_.begin(),keys_down_.end());
    rearm_buttons_.insert(buttons_down_.begin(),buttons_down_.end());
    for(auto [key,action]:panel_keys_) rearm_keys_.insert(key);
    for(auto [button,action]:panel_buttons_) rearm_buttons_.insert(button);
    panel_keys_.clear(); panel_buttons_.clear();
    config.keys.swap(keys); config.buttons.swap(buttons);
    config.wheel_up=value.wheel_up; config.wheel_down=value.wheel_down;
    pending_={}; keys_down_.clear(); buttons_down_.clear();
    gamepad_={}; gamepad_pressed_.clear(); gamepad_rearm_=true; ++gamepad_revision_;
}
bool Controls::bound_down(Action a) const {
    for (auto key : keys_down_) if (config.keys.at(key) == a) return true;
    for (auto button : buttons_down_) if (config.buttons.at(button) == a) return true;
    return false;
}
void Controls::context(bool focused, bool text) {
    focused_ = focused; text_ = text;
    if (!active()) clear();
    if (!focused || text) { cancel_panel_holds(); clear_gamepad(); }
}
void Controls::cancel_panel_holds() {
    std::lock_guard lock(mutex_); panel_keys_.clear(); panel_buttons_.clear();
}
bool Controls::panel_held(Action action) const {
    std::lock_guard lock(mutex_);
    const auto owns=[&](const auto &entry) { return entry.second==action; };
    return focused_ && !text_ && !overlay_ &&
        (std::any_of(panel_keys_.begin(),panel_keys_.end(),owns) ||
         std::any_of(panel_buttons_.begin(),panel_buttons_.end(),owns));
}
void Controls::button(Action a, bool down) {
    auto index = static_cast<size_t>(a);
    if (down && !pending_.held.test(index) && (!gamepad_receiver_supported_ || !gamepad_.held.test(index))) pending_.pressed.push_back(a);
    pending_.held.set(index,down);
}
bool Controls::handle(const SDL_Event &e) {
    std::lock_guard lock(mutex_);
    // Releases must reach the physical hold even after the native panel
    // has made gameplay inactive. Other modal keys remain absolute UI input.
    if(e.type==SDL_EVENT_KEY_UP) {
        const bool owned=panel_keys_.erase(e.key.key)!=0;
        if(owned && (!active() || communication_panel_)) return true;
    }
    if(e.type==SDL_EVENT_MOUSE_BUTTON_UP) {
        const bool owned=panel_buttons_.erase(e.button.button)!=0;
        if(owned && (!active() || communication_panel_)) return true;
    }
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        if(rearm_keys_.contains(e.key.key)) {
            if(e.type == SDL_EVENT_KEY_UP) rearm_keys_.erase(e.key.key);
            return true;
        }
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if(rearm_buttons_.contains(e.button.button)) {
            if(e.type == SDL_EVENT_MOUSE_BUTTON_UP) rearm_buttons_.erase(e.button.button);
            return true;
        }
    }
    if (!active()) return false;
    if(communication_panel_) {
        // Absolute pointer events belong to the visible panel. Only remapped
        // locomotion keys/buttons continue through the native gameplay path.
        if(e.type==SDL_EVENT_MOUSE_MOTION || e.type==SDL_EVENT_MOUSE_WHEEL) return false;
        if(e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) {
            const auto it=config.keys.find(e.key.key);
            if(it!=config.keys.end() && !panel_movement(it->second)) return true;
        }
        if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP) {
            const auto it=config.buttons.find(e.button.button);
            if(it==config.buttons.end() || !panel_movement(it->second)) return false;
        }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        if (std::isfinite(e.motion.xrel) && std::isfinite(e.motion.yrel)) {
            pending_.dx += e.motion.xrel; pending_.dy += e.motion.yrel;
        }
        return true;
    }
    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        auto it = config.keys.find(e.key.key);
        if (it == config.keys.end()) return false;
        if (!e.key.repeat) {
            if(e.type==SDL_EVENT_KEY_DOWN && (it->second==Action::scores || it->second==Action::chat)) panel_keys_[e.key.key]=it->second;
            if (e.type == SDL_EVENT_KEY_DOWN) keys_down_.insert(e.key.key);
            else keys_down_.erase(e.key.key);
            button(it->second,bound_down(it->second));
        }
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        auto it = config.buttons.find(e.button.button);
        if (it != config.buttons.end()) {
            if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && (it->second==Action::scores || it->second==Action::chat)) panel_buttons_[e.button.button]=it->second;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) buttons_down_.insert(e.button.button);
            else buttons_down_.erase(e.button.button);
            button(it->second,bound_down(it->second));
        }
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        float y = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
        if (std::isfinite(y) && y != 0) {
            const auto action=y > 0 ? config.wheel_up : config.wheel_down;
            if(action!=Action::count) pending_.pressed.push_back(action);
        }
        return true;
    }
    return false;
}
ContinuousGamepadInput Controls::continuous_gamepad() const {
    std::lock_guard lock(mutex_);
    const bool admitted=active() && gamepad_receiver_supported_ && !gamepad_rearm_;
    return {admitted ? gamepad_ : GamepadSample{},pending_.held,gamepad_revision_,gamepad_.device!=0,admitted};
}
bool Controls::gamepad_current(uint64_t revision) const {
    std::lock_guard lock(mutex_);
    return revision==gamepad_revision_ && active() && gamepad_receiver_supported_ && !gamepad_rearm_;
}
uint64_t Controls::gamepad_revision() const {
    std::lock_guard lock(mutex_); return gamepad_revision_;
}
bool Controls::revoke_stale_gamepad(FrameInput &frame) const {
    std::lock_guard lock(mutex_);
    if(frame.gamepad_revision==gamepad_revision_) return false;
    frame.held&=~frame.gamepad_only_held;
    std::erase_if(frame.pressed,[&](Action a) { return frame.gamepad_edge(a); });
    frame.gamepad={}; frame.gamepad_only_held.reset(); frame.gamepad_only_pressed.clear();
    frame.gamepad_revision=gamepad_revision_; return true;
}
void Controls::bind_gamepad_receiver(bool supported,bool terminal) {
    std::lock_guard lock(mutex_);
    gamepad_receiver_supported_=supported;
    if(terminal) {
        gamepad_={}; gamepad_pressed_.clear(); gamepad_rearm_=true; ++gamepad_revision_;
    }
}
FrameInput Controls::take() {
    std::lock_guard lock(mutex_);
    auto result = pending_;
    result.edge_epoch=edge_epoch_.load();
    result.gamepad_revision=gamepad_revision_;
    if(active() && gamepad_receiver_supported_ && !gamepad_rearm_) {
        result.gamepad_only_held=gamepad_.held & ~result.held;
        result.held|=gamepad_.held;
        result.gamepad=gamepad_.axes;
        for(auto action : gamepad_pressed_) if(!result.edge(action)) {
            result.pressed.push_back(action); result.gamepad_only_pressed.push_back(action);
        }
    }
    // UI can enqueue a command after the frame-start command drain. Retain it
    // for the next frame instead of letting a gameplay receiver discard it.
    const auto command=[](Action a) { return a==Action::chat || a==Action::scores; };
    std::erase_if(result.pressed,command);
    std::erase_if(result.gamepad_only_pressed,command);
    std::erase_if(gamepad_pressed_,[&](Action a) { return !command(a); });
    std::erase_if(pending_.pressed,[&](Action a) { return !command(a); });
    if(communication_panel_) {
        for(size_t i=0;i<action_count;++i) if(!panel_movement(static_cast<Action>(i))) {
            result.held.reset(i);result.gamepad_only_held.reset(i);
        }
        std::erase_if(result.pressed,[](Action a) { return !panel_movement(a); });
        std::erase_if(result.gamepad_only_pressed,[](Action a) { return !panel_movement(a); });
        result.dx=result.dy=result.gamepad.look_x=result.gamepad.look_y=0;
    }
    pending_.dx = pending_.dy = 0;
    return result;
}
}

namespace nova::input {
bool Controls::take_pressed(Action action) {
    std::lock_guard lock(mutex_);
    const bool keyboard=std::erase(pending_.pressed,action)!=0;
    const bool pad=std::erase(gamepad_pressed_,action)!=0;
    return active() && (keyboard || (pad && gamepad_receiver_supported_ && !gamepad_rearm_));
}
const char *action_label(Action a) {
    static constexpr const char *labels[]={"Forward","Left","Backward","Right",
        "Sprint","Jump","Reload","Grenade","Interact","Melee","Power",
        "Fire","Aim","Next weapon","Previous weapon",
        "Weapon 1","Weapon 2","Weapon 3","Weapon 4","Weapon 5","Voice lines","Scoreboard"};
    return static_cast<size_t>(a)<action_count ? labels[static_cast<size_t>(a)] : "Unbound";
}
const char *action_contexts(Action a) {
    switch(a) {
    case Action::left: case Action::right: return "On foot, mech, jeep driving and bike";
    case Action::forward: case Action::back: return "On foot, mech and jeep driving";
    case Action::fire: return "On foot; primary fire in mech, jeep, mounted gun and bike";
    case Action::aim: return "Held aim on foot; secondary fire in mech, jeep and mounted gun";
    case Action::interact: return "Interact on foot; exit mech, jeep or mounted gun";
    case Action::slot1: return "Weapon 1 on foot; request driver in a jeep only when the native role is available";
    case Action::slot2: return "Weapon 2 on foot; request shooter in a jeep only when the native role is available";
    case Action::chat: return "Hold to open multiplayer voice lines; click a line or release to close";
    case Action::scores: return "Hold to show the multiplayer scoreboard; release to return to the game";
    default: return "On foot";
    }
}
bool held_binding_required(Action a) {
    return a==Action::forward || a==Action::left || a==Action::back || a==Action::right ||
        a==Action::sprint || a==Action::fire || a==Action::aim || a==Action::scores || a==Action::chat;
}
bool bindable_key(SDL_Keycode key) {
    if(key==SDLK_ESCAPE || key==SDLK_INSERT || key==SDLK_RETURN || key==SDLK_KP_ENTER ||
       key==SDLK_BACKSPACE || key==SDLK_LALT || key==SDLK_RALT ||
       key==SDLK_LGUI || key==SDLK_RGUI) return false;
    // Stable SDL keycodes, not localized display names. SDL exposes printable
    // Unicode keys and scancode-tagged nonprinting keys in separate domains.
    const auto scancode=key&~SDLK_SCANCODE_MASK;
    return (key>=32 && key<=0x10ffffu) || key==SDLK_TAB ||
        ((key&SDLK_SCANCODE_MASK) && scancode>0 && scancode<static_cast<uint32_t>(SDL_SCANCODE_COUNT));
}
bool valid_binding(Binding b) {
    switch(b.kind) {
    case BindingKind::key: return bindable_key(b.code);
    case BindingKind::mouse: return b.code>=SDL_BUTTON_LEFT && b.code<=SDL_BUTTON_X2;
    case BindingKind::wheel_up: case BindingKind::wheel_down: return b.code==0;
    }
    return false;
}
std::optional<Action> binding_owner(const ControlsConfig &c,Binding b) {
    if(b.kind==BindingKind::key) {
        if(auto it=c.keys.find(b.code);it!=c.keys.end()) return it->second;
    } else if(b.kind==BindingKind::mouse) {
        if(auto it=c.buttons.find(static_cast<Uint8>(b.code));it!=c.buttons.end()) return it->second;
    } else {
        const auto a=b.kind==BindingKind::wheel_up ? c.wheel_up : c.wheel_down;
        if(a!=Action::count) return a;
    }
    return {};
}
std::string binding_label(Binding b) {
    switch(b.kind) {
    case BindingKind::key: {
        const auto name=SDL_GetKeyName(b.code);
        return name && *name ? std::string(name) : "Key "+std::to_string(b.code);
    }
    case BindingKind::mouse: {
        static constexpr const char *labels[]={"","Mouse 1","Mouse 3","Mouse 2","Mouse 4","Mouse 5"};
        return b.code<=SDL_BUTTON_X2 ? labels[b.code] : "Unknown mouse button";
    }
    case BindingKind::wheel_up: return "Wheel up";
    case BindingKind::wheel_down: return "Wheel down";
    }
    return "Unknown";
}
std::string action_bindings(const ControlsConfig &c,Action a) {
    std::string out;
    auto append=[&](Binding b) { if(!out.empty()) out+=" / "; out+=binding_label(b); };
    for(auto [key,action]:c.keys) if(action==a) append({BindingKind::key,key});
    for(auto [button,action]:c.buttons) if(action==a) append({BindingKind::mouse,button});
    if(c.wheel_up==a) append({BindingKind::wheel_up});
    if(c.wheel_down==a) append({BindingKind::wheel_down});
    return out.empty() ? "Unbound" : out;
}
std::string rebind(ControlsConfig &c,Action a,Binding b) {
    if(static_cast<size_t>(a)>=action_count) return "Unknown action.";
    if(!valid_binding(b)) return "This input is reserved or unsupported. Choose another.";
    if(held_binding_required(a) && (b.kind==BindingKind::wheel_up || b.kind==BindingKind::wheel_down))
        return "This action needs a held key or mouse button.";
    if(const auto other=binding_owner(c,b);other && *other!=a)
        return "Already used by "+std::string(action_label(*other))+". Choose another input.";
    // Validate first. Conflict/cancel paths never mutate even one old alias.
    std::erase_if(c.keys,[&](const auto &entry) { return entry.second==a; });
    std::erase_if(c.buttons,[&](const auto &entry) { return entry.second==a; });
    if(c.wheel_up==a) c.wheel_up=Action::count;
    if(c.wheel_down==a) c.wheel_down=Action::count;
    switch(b.kind) {
    case BindingKind::key: c.keys[b.code]=a; break;
    case BindingKind::mouse: c.buttons[static_cast<Uint8>(b.code)]=a; break;
    case BindingKind::wheel_up: c.wheel_up=a; break;
    case BindingKind::wheel_down: c.wheel_down=a; break;
    }
    return {};
}
void reset_bindings(ControlsConfig &c) {
    ControlsConfig defaults;
    c.keys=std::move(defaults.keys); c.buttons=std::move(defaults.buttons);
    c.wheel_up=defaults.wheel_up; c.wheel_down=defaults.wheel_down;
}
bool same_bindings(const ControlsConfig &a,const ControlsConfig &b) {
    return a.keys==b.keys && a.buttons==b.buttons &&
        a.wheel_up==b.wheel_up && a.wheel_down==b.wheel_down;
}
std::string serialize_bindings(const ControlsConfig &c) {
    std::string out="bindings = replace\n";
    auto append=[&](const std::string &name,Action a) {
        if(static_cast<size_t>(a)>=action_count) throw std::invalid_argument("Invalid binding action");
        out+="bind."+name+" = "+action_name(a)+"\n";
    };
    for(auto [key,a]:c.keys) {
        if(!bindable_key(key)) throw std::invalid_argument("Reserved control key");
        append("KEY_"+std::to_string(key),a);
    }
    for(auto [button,a]:c.buttons) {
        if(!valid_binding({BindingKind::mouse,button})) throw std::invalid_argument("Invalid mouse binding");
        // User-facing MOUSE2 is right, MOUSE3 is middle; SDL enum order differs.
        const auto number=button==SDL_BUTTON_RIGHT ? 2u : button==SDL_BUTTON_MIDDLE ? 3u : button;
        append("MOUSE"+std::to_string(number),a);
    }
    if(c.wheel_up!=Action::count) append("WHEELUP",c.wheel_up);
    if(c.wheel_down!=Action::count) append("WHEELDOWN",c.wheel_down);
    return out;
}
void BindingCapture::begin(Action a) {
    if(static_cast<size_t>(a)>=action_count) throw std::invalid_argument("Invalid capture action");
    action_=a; error_.clear();
    armed_=keys_.empty() && buttons_.empty();
}
void BindingCapture::cancel() { action_.reset(); armed_=false; error_.clear(); }
void BindingCapture::cancel_mouse(Uint8 button) {
    buttons_.insert(button); swallow_buttons_.insert(button); cancel();
}
void BindingCapture::reset() {
    cancel(); keys_.clear(); buttons_.clear(); swallow_keys_.clear(); swallow_buttons_.clear();
    // A completed edit remains deliverable even if Insert/close arrives before
    // the next frame. Only take_completed acknowledges that copied command.
}
bool BindingCapture::take_completed() { const bool value=completed_; completed_=false; return value; }
bool BindingCapture::event(const SDL_Event &e,ControlsConfig &config) {
    if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST || e.type==SDL_EVENT_WINDOW_HIDDEN ||
       e.type==SDL_EVENT_WINDOW_MINIMIZED) { reset(); return false; }
    const bool kd=e.type==SDL_EVENT_KEY_DOWN,ku=e.type==SDL_EVENT_KEY_UP;
    const bool md=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN,mu=e.type==SDL_EVENT_MOUSE_BUTTON_UP;
    bool first=false,swallow=false;
    if(kd || ku) {
        first=kd && keys_.insert(e.key.key).second;
        swallow=swallow_keys_.contains(e.key.key);
        if(ku) { keys_.erase(e.key.key); swallow_keys_.erase(e.key.key); }
    }
    if(md || mu) {
        first=md && buttons_.insert(e.button.button).second;
        swallow=swallow_buttons_.contains(e.button.button);
        if(mu) { buttons_.erase(e.button.button); swallow_buttons_.erase(e.button.button); }
    }
    if(!active()) return swallow;
    if(kd) swallow_keys_.insert(e.key.key);
    if(md) swallow_buttons_.insert(e.button.button);
    if(kd && !e.key.repeat && first && e.key.key==SDLK_ESCAPE) { cancel(); return true; }
    if(!armed_) {
        armed_=keys_.empty() && buttons_.empty();
        return input_event(e.type) || e.type==SDL_EVENT_TEXT_EDITING;
    }
    std::optional<Binding> candidate;
    if(kd && first && !e.key.repeat) {
        if(e.key.mod&(SDL_KMOD_ALT|SDL_KMOD_GUI)) {
            error_="Alt and Windows shortcuts are reserved.";
            return true;
        }
        candidate=Binding{BindingKind::key,e.key.key};
    } else if(md && first) candidate=Binding{BindingKind::mouse,e.button.button};
    else if(e.type==SDL_EVENT_MOUSE_WHEEL) {
        const auto y=e.wheel.y*(e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f);
        if(std::isfinite(y) && y!=0) candidate=Binding{y>0 ? BindingKind::wheel_up : BindingKind::wheel_down};
    }
    if(candidate) {
        error_=rebind(config,*action_,*candidate);
        if(error_.empty()) { cancel(); completed_=true; }
    }
    // Motion remains available to point at Cancel/X/tabs. PanelInputOwner still
    // consumes every event before either Controls or the guest menu translator.
    return (input_event(e.type) && e.type!=SDL_EVENT_MOUSE_MOTION) || e.type==SDL_EVENT_TEXT_EDITING;
}
} // namespace nova::input
