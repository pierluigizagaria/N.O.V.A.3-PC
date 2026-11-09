#include "input/controls.h"
#include "input/native_adapter.h"
#include "input/native_controls.h"
#include "platform/fov_panel.h"
#include "input/script.h"
#include "abi/guest_hook.h"
#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>
using namespace nova::input;
namespace {
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
uint32_t original = 0;
uint32_t __cdecl replacement(uint32_t n) { return nova::abi::invoke(original,{n}) * 2; }
SDL_Event key(Uint32 type,SDL_Keycode code) {
    SDL_Event e{}; e.type = type; e.key.key = code; return e;
}
}
int main(int argc,char **argv) {
    try {
        check(argc == 2,"fixture path");
        ControlsConfig defaults;
        check(defaults.keys.at(SDLK_Q)==Action::power && defaults.keys.at(SDLK_Z)==Action::previous_power &&
              defaults.keys.at(SDLK_X)==Action::next_power && defaults.keys.at(SDLK_C)==Action::next_grenade &&
              defaults.buttons.at(SDL_BUTTON_MIDDLE)==Action::grenade && !defaults.keys.contains(SDLK_G),
              "power and grenade defaults use Q/Z/X/C and middle mouse");
        std::istringstream conf("sensitivity = 0.25\ninvert_y = true\ntouch_autoaim = true\nbind.Z = jump\nbind.MOUSE2 = melee\nbind.WHEELUP = previous\n");
        auto config = ControlsConfig::parse(conf);
        check(config.sensitivity == .25f && config.invert_y && config.touch_autoaim &&
            config.keys.at(SDLK_Z) == Action::jump && config.buttons.at(SDL_BUTTON_RIGHT) == Action::melee &&
            config.wheel_up == Action::previous,"configuration parsing");
        for (auto bad : {"sensitivity=nan","sensitivity=-1","sensitivity=0","sensitivity=11",
             "invert_y=yes","bind.ESCAPE=fire","bind.NO=fire","bind.W=unknown","unknown=true"}) {
            bool rejected = false;
            try { std::istringstream stream(bad); ControlsConfig::parse(stream); }
            catch (const std::invalid_argument &) { rejected = true; }
            check(rejected,"bad config rejected");
        }
        auto delta = look_delta(100,40,0,85,defaults);
        check(delta.yaw == -10 && delta.pitch == -4,"look sensitivity/sign");
        check(look_delta(0,40,0,85,config).pitch == 10,"invert Y");
        check(look_delta(0,-100,84,85,defaults).pitch == 1 &&
              look_delta(0,100,-84,85,defaults).pitch == -1,"pitch clamp");
        Look accumulated{};
        for (int i=0;i<100;++i) { auto d = look_delta(1,1,accumulated.pitch,85,defaults);
            accumulated.yaw += d.yaw; accumulated.pitch += d.pitch; }
        auto single = look_delta(100,100,0,85,defaults);
        check(std::abs(accumulated.yaw-single.yaw) < .001f &&
              std::abs(accumulated.pitch-single.pitch) < .001f,"mouse counts independent of frame partition");
        Controls controls(defaults);
        check(!controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W)),"menus do not consume movement");
        controls.gameplay = true;
        for(auto code:{SDLK_Z,SDLK_X,SDLK_C}) {
            controls.handle(key(SDL_EVENT_KEY_DOWN,code));
            auto repeated=key(SDL_EVENT_KEY_DOWN,code); repeated.key.repeat=true; controls.handle(repeated);
            controls.handle(key(SDL_EVENT_KEY_UP,code));
            const auto selected=controls.take();
            check(selected.pressed.size()==1 && selected.edge(defaults.keys.at(code)),
                  "selection emits one edge per press, not per key repeat");
            check(controls.take().pressed.empty(),"selection edge drains once");
        }
        controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_W));
        controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_D));
        controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_SPACE));
        auto e = key(SDL_EVENT_KEY_DOWN,SDLK_SPACE); e.key.repeat = true; controls.handle(e);
        SDL_Event motion{}; motion.type = SDL_EVENT_MOUSE_MOTION; motion.motion.xrel = 17;
        motion.motion.yrel = -5; controls.handle(motion);
        auto frame = controls.take();
        check(frame.down(Action::forward) && frame.down(Action::right) && frame.pressed.size() == 3 &&
            frame.dx == 17 && frame.dy == -5,"action mapping and repeat suppression");
        frame = controls.take();
        check(frame.down(Action::forward) && frame.pressed.empty() && frame.dx == 0,"held persists, deltas drain");
        controls.handle(key(SDL_EVENT_KEY_UP,SDLK_W));
        check(!controls.take().down(Action::forward),"key release");
        controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_LSHIFT));
        controls.handle(key(SDL_EVENT_KEY_DOWN,SDLK_RSHIFT));
        controls.handle(key(SDL_EVENT_KEY_UP,SDLK_LSHIFT));
        check(controls.take().down(Action::sprint),"releasing one of two bindings keeps action held");
        SDL_Event click{}; click.type = SDL_EVENT_MOUSE_BUTTON_DOWN; click.button.button = SDL_BUTTON_LEFT;
        controls.handle(click); click.type = SDL_EVENT_MOUSE_BUTTON_UP; controls.handle(click);
        frame = controls.take();
        check(!frame.down(Action::fire) && frame.edge(Action::fire),"sub-frame click retains its pressed edge");
        click.type = SDL_EVENT_MOUSE_BUTTON_DOWN; click.button.button = SDL_BUTTON_RIGHT;
        controls.handle(click); click.type = SDL_EVENT_MOUSE_BUTTON_UP; controls.handle(click);
        check(controls.take().requested(Action::aim),"brief aim click requests one native frame");
        check(!controls.take().requested(Action::aim),"brief hold-aim releases on the next frame");
        controls.context(false,false);
        check(!controls.active() && controls.take().held.none(),"focus releases owned inputs");
        controls.context(true,true); check(!controls.active(),"text dialog releases capture");
        controls.context(true,false); check(controls.active(),"gameplay recaptures");
        controls.gameplay = false; check(!controls.active(),"pause releases capture");
        for (bool game : {false,true}) for (bool focus : {false,true}) for (bool text : {false,true})
            check(capture_policy(game,focus,text) == (game && focus && !text),"capture state combinations");
        check(!permit_input(true,false,false,SDL_EVENT_KEY_DOWN) &&
            !permit_input(true,false,false,SDL_EVENT_MOUSE_MOTION) &&
            permit_input(true,false,true,SDL_EVENT_KEY_DOWN) &&
            permit_input(true,true,false,SDL_EVENT_KEY_DOWN) &&
            permit_input(true,false,false,SDL_EVENT_WINDOW_CLOSE_REQUESTED),"physical input gate");
        for (auto bad_script : {"relative nan 0", "buttondown LEFT",
             "keydown W\nkeydown W", "focus elsewhere",
             "down 1 2\nbuttondown LEFT\nbuttonup LEFT\nup 1 2",
             "buttondown LEFT\nclick 1 2\nbuttonup LEFT"}) {
            bool rejected = false;
            try { std::istringstream source(bad_script); Script::parse(source); }
            catch (const std::invalid_argument &) { rejected = true; }
            check(rejected,"invalid native script or overlapping left-button ownership rejected");
        }
        check(SDL_Init(SDL_INIT_EVENTS),SDL_GetError());
        std::istringstream source("frame 10\nkeydown W\nkeydown SHIFT\nrelative 30 -40\nbuttondown LEFT\nwheel -1\nwait 2\nkeyup W\nkeyup SHIFT\nbuttonup LEFT\nfocus lost\nfocus gained\n");
        auto script = Script::parse(source);
        check(script.size() == 10 && script.last_frame() == 12,"extended script grammar");
        SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
        script.push(10,42,0);
        size_t count = 0, native_count = 0;
        while (SDL_PollEvent(&e)) {
            bool native_only = false;
            check(Script::unwrap(e,&native_only),"script source wrapper");
            ++count; native_count += native_only;
        }
        check(count == 5 && native_count == 3,"script frame boundary and native-only identity");
        e = key(SDL_EVENT_KEY_DOWN,SDLK_W); check(!Script::unwrap(e),"physical source identity");
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        {
            nova::platform::PanelInputOwner owner;
            Controls owned(defaults); owned.gameplay=true;
            for(auto code : {SDLK_W,SDLK_LSHIFT}) { auto event=key(SDL_EVENT_KEY_DOWN,code); owner.route(event); owned.handle(event); }
            for(auto code : {SDL_BUTTON_LEFT,SDL_BUTTON_RIGHT}) {
                SDL_Event event{}; event.type=SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button=static_cast<Uint8>(code);
                owner.route(event); owned.handle(event);
            }
            auto held=owned.take();
            check(held.down(Action::fire) && held.down(Action::aim) && held.down(Action::forward) && held.down(Action::sprint),"owned normal held state before overlay");
            std::array<uint32_t,12> native{1,1,1,0}; bool fire=true,aim=true,moving=true;
            const auto event=nova::abi::require_function(image,"normal_release_event");
            const auto unaim=nova::abi::require_function(image,"normal_release_aim");
            const auto receiver=reinterpret_cast<uint32_t>(native.data());
            const auto route=owner.route(key(SDL_EVENT_KEY_DOWN,SDLK_INSERT));
            check(route.release_owned,"overlay requests owned cleanup");
            owned.overlay(owner.visible()); owned.gameplay=true;
            if(!owned.active()) release_normal_owned(fire,aim,moving,
                [&](uint32_t type) { nova::abi::invoke(event,{receiver,type}); },
                [&] { return receiver; },[&](uint32_t target) { nova::abi::invoke(unaim,{target}); });
            check(native[0]==0 && native[1]==0 && native[2]==0 && native[3]==3 &&
                  native[4]==30 && native[5]==99 && native[6]==14,"overlay executes aligned native fire, UnAim and movement release in accepted order");
            release_normal_owned(fire,aim,moving,[&](uint32_t type) { nova::abi::invoke(event,{receiver,type}); },
                [&] { return receiver; },[&](uint32_t target) { nova::abi::invoke(unaim,{target}); });
            check(native[3]==3,"owned native release remains once-only");
            owner.set_visible(false); owned.overlay(false); owned.clear();
            check(owner.route(key(SDL_EVENT_KEY_DOWN,SDLK_W)).consume,"held normal key cannot cross close");
            owner.route(key(SDL_EVENT_KEY_UP,SDLK_W));
            auto fresh=key(SDL_EVENT_KEY_DOWN,SDLK_W);
            if(!owner.route(fresh).consume) owned.handle(fresh);
            check(owned.take().down(Action::forward),"normal movement rearms on fresh press");
        }
        auto consume = nova::abi::require_function(image,"guest_consume_look");
        GuestLookInput look{};
        std::array<float,2> actor{30,10};
        look.yaw_target = 30; look.yaw_pending = 1;
        actor[0] += 12; // The old early RotateYaw approach is overwritten.
        nova::abi::invoke(consume,{reinterpret_cast<uint32_t>(&look),reinterpret_cast<uint32_t>(actor.data())});
        check(actor[0] == 30,"fixture reproduces pending touch yaw overwrite");
        look.yaw_rate = look.yaw_rate_target = look.yaw_rate_step = 9;
        look.pitch_override = 20;
        queue_mouse_look(look,actor[0],actor[1],Look{12,-4});
        check(look.yaw_rate == 0 && look.yaw_rate_target == 0 && look.yaw_rate_step == 0 &&
              look.pitch_override == 0,"mouse inputs clear stale touch rates");
        nova::abi::invoke(consume,{reinterpret_cast<uint32_t>(&look),reinterpret_cast<uint32_t>(actor.data())});
        check(actor[0] == 42 && actor[1] == 6 && !look.yaw_pending,"native consumer applies both axes");
        nova::abi::invoke(consume,{reinterpret_cast<uint32_t>(&look),reinterpret_cast<uint32_t>(actor.data())});
        check(actor[0] == 42 && actor[1] == 6,"mouse delta consumed exactly once");
        for (int parts : {1,4,100}) {
            actor = {30,10};
            for (int i=0;i<parts;++i) {
                auto d = look_delta(120.f/parts,-600.f/parts,actor[1],60,defaults);
                queue_mouse_look(look,actor[0],actor[1],d);
                nova::abi::invoke(consume,{reinterpret_cast<uint32_t>(&look),reinterpret_cast<uint32_t>(actor.data())});
            }
            check(std::abs(actor[0]-18)<.001f && std::abs(actor[1]-60)<.001f,
                  "queued look preserves frame independence and native pitch limit");
        }
        FrameInput direction;
        direction.held.set(static_cast<size_t>(Action::sprint));
        check(!sprint_requested(direction),"Shift alone does not propel the player");
        direction.held.set(static_cast<size_t>(Action::right));
        check(!sprint_requested(direction),"pure strafe does not request rush");
        direction.held.set(static_cast<size_t>(Action::forward));
        check(sprint_requested(direction) && sprint_heading(direction) == -45,"forward right rush");
        direction.held.reset(static_cast<size_t>(Action::right));
        direction.held.set(static_cast<size_t>(Action::left));
        check(sprint_heading(direction) == 45,"forward left rush");
        direction.held.set(static_cast<size_t>(Action::right));
        check(sprint_heading(direction) == 0,"opposed lateral keys cancel");
        direction.held.set(static_cast<size_t>(Action::back));
        check(!sprint_requested(direction) && sprint_heading(direction) == 0,"back or opposed forward keys cancel rush");
        for (float heading : {-45.f,0.f,45.f}) {
            float yaw = 270, vx = 0, vy = 0;
            with_movement_heading(heading,[&] { return yaw; },[&](float d) { yaw += d; },[&] {
                vx = -std::sin(yaw*3.14159265f/180)*12;
                vy = std::cos(yaw*3.14159265f/180)*12;
            });
            check(yaw == 270 && std::abs(std::hypot(vx,vy)-12)<.001f,"rush preserves view and native speed");
            if (heading) check(std::abs(std::abs(vx)-std::abs(vy))<.001f,"rush diagonal changes direction, not speed");
        }
        float saved_yaw = 20;
        try { with_movement_heading(45,[&] { return saved_yaw; },[&](float d) { saved_yaw += d; },
                                    [] { throw std::runtime_error("synthetic native failure"); }); }
        catch (const std::runtime_error &) {}
        check(saved_yaw == 20,"temporary movement basis restores on failure");
        check(hide_scheme_draw(true,100,100) && !hide_scheme_draw(false,100,100) &&
              !hide_scheme_draw(true,200,100) && !hide_scheme_draw(true,0,0),
              "scheme draw skips only normal gameplay touch graphics, independent of child draw flags");
        const std::array<uint32_t,3> slots{300,100,900}, inventory{900,300,100};
        int selected = -1, queried = -1;
        auto resolve = [&](uint32_t id) { for (int i=0;i<3;++i) if (inventory[i] == id) return i; return -1; };
        auto available = [&](int i) { queried = i; return i != 2; };
        auto select = [&](int i) { selected = i; };
        auto selection = select_weapon_slot(slots,0,resolve,available,select);
        check(selection.status == SlotStatus::selected && selection.index == 1 && queried == 1 && selected == 1,
              "slot ID resolves in reordered player inventory, not catalog order");
        selection = select_weapon_slot(slots,1,resolve,available,select);
        check(selection.status == SlotStatus::unavailable && selected == 1,"native availability remains authoritative");
        selection = select_weapon_slot(slots,2,resolve,available,select);
        check(selection.status == SlotStatus::selected && selected == 0,"MP loadout third slot can resolve to inventory zero");
        for (size_t absent : {3u,4u}) check(select_weapon_slot(slots,absent,resolve,available,select).status == SlotStatus::absent,
                                          "absent fourth/fifth slots do not select arbitrary weapons");
        const std::array<uint32_t,1> missing{999}; queried = -1;
        check(select_weapon_slot(missing,0,resolve,available,select).status == SlotStatus::unavailable && queried == -1,
              "missing weapon IDs never reach native availability or selection");
        const std::array<uint32_t,4> stale_slots{100,999,300,300};
        auto pc_slots = available_weapon_slots(stale_slots,inventory,resolve,available);
        check(pc_slots == std::vector<uint32_t>({300,900}),
              "unowned preferred variants are skipped, pickups appended, duplicate IDs removed");
        pc_slots = available_weapon_slots(std::array<uint32_t,1>{100},inventory,resolve,available);
        check(pc_slots == std::vector<uint32_t>({900,300}),
              "campaign inventory remains selectable when all preferred slots are unavailable");
        pc_slots = available_weapon_slots(slots,inventory,resolve,[](int) { return true; });
        check(pc_slots == std::vector<uint32_t>({300,100,900}),"owned MP loadout order is preserved");
        const std::array<uint32_t,6> many{1,2,3,4,5,6};
        check(available_weapon_slots(many,many,[](uint32_t id) { return static_cast<int>(id); },
              [](int) { return true; }).size() == 5,"PC number keys expose at most five owned slots");
        auto boolean = nova::abi::require_function(image,"guest_boolean");
        check(!nova::abi::invoke_bool(boolean,{0}) && nova::abi::invoke_bool(boolean,{1}),
              "guest bool consumes only AL");
        bool relative_rejected = false;
        try { nova::abi::GuestHook bad(image,"hook_relative",{0xe8,0,0,0,0},
                reinterpret_cast<uint32_t>(&replacement)); }
        catch (const std::runtime_error &) { relative_rejected = true; }
        check(relative_rejected,"PC-relative prologue rejected without relocation guesses");
        auto subject = nova::abi::require_function(image,"hook_subject");
        check(nova::abi::invoke(subject,{5}) == 12,"aligned local-symbol guest call");
        bool mismatch = false;
        try { nova::abi::GuestHook bad(image,"hook_subject",{0x55,0x89,0xe5,0x83,0xec,0x14},
                reinterpret_cast<uint32_t>(&replacement)); }
        catch (const std::runtime_error &) { mismatch = true; }
        check(mismatch && nova::abi::invoke(subject,{5}) == 12,"mismatch leaves guest intact");
        {
            nova::abi::GuestHook hook(image,"hook_subject",{0x55,0x89,0xe5,0x83,0xec,0x18},
                reinterpret_cast<uint32_t>(&replacement));
            original = hook.original();
            check(nova::abi::invoke(subject,{5}) == 24 && nova::abi::invoke(original,{5}) == 12,
                "detour and aligned trampoline");
        }
        check(nova::abi::invoke(subject,{5}) == 12,"uninstall restores code");
        std::puts("native controls synthetic tests passed"); return 0;
    } catch (const std::exception &e) { std::fprintf(stderr,"controls test: %s\n",e.what()); return 1; }
}
