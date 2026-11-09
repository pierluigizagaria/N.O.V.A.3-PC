#include "input/bindings.h"
#include "input/native_intents.h"
#include "input/native_ads.h"
#include "input/native_adapter.h"
#include <limits>
#include "platform/fov_panel.h"
#include <cstdio>
#include <sstream>
#include <stdexcept>
using namespace nova::input;
namespace {
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
SDL_Event key(Uint32 type,SDL_Keycode code,bool repeat=false) {
    SDL_Event e{}; e.type=type; e.key.key=code; e.key.repeat=repeat; return e;
}
SDL_Event mouse(Uint32 type,Uint8 button) {
    SDL_Event e{}; e.type=type; e.button.button=button; return e;
}
SDL_Event wheel(float y) { SDL_Event e{}; e.type=SDL_EVENT_MOUSE_WHEEL; e.wheel.y=y; return e; }
ControlsConfig parse(const std::string &text) { std::istringstream in(text); return ControlsConfig::parse(in); }
void model_and_persistence() {
    auto config=parse("sensitivity=.25\ninvert_y=true\ntouch_autoaim=true\nbind.Z=jump\n");
    const auto initial=config;
    check(!rebind(config,Action::jump,{BindingKind::key,SDLK_W}).empty() &&
          same_bindings(config,initial),"on-foot conflict leaves every alias unchanged");
    check(!rebind(config,Action::aim,{BindingKind::wheel_up}).empty() &&
          same_bindings(config,initial),"C04 held aim cannot become a wheel pulse");
    for(const auto keycode:{SDLK_INSERT,SDLK_ESCAPE,SDLK_RETURN,SDLK_KP_ENTER,SDLK_LALT,SDLK_LGUI})
        check(!rebind(config,Action::jump,{BindingKind::key,keycode}).empty(),"access/menu/OS keys reserved");
    check(rebind(config,Action::jump,{BindingKind::mouse,SDL_BUTTON_X1}).empty(),"side mouse button accepted");
    check(!config.keys.contains(SDLK_SPACE) && !config.keys.contains(SDLK_Z),"Change removes all old aliases");
    check(rebind(config,Action::next,{BindingKind::key,SDLK_N}).empty() &&
          config.wheel_up==Action::count,"keyboard next removes old wheel binding");
    check(rebind(config,Action::reload,{BindingKind::wheel_up}).empty(),"released wheel input can serve a one-shot");
    const std::string original="# custom\r\nsensitivity=.25\r\ninvert_y=true\n"
        "touch_autoaim=true\r\nbind.Z=jump # keep this note\r\nbind.MOUSE3=power";
    const auto saved=nova::platform::merge_controls_config(original,config);
    const auto restored=parse(saved);
    check(same_bindings(restored,config),"full key/button/wheel round trip with no default resurrection");
    check(saved.find("# custom\r\nsensitivity=.25\r\ninvert_y=true\n")==0 &&
          saved.find("# keep this note\r\n")!=std::string::npos,"unrelated bytes/comments retained");
    check(restored.sensitivity==.25f && restored.invert_y && restored.touch_autoaim,
          "controls save keeps scalar options");
    check(nova::platform::merge_controls_config(saved,config)==saved,"repeat explicit save is idempotent");
    reset_bindings(config);
    check(same_bindings(config,ControlsConfig{}) && config.sensitivity==.25f &&
          config.invert_y && config.touch_autoaim,"Reset bindings preserves look and aim-assist preferences");
    config.keys.clear(); config.buttons.clear(); config.wheel_up=config.wheel_down=Action::count;
    check(same_bindings(parse(serialize_bindings(config)),config),"unbound physical sources remain unbound on restart");
    for(const auto bad:{"bindings=unknown","bindings=replace\nbindings=replace","bind.Z=jump\nbindings=replace",
                         "bindings=replace\nbind.KEY_0=jump","bindings=replace\nbind.MOUSE6=fire"})
        { bool rejected=false; try { (void)parse(bad); } catch(const std::invalid_argument &) { rejected=true; }
          check(rejected,"invalid replacement grammar fails without partial installation"); }
}
void mouse_sensitivity() {
    const auto close=[](float a,float b) { return std::abs(a-b)<.00001f; };
    check(close(mouse_degrees_per_count(1.f),.022f) &&
          close(mouse_degrees_per_count(2.f),.044f),"linear FPS units have a fixed angular gain");
    const MouseLookConfig fps{mouse_degrees_per_count(2.f),false};
    const auto whole=look_delta(100,100,0,89,fps);
    const auto part=look_delta(25,25,0,89,fps);
    check(close(whole.yaw,-4.4f) && close(whole.pitch,-4.4f) &&
          close(whole.yaw,4*part.yaw),"FPS scale preserves equal axes and packet-independent rotation");
    for(const float saved:{.001f,.0173f,.10f,.25f,10.f})
        check(close(mouse_degrees_per_count(mouse_fps_units(saved)),saved),
              "legacy angular values survive display-unit conversion, including out-of-slider values");
    auto legacy=parse("sensitivity=.25\n");
    check(legacy.sensitivity==.25f && legacy.aim_multiplier==1.f,"legacy base sensitivity restores native aim scaling");
    auto explicit_ads=parse("ads_sensitivity=.07\nsensitivity=.4\n");
    check(explicit_ads.aim_multiplier==1.f && explicit_ads.sensitivity==.4f,"retired absolute ADS is not mistaken for a dimensionless multiplier");
    for(const auto text:{"ads_sensitivity=nan","ads_sensitivity=inf","ads_sensitivity=0",
                         "ads_sensitivity=-1","ads_sensitivity=10.01","ads_sensitivity=.1junk"}) {
        bool rejected=false; try { (void)parse(text); } catch(const std::invalid_argument &) { rejected=true; }
        check(rejected,"invalid ADS cannot partially install");
    }
    for(const auto text:{"aim_multiplier=nan","aim_multiplier=inf",
                         "aim_multiplier=-1","aim_multiplier=10.01","aim_multiplier=.1junk"}) {
        bool rejected=false; try { (void)parse(text); } catch(const std::invalid_argument &) { rejected=true; }
        check(rejected,"invalid dimensionless multiplier is rejected");
    }
    for(const auto text:{"aim_multiplier=1.5\nads_sensitivity=.07\nsensitivity=.4\n",
                         "ads_sensitivity=.07\nsensitivity=.4\naim_multiplier=1.5\n"})
        check(parse(text).aim_multiplier==1.5f,"explicit multiplier takes precedence in any legacy key order");
    check(parse("aim_multiplier=0").aim_multiplier==0,"zero multiplier loads exactly");
    check(parse("aim_multiplier=2").aim_multiplier==2,"upper endpoint loads exactly");
    check(parse("aim_multiplier=9").aim_multiplier==2,"old O12 gain is capped in memory");
    for(float invalid_gain:{-0.01f,2.01f,10.f})
        check(!valid_sensitivity(MouseSensitivity{.1f,invalid_gain}),"runtime edits cannot exceed the new range");
    for(float gain:{0.f,1.f,2.f}) {
        const auto saved_range=nova::platform::merge_mouse_sensitivity("sensitivity=.3\nbind.Z=jump\n",{.3f,gain});
        const auto loaded=parse(saved_range);
        check(loaded.aim_multiplier==gain && loaded.sensitivity==.3f && loaded.keys.at(SDLK_Z)==Action::jump,
              "zero/default/maximum persist without changing base or bindings");
    }
    const auto defaults=parse("");
    check(defaults.sensitivity==.1f && defaults.aim_multiplier==1.f,"fresh defaults preserve native aim scaling");
    const std::string original="sensitivity=.25 # mouse\r\nbind.Z=jump\r\ninvert_y=true\r\ntouch_autoaim=true\r\n";
    const auto saved=nova::platform::merge_mouse_sensitivity(original,{.4f,.25f});
    const auto restored=parse(saved);
    check(restored.sensitivity==.4f && restored.aim_multiplier==.25f && restored.invert_y &&
          restored.touch_autoaim && restored.keys.at(SDLK_Z)==Action::jump,"save carries base plus dimensionless multiplier");
    check(saved.find("# mouse\r\n")!=std::string::npos &&
          saved.find("bind.Z=jump\r\ninvert_y=true\r\ntouch_autoaim=true\r\n")!=std::string::npos,
          "sensitivity save preserves binding/scalar/comment bytes");
    check(nova::platform::merge_mouse_sensitivity(saved,{.4f,.25f})==saved,"sensitivity merge idempotent");
    const auto later=parse(nova::platform::merge_mouse_sensitivity(saved,{.6f,restored.aim_multiplier}));
    check(later.aim_multiplier==.25f,"base edits preserve the saved relative multiplier");
    const auto unterminated=parse(nova::platform::merge_mouse_sensitivity("bind.Z=jump\r",{.3f,.08f}));
    check(unterminated.keys.at(SDLK_Z)==Action::jump && unterminated.aim_multiplier==.08f,
          "unterminated legacy line retains bytes and receives a valid separator");
    const auto duplicate=parse(nova::platform::merge_mouse_sensitivity(
        "sensitivity=.1\nads_sensitivity=.2\nsensitivity=.3\nads_sensitivity=.4\n",{.7f,.08f}));
    check(duplicate.sensitivity==.7f && duplicate.aim_multiplier==.08f,"all duplicate owned keys replaced");
    Controls controls(legacy); controls.gameplay=true;
    controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W));
    controls.replace_sensitivity({.4f,.07f});
    check(controls.take().down(Action::forward),"sensitivity changes do not disturb held physical actions");
    auto snapshot=controls.snapshot_config();
    check(snapshot.sensitivity==.4f && snapshot.aim_multiplier==.07f &&
          same_bindings(snapshot,legacy),"runtime snapshot carries pair without changing mappings");
    check(controls.look_config().sensitivity==.4f && close(controls.look_config(.5f,true).sensitivity,.014f),
          "both runtime scalars are observed through one synchronized pair");
    auto invalid=MouseSensitivity{.2f,std::numeric_limits<float>::quiet_NaN()};
    bool rejected=false; try { controls.replace_sensitivity(invalid); } catch(const std::invalid_argument &) { rejected=true; }
    check(rejected && controls.look_config().sensitivity==.4f,"invalid runtime pair is atomic");
    MouseSensitivityEdit editor({.4f,.07f});
    check(!editor.take(),"opening Controls never saves sensitivity");
    editor.edit({.5f,.07f}); check(!editor.take(),"drag remains staged until completion");
    editor.finish(); check(editor.take()==MouseSensitivity{.5f,.07f} && !editor.take(),"tab/close completion emits once");
    check(!editor.edit(invalid) && !editor.take(),"invalid editor value has no command");
    editor.edit({.6f,.07f}); editor.finish(); editor.edit({.6f,.08f});
    check(!editor.take(),"a newly edited row defers the completed pair until its own completion");
    editor.finish(); check(editor.take()==MouseSensitivity{.6f,.08f} && !editor.take(),"two row completions coalesce once");
    editor.edit({.6f,.07f}); editor.reset();
    check(editor.take()==MouseSensitivity{} && !editor.take(),"Reset supersedes pending edit and restores both defaults once");
}
void native_ads_sensitivity() {
    // Synthetic native storage with strict reads; use the production resolver
    // and control policy, not a cached button/selected-ADS result.
    std::map<uint32_t,uint32_t> words{{0x1170,0x2000},{0x2160,0},{0x2148,0x3000},
        {0x214c,0x3030},{0x300c,0x4000},{0x3024,0x5000}};
    std::map<uint32_t,uint8_t> bytes{{0x4060,0},{0x4061,0},{0x5060,0},{0x5061,1}};
    float native_factor=1;
    unsigned status_reads=0;
    auto r32=[&](uint32_t p,uint32_t o) { return words.at(p+o); };
    auto r8=[&](uint32_t p,uint32_t o) { ++status_reads; return bytes.at(p+o); };
    auto rf=[&](uint32_t p,uint32_t o) { check(p==0x1000 && o==0x2c,"read original player factor"); return native_factor; };
    auto status=[&] { return current_native_ads(0x1000,r32,r8); };
    Controls controls(parse("sensitivity=.1\naim_multiplier=2\n"));
    auto config=[&] { const auto aim=current_native_aim_scale(0x1000,r32,r8,rf);
        return controls.look_config(aim.factor,aim.aiming); };
    auto close_to=[](float a,float b) { return std::abs(a-b)<.00001f; };
    controls.gameplay=true; controls.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_RIGHT));
    check(status()==NativeAds::inactive && config().sensitivity==.1f,"held/refused RMB leaves hip sensitivity unchanged");
    // Native Aim has already published scaling during pending entry, before +61.
    native_factor=.25f; bytes[0x4060]=1;
    check(status()==NativeAds::pending && close_to(config().sensitivity,.05f),"pending native Aim uses the published factor immediately");
    bytes[0x4061]=1; bytes[0x4060]=0;
    const auto ads=look_delta(10,20,0,60,config());
    check(close_to(ads.yaw,-.5f) && close_to(ads.pitch,-1.f),"native factor and user multiplier affect both axes once");
    for(float zoom_factor:{.4f,.2f,.6f,1.f}) {
        native_factor=zoom_factor;
        check(close_to(config().sensitivity,.1f*zoom_factor*2),"continuous native sniper updates remain live, including factor one");
    }
    controls.replace_sensitivity({.2f,2.f}); native_factor=.25f;
    check(close_to(config().sensitivity,.1f),"base change also scales aiming speed");
    controls.replace_sensitivity({.1f,1.f});
    check(close_to(config().sensitivity,.025f),"default multiplier preserves original weapon factor");
    controls.replace_sensitivity({.1f,2.f});
    controls.replace_sensitivity({.1f,0.f});
    const auto stopped=look_delta(10,20,0,60,config());
    check(stopped.yaw==0 && stopped.pitch==0,"zero user gain stops both mouse axes during native aim");
    check(controls.look_config().sensitivity==.1f,"zero aim gain leaves ordinary/vehicle base unchanged");
    controls.replace_sensitivity({.1f,2.f});
    bytes[0x4061]=0; native_factor=1;
    check(status()==NativeAds::inactive && controls.take().down(Action::aim) && config().sensitivity==.1f,
          "native exit/sprint restores hip gain while RMB remains held");
    words[0x2160]=1; bytes.erase(0x4061); bytes.erase(0x4060); native_factor=.3f;
    check(status()==NativeAds::active && close_to(config().sensitivity,.06f),"weapon change rereads current native factor and receiver");
    controls.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_RIGHT));
    check(!controls.take().down(Action::aim) && status()==NativeAds::active,
          "physical release cannot replace native exit timing");
    const auto before=status_reads;
    for(auto index:{uint32_t(-1),2u,0x7fffffffu}) {
        words[0x2160]=index; check(status()==NativeAds::unknown,"invalid selected index is unknown");
        check(close_to(config().sensitivity,.03f),"unknown aim ownership preserves native gain without customization");
    }
    check(status_reads==before,"invalid vector index never reads weapon status");
    words[0x2160]=0; words[0x300c]=0; check(status()==NativeAds::unknown,"absent weapon has no custom aim authority");
    words[0x214c]=0x3001; check(status()==NativeAds::unknown,"malformed vector span is rejected");
    words[0x1170]=0; check(status()==NativeAds::unknown,"no manager means no aim authority");
    for(float bad:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        native_factor=bad; check(config().sensitivity==.1f,"invalid native factor cannot poison the mouse queue");
    }
    native_factor=0; check(config().sensitivity==0,"native zero factor is respected");
    check(current_native_aim_scale(0,r32,r8,rf).factor==1,"absent player causes no read");
    GuestLookInput pending{}; bool look_pending=true; unsigned preparations=0,consumptions=0;
    auto consume=[&] {
        mouse_look_boundary(look_pending,[] { return true; },[&] {
            ++preparations; queue_mouse_look(pending,10,20,ads);
        },[&] { ++consumptions; },[] {});
    };
    consume(); consume();
    check(preparations==1 && consumptions==2 && pending.yaw_target==10+ads.yaw &&
          pending.pitch_target==20+ads.pitch && pending.pitch_delta==ads.pitch,
          "aim retains once-only coherent queue preparation");
    auto invert=controls.look_config(.25f,true); invert.invert_y=true;
    check(look_delta(10,20,59.9f,60,invert).pitch<=.10001f,"aim preserves inversion and native pitch limit");
}
void capture_lifecycle() {
    ControlsConfig config;
    const auto original=config;
    BindingCapture capture;
    capture.event(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN),config); // keyboard activation
    capture.begin(Action::jump);
    check(!capture.armed(),"activation key must be released");
    capture.event(key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,true),config);
    check(capture.event(key(SDL_EVENT_KEY_UP,SDLK_RETURN),config) && capture.armed() &&
          !capture.take_completed(),"activation up is consumed, never bound");
    capture.event(key(SDL_EVENT_KEY_DOWN,SDLK_W),config);
    check(capture.active() && !capture.error().empty() && same_bindings(config,original),
          "conflict remains in capture without mutating mappings");
    capture.event(key(SDL_EVENT_KEY_UP,SDLK_W),config);
    capture.event(key(SDL_EVENT_KEY_DOWN,SDLK_ESCAPE),config);
    check(!capture.active() && !capture.take_completed() && same_bindings(config,original),"Escape cancels cleanly");
    check(capture.event(key(SDL_EVENT_KEY_UP,SDLK_ESCAPE),config),"cancel release never reaches widgets");
    capture.event(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_LEFT),config);
    capture.begin(Action::aim);
    check(!capture.armed(),"activation mouse held must be released");
    capture.event(mouse(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT),config);
    capture.event(wheel(1),config);
    check(capture.active() && !capture.error().empty(),"held action refuses wheel while capture continues");
    capture.event(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_X2),config);
    check(!capture.active() && capture.take_completed() && !capture.take_completed() &&
          config.buttons.at(SDL_BUTTON_X2)==Action::aim,"mouse completion emitted exactly once");
    check(capture.event(mouse(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_X2),config),"accepted mouse release swallowed");
    capture.begin(Action::jump);
    capture.cancel_mouse(SDL_BUTTON_LEFT);
    check(!capture.active() && !capture.take_completed() &&
          capture.event(mouse(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_LEFT),config),"Cancel mouse pair cannot click through");
    capture.begin(Action::reload);
    SDL_Event focus{}; focus.type=SDL_EVENT_WINDOW_FOCUS_LOST; capture.event(focus,config);
    check(!capture.active() && !capture.take_completed(),"focus loss cancels unfinished capture");
    capture.begin(Action::reload);
    capture.event(key(SDL_EVENT_KEY_DOWN,SDLK_K),config);
    capture.reset(); // Close/window transition before the next UI frame.
    check(capture.take_completed() && config.keys.at(SDLK_K)==Action::reload,
          "already completed mapping survives close until command handoff");
    capture.begin(Action::grenade); capture.reset();
    check(!capture.active() && !capture.take_completed(),"window/tab/close cancellation creates no command");
}
void runtime_and_owner() {
    ControlsConfig initial;
    Controls controls(initial); controls.gameplay=true;
    auto remapped=initial;
    check(rebind(remapped,Action::aim,{BindingKind::key,SDLK_B}).empty(),"keyboard held aim mapping");
    controls.replace_bindings(remapped);
    controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_B));
    NativeAimIntent intent; intent.sample(controls.take().down(Action::aim));
    check(intent.wanted() && !intent.permit_start(false,false,true,true),"held desire survives native refusal");
    intent.sample(controls.take().down(Action::aim));
    check(intent.wanted() && intent.permit_start(true,false,true,true),"same physical hold rearms native eligibility");
    controls.replace_bindings(remapped);
    controls.clear();
    check(controls.take().held.none(),"map replacement releases pending held and edge state");
    controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_B,true));
    check(!controls.take().down(Action::aim),"held autorepeat cannot reactivate replaced binding");
    controls.handle(key(SDL_EVENT_KEY_UP,SDLK_B));
    controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_B));
    check(controls.take().down(Action::aim),"release then fresh press rearms mapped aim");
    controls.handle(key(SDL_EVENT_KEY_UP,SDLK_B));
    intent.sample(controls.take().down(Action::aim));
    check(!intent.wanted() && intent.permit_exit(),"physical release still owns C04 exit desire");

    nova::platform::PanelInputOwner owner;
    BindingCapture capture;
    auto panel_config=controls.snapshot_config();
    unsigned guest_escape=0;
    auto dispatch=[&](const SDL_Event &e) {
        const auto route=owner.route(e,capture.active());
        if(route.visibility_changed) { controls.overlay(owner.visible()); capture.reset(); }
        if(route.release_owned) controls.clear();
        if(route.panel_event) capture.event(e,panel_config);
        if(!route.consume) {
            controls.handle(e);
            if((e.type==SDL_EVENT_KEY_DOWN || e.type==SDL_EVENT_KEY_UP) && e.key.key==SDLK_ESCAPE) ++guest_escape;
        }
    };
    dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_INSERT)); dispatch(key(SDL_EVENT_KEY_UP,SDLK_INSERT));
    capture.begin(Action::reload);
    dispatch(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_X1));
    check(capture.take_completed(),"panel capture completed");
    controls.replace_bindings(panel_config);
    dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_INSERT)); dispatch(key(SDL_EVENT_KEY_UP,SDLK_INSERT));
    dispatch(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_X1));
    check(controls.take().held.none(),"captured held mouse cannot leak through panel closure");
    dispatch(mouse(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_X1));
    dispatch(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_BUTTON_X1));
    check(controls.take().edge(Action::reload),"fresh post-capture mouse press acts exactly once");
    dispatch(mouse(SDL_EVENT_MOUSE_BUTTON_UP,SDL_BUTTON_X1));
    dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_INSERT)); dispatch(key(SDL_EVENT_KEY_UP,SDLK_INSERT));
    capture.begin(Action::jump);
    dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_ESCAPE)); dispatch(key(SDL_EVENT_KEY_UP,SDLK_ESCAPE));
    check(owner.visible() && !capture.active(),"first Escape cancels capture while Settings stays open");
    dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_ESCAPE)); dispatch(key(SDL_EVENT_KEY_UP,SDLK_ESCAPE));
    check(!owner.visible() && !guest_escape && !capture.take_completed(),"second Escape closes; neither pair leaks native Back");
}
}
int main() {
    try {
        model_and_persistence(); mouse_sensitivity(); native_ads_sensitivity(); capture_lifecycle(); runtime_and_owner();
        std::puts("controls rebinding model, persistence and ownership passed"); return 0;
    } catch(const std::exception &e) { std::fprintf(stderr,"controls rebinding: %s\n",e.what()); return 1; }
}
