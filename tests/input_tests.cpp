#include "input/input.h"
#include "input/controls.h"
#include "input/normal_keyboard_movement.h"
#include "input/script.h"
#include "loader/diagnostics.h"
#include "platform/guest_extent.h"
#include "platform/window_modes.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
struct TextSpy { unsigned starts=0,stops=0; bool active=false,password=false; };
bool text_start(void *user,SDL_Window *,bool password) {
    auto &spy=*static_cast<TextSpy *>(user); ++spy.starts; spy.active=true; spy.password=password; return true;
}
bool text_stop(void *user,SDL_Window *) {
    auto &spy=*static_cast<TextSpy *>(user); ++spy.stops; spy.active=false; return true;
}
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
SDL_Event mouse(Uint32 type, SDL_WindowID window, float x, float y) {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = window;
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.windowID = window;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
    }
    return event;
}
SDL_Event key(Uint32 type, SDL_WindowID window, SDL_Keycode code) {
    SDL_Event event{};
    event.type = type;
    event.key.windowID = window;
    event.key.key = code;
    return event;
}
} // namespace
int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixture and area required");
        check(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"), "dummy video hint");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        auto window = SDL_CreateWindow("Synthetic input", 640, 360, SDL_WINDOW_HIDDEN);
        check(window != nullptr, SDL_GetError());
        auto id = SDL_GetWindowID(window);
        nova::ImportRegistry imports;
        nova::ElfImage image(argv[1], imports);
        nova::Diagnostics diagnostics(image);
        nova::jni::Runtime runtime(image);
        TextSpy text_spy;
        const nova::input::TextInputApi text_api{&text_spy,text_start,text_stop};
        std::string mode = argv[2];
        // Supported internal resolution remains independent of window/pixel size.
        const nova::platform::GuestExtent extent=mode=="guest-extent"
            ? nova::platform::GuestExtent{1600,900} : nova::platform::GuestExtent{};
        nova::input::Input input(runtime, window, extent.width, extent.height,&text_api);
        auto count = reinterpret_cast<uint32_t *>(image.lookup("input_count"));
        auto records = reinterpret_cast<std::array<uint32_t, 5> *>(image.lookup("input_records"));
        auto expect = [&](size_t index, std::array<uint32_t, 5> expected) {
            check(index < *count && records[index] == expected, "guest event record");
        };
        if (mode == "menu-ack") {
            input.window_viewport({80,45,480,270});
            float x=0,y=0;
            const auto initial=input.diagnostic_touch_receipt();
            check(!initial.held && initial.down==0 && initial.up==0,"empty diagnostic receipt");
            check(!input.diagnostic_center(extent.width+1,extent.height,x,y),"reject mismatched extent");
            check(input.diagnostic_center(extent.width,extent.height,x,y) && x==320 && y==180,
                  "actual diagnostic center uses the content viewport");
            std::ostringstream text;text<<"frame 0\ndown "<<x<<' '<<y<<"\nup "<<x<<' '<<y<<'\n';
            std::istringstream source(text.str());auto pair=nova::input::Script::parse(source);
            check(pair.size()==2 && pair.last_frame()==0,"balanced same-frame Script");
            SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);pair.push(0,id,0);
            SDL_Event event;unsigned consumed=0;
            while(SDL_PollEvent(&event)) {
                if(!nova::input::Script::unwrap(event))continue;
                check(event.type==(consumed==0?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP),"pair order");
                input.handle(event,534);++consumed;
            }
            const auto complete=input.diagnostic_touch_receipt();
            check(consumed==2 && *count==2 && complete.down==1 && complete.up==1 && !complete.held,
                  "actual Input/JNI pair receipts and release");
            expect(0,{1,1,static_cast<uint32_t>(extent.width/2),static_cast<uint32_t>(extent.height/2),0});
            expect(1,{1,0,static_cast<uint32_t>(extent.width/2),static_cast<uint32_t>(extent.height/2),0});
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,x,y),535);
            const auto partial=input.diagnostic_touch_receipt();
            check(partial.down==2 && partial.up==1 && partial.held,"actual partial down owns contact");
            check(!input.diagnostic_center(extent.width,extent.height,x,y),"owned contact cannot arm another pair");
            input.release_all();const auto released=input.diagnostic_touch_receipt();
            check(released.down==2 && released.up==2 && !released.held && *count==4,"partial cleanup releases once");
            expect(2,{1,1,static_cast<uint32_t>(extent.width/2),static_cast<uint32_t>(extent.height/2),0});
            expect(3,{1,0,static_cast<uint32_t>(extent.width/2),static_cast<uint32_t>(extent.height/2),0});
            input.release_all();check(*count==4,"repeated cleanup cannot add another release");
            input.window_viewport({0,0,0,0});
            check(!input.diagnostic_center(extent.width,extent.height,x,y),"zero viewport cannot arm");
        } else if (mode == "guest-extent") {
            nova::platform::WindowSurface output;
            output.width=640; output.height=360; output.pixel_width=1280; output.pixel_height=720;
            const auto view=nova::platform::window_presentation(output,extent.width,extent.height);
            check(view.guest_width==1600 && view.guest_height==900 && view.pixels.x==0 &&
                  view.pixels.y==0 && view.pixels.w==1280 && view.pixels.h==720 &&
                  view.logical.x==0 && view.logical.w==640 && extent.aspect()==16.0/9,
                  "supported native extent/aspect fits independent pixels and logical input");
            input.window_viewport(view.logical);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,-1,180),10);
            check(*count==0,"nondefault aspect rejects its output bar");
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),11);
            input.handle(mouse(SDL_EVENT_MOUSE_MOTION,id,640,360),12);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,640,360),13);
            expect(0,{1,1,800,450,0});
            expect(1,{1,2,1600,900,0});
            expect(2,{1,0,1600,900,0});
            check(*count==3,"absolute menu mapping uses native extent rather than output size");
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),14);
            input.surface_extent(1920,1080);
            expect(3,{1,1,800,450,0}); expect(4,{1,0,800,450,0});
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),15);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,320,180),16);
            expect(5,{1,1,960,540,0}); expect(6,{1,0,960,540,0});
            input.surface_extent(1280,720);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),17);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,320,180),18);
            expect(7,{1,1,640,360,0}); expect(8,{1,0,640,360,0});
            check(*count==9,"hot extent releases old contact and updates real native inverse in both directions");
            input.surface_extent(3840,2160);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),19);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,320,180),20);
            expect(9,{1,1,1920,1080,0}); expect(10,{1,0,1920,1080,0});
            check(*count==11,"4K menu inverse reaches actual guest coordinates without changing output/window dimensions");
        } else if (mode == "communication-mouse") {
            nova::input::Controls controls(nova::input::ControlsConfig{});
            const auto route=[&](const SDL_Event &event) {
                if(!controls.handle(event)) input.handle(event,20);
            };
            input.window_viewport({80,45,480,270});
            for(const auto command:{SDLK_V,SDLK_TAB}) {
                controls.gameplay=true;controls.clear();
                route(key(SDL_EVENT_KEY_DOWN,id,command));
                check(controls.take_pressed(command==SDLK_V ? nova::input::Action::chat : nova::input::Action::scores),
                      "communication command consumed before touch route");
                route(key(SDL_EVENT_KEY_DOWN,id,SDLK_W));
                const auto before=*count;
                // Both panels retain native gameplay but release pointer capture.
                controls.communication_panel(true);controls.clear_except_movement();input.release_all();
                route(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180));
                route(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,320,180));
                expect(before,{1,1,640,360,0});expect(before+1,{1,0,640,360,0});
                const auto frame=controls.take();
                check(controls.active() && !controls.pointer_capture() && frame.down(nova::input::Action::forward) &&
                      !frame.requested(nova::input::Action::fire),"panel preserves held movement and absolute clicks without firing");
                bool moving=false,observed=false;unsigned moves=0,releases=0;
                const auto movement=[&](const nova::input::FrameInput &f) {
                    nova::input::queue_normal_keyboard_movement(f,moving,observed,[]{return true;},
                        [&](uint32_t type,float x,float y) { if(type==13) {check(x==0 && y==-1,"native forward movement vector");++moves;} else if(type==14) ++releases; });
                };
                movement(frame);check(moves==1,"panel movement reaches normal native event producer");
                route(key(SDL_EVENT_KEY_UP,id,SDLK_W));movement(controls.take());
                check(releases==1,"movement release remains live inside panel");
                route(key(SDL_EVENT_KEY_DOWN,id,SDLK_W));
                controls.communication_panel(false);controls.clear_except_movement();input.release_all();
                check(controls.pointer_capture() && controls.take().down(nova::input::Action::forward),"closing panel retains newly pressed movement and restores mouse look");
                route(key(SDL_EVENT_KEY_UP,id,SDLK_W));route(key(SDL_EVENT_KEY_UP,id,command));
                route(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,320,180));
                check(!controls.take().requested(nova::input::Action::fire),"closing release cannot fire");
                route(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180));
                check(controls.active() && controls.take().requested(nova::input::Action::fire) && *count==before+2,
                      "fresh gameplay click fires without a stray menu touch");
                route(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,320,180));
            }
        } else if (mode == "touch") {
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, id, 30, 40), 9);
            input.handle(mouse(SDL_EVENT_MOUSE_MOTION, id, 40, 50), 10);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, id + 1, 30, 40), 10);
            check(*count == 0, "startup/foreign/hover ignored");
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, id, 30.75f, 40.5f), 10);
            input.handle(mouse(SDL_EVENT_MOUSE_MOTION, id, 100, 120), 11);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP, id, 110, 130), 12);
            expect(0, {1, 1, 61, 81, 0});
            expect(1, {1, 2, 200, 240, 0});
            expect(2, {1, 0, 220, 260, 0});
            check(SDL_SetWindowSize(window, 1280, 720), "unscaled window");
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, id, 100, 200), 20);
            input.handle(mouse(SDL_EVENT_MOUSE_MOTION, id, -4, 300), 21);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP, id, 100, 200), 22);
            expect(3, {1, 1, 100, 200, 0});
            expect(4, {1, 0, static_cast<uint32_t>(-4), 300, 0});
            check(*count == 5, "outside releases once");
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, id, 50, 60), 23);
            SDL_Event focus{};
            focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            focus.window.windowID = id;
            input.handle(focus, 24);
            expect(6, {1, 0, 50, 60, 0});
        } else if (mode == "letterbox") {
            input.window_viewport({80,45,480,270});
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,10,180),10);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,10,180),11);
            check(*count==0,"Letterbox bar cannot synthesize native touch");
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),12);
            expect(0,{1,1,640,360,0});
            input.handle(mouse(SDL_EVENT_MOUSE_MOTION,id,79.75f,180),13);
            expect(1,{1,0,0,360,0});
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP,id,320,180),14);
            check(*count==2,"Subpixel bar exit releases once even when cast rounds to zero");
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,560,315),15);
            expect(2,{1,1,1280,720,0});
            input.window_viewport({0,0,640,360}); expect(3,{1,0,1280,720,0});
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),16);
            expect(4,{1,1,640,360,0});
            input.window_viewport({0,0,0,0}); expect(5,{1,0,640,360,0});
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,id,320,180),17);
            check(*count==6,"Zero drawable viewport suppresses touch");
        } else if (mode == "keys") {
            check(nova::input::Input::android_key(SDLK_ESCAPE) == 4 &&
                      nova::input::Input::android_key(SDLK_RETURN) == 66 &&
                      nova::input::Input::android_key(SDLK_KP_ENTER) == 66 &&
                      nova::input::Input::android_key(SDLK_BACKSPACE) == 67 &&
                      nova::input::Input::android_key(SDLK_W) == 0,
                  "menu key mapping");
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_ESCAPE), 3);
            check(*count == 0, "key startup guard");
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_ESCAPE), 10);
            auto repeat = key(SDL_EVENT_KEY_DOWN, id, SDLK_ESCAPE);
            repeat.key.repeat = true;
            input.handle(repeat, 11);
            input.handle(key(SDL_EVENT_KEY_UP, id, SDLK_ESCAPE), 12);
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_RETURN), 13);
            input.release_all();
            expect(0, {2, 4, 0, 0, 0});
            expect(1, {3, 4, 0, 0, 0});
            expect(2, {2, 66, 0, 0, 0});
            expect(3, {3, 66, 0, 0, 0});
            check(*count == 4, "repeat and release sequence");
        } else if (mode == "text") {
            auto klass = runtime.find_class("com/gameloft/android/ANMP/GloftN3HM/GL2JNILib");
            auto method = runtime.method(klass, "SetKeyboard", "(ILjava/lang/String;I)V", true);
            nova::jni::Value show, initial, length;
            show.i = 1;
            initial.l = runtime.string("  Pilot");
            length.i = 20;
            std::thread callback([&] {
                runtime.invoke(klass, method, {show, initial, length});
                runtime.detach();
            });
            callback.join();
            runtime.release(initial.l, 1);
            input.sync_keyboard();
            auto refs = runtime.live_references();
            SDL_Event text{};
            text.type = SDL_EVENT_TEXT_INPUT;
            text.text.windowID = id;
            text.text.text = "\xc3\xa9\xf0\x9f\x9a\x80 ";
            input.handle(text, 20);
            auto captured = reinterpret_cast<const char *>(image.lookup("input_text"));
            check(std::string(captured) == "Pilot\xc3\xa9\xf0\x9f\x9a\x80",
                  "standard UTF-8 and Java trim through JNI");
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_BACKSPACE), 21); // trailing space
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_BACKSPACE), 22); // surrogate pair
            check(std::string(captured) == "Pilot\xc3\xa9", "Unicode backspace");
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_RETURN), 23);
            check(*reinterpret_cast<int *>(image.lookup("input_visible")) == 1,
                  "finish callback before hide");
            input.sync_keyboard();
            auto visible = runtime.method(klass, "IsKeyboardVisible", "()I", true);
            check(runtime.invoke(klass, visible, {}).i == 0, "keyboard hidden after finish");
            input.handle(text, 24);
            check(*count == 4 && runtime.live_references() == refs,
                  "no hidden input or JNI reference leak");
            show.i = 2;
            initial.l = runtime.string("");
            length.i = 1;
            runtime.invoke(klass, method, {show, initial, length});
            runtime.release(initial.l, 1);
            input.sync_keyboard();
            text.text.text = "\xf0\x9f\x9a\x80";
            input.handle(text, 25);
            check(*count == 4, "length limit never splits surrogate pair");
            text.text.text = "ab";
            input.handle(text, 26);
            check(std::string(captured) == "a", "requested length limit");
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_ESCAPE), 27);
            input.sync_keyboard();
            show.i = 1;
            initial.l = runtime.string("");
            length.i = 8;
            runtime.invoke(klass, method, {show, initial, length});
            runtime.release(initial.l, 1);
            input.sync_keyboard();
            std::istringstream source("frame 28\ntext \"A \\\"B\\\"\"\n");
            auto script = nova::input::Script::parse(source);
            SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
            script.push(28, id, 0);
            SDL_Event event;
            while (SDL_PollEvent(&event))
                input.handle(event, 28);
            check(std::string(captured) == "A \"B\"",
                  "quoted script text through SDL and guest callback");
            runtime.release(klass, 1);
        } else if (mode == "ownership") {
            input.handle(key(SDL_EVENT_KEY_DOWN,id,SDLK_RETURN),10); input.release_all();
            const auto original_events=*count;
            auto klass=runtime.find_class("com/gameloft/android/ANMP/GloftN3HM/GL2JNILib");
            auto method=runtime.method(klass,"SetKeyboard","(ILjava/lang/String;I)V",true);
            nova::jni::Value show,initial,length; show.i=2; initial.l=runtime.string("Guest editor"); length.i=40;
            runtime.invoke(klass,method,{show,initial,length}); runtime.release(initial.l,1);
            input.sync_keyboard();
            check(text_spy.active && text_spy.password && text_spy.starts==1,"guest password editor already synchronized");
            input.suspend_keyboard(true);
            check(!text_spy.active && text_spy.stops==1 && input.keyboard_visible(),"panel takeover stops host text without hiding guest editor");
            input.sync_keyboard(); check(text_spy.starts==1,"suspended guest revision cannot steal overlay text");
            // Synthetic overlay IME ownership, with no SDL platform call.
            text_start(&text_spy,window,false); text_stop(&text_spy,window);
            input.suspend_keyboard(false); input.sync_keyboard(true);
            check(text_spy.active && text_spy.password && text_spy.starts==3,"close restores unchanged guest revision and password properties");
            input.suspend_keyboard(true); input.invalidate_keyboard();
            check(!text_spy.active,"focus loss stops restored guest editor");
            input.suspend_keyboard(false); input.sync_keyboard();
            check(text_spy.active && text_spy.password && text_spy.starts==4,"focus regain forces guest ownership restoration without callback");
            check(*count==original_events,"host text ownership never fabricates guest completion or key events");
            runtime.release(klass,1);
        } else if (mode == "script") {
            std::istringstream source("# synthetic\nframe 10\nclick 30 40\nwait 3\nkey BACK\nframe "
                                      "20\ndown 1 2\nwait 1\nmove 3 4\nwait 1\nup 5 6\n");
            auto script = nova::input::Script::parse(source);
            check(script.size() == 7 && script.last_frame() == 22, "parser expanded schedule");
            SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
            for (uint64_t frame = 0; frame <= 23; ++frame) {
                if (script.scheduled(frame))
                    script.push(frame, id, 0);
                SDL_Event event;
                while (SDL_PollEvent(&event))
                    input.handle(event, frame);
                if (frame == 9)
                    check(*count == 0, "not early");
                if (frame == 10)
                    check(*count == 1, "down after frame N");
                if (frame == 11)
                    check(*count == 2, "up after next frame");
                if (frame == 13)
                    check(*count == 2, "wait is relative to last event");
            }
            expect(0, {1, 1, 60, 80, 0});
            expect(1, {1, 0, 60, 80, 0});
            expect(2, {2, 4, 0, 0, 0});
            expect(3, {3, 4, 0, 0, 0});
            expect(4, {1, 1, 2, 4, 0});
            expect(5, {1, 2, 6, 8, 0});
            expect(6, {1, 0, 10, 12, 0});
            std::vector<std::array<uint32_t, 5>> scripted(records, records + *count);
            *count = 0;
            // Real SDL events enter precisely the handler that consumed the pushed script events.
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, id, 30, 40), 10);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP, id, 30, 40), 11);
            input.handle(key(SDL_EVENT_KEY_DOWN, id, SDLK_ESCAPE), 14);
            input.handle(key(SDL_EVENT_KEY_UP, id, SDLK_ESCAPE), 15);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, id, 1, 2), 20);
            input.handle(mouse(SDL_EVENT_MOUSE_MOTION, id, 3, 4), 21);
            input.handle(mouse(SDL_EVENT_MOUSE_BUTTON_UP, id, 5, 6), 22);
            check(scripted == std::vector<std::array<uint32_t, 5>>(records, records + *count),
                  "script/real path parity");
            for (auto bad : {"frame -1", "frame 8\nframe 2", "click 1", "wait 4294967296",
                             "frame 4294967295\nclick 1 2", "key UNKNOWN", "up 1 2", "down 1 2",
                             "move 1 2", "keydown ENTER", "keyup ENTER", "frame 2 junk",
                             "text unquoted", "text \"unterminated", "click nan 1"}) {
                bool rejected = false;
                try {
                    std::istringstream invalid(bad);
                    nova::input::Script::parse(invalid);
                } catch (const std::invalid_argument &e) {
                    rejected = std::strstr(e.what(), "line") != nullptr;
                }
                check(rejected, "invalid script has line diagnostic");
            }
        } else
            throw std::invalid_argument("Unknown input test");
        check(*reinterpret_cast<uint32_t *>(image.lookup("input_env")) ==
                  reinterpret_cast<uint32_t>(runtime.attach()),
              "input on UI JNIEnv");
        check(runtime.object(reinterpret_cast<void *>(
                                 *reinterpret_cast<uint32_t *>(image.lookup("input_receiver"))))
                      ->name == "com/gameloft/android/ANMP/GloftN3HM/GL2JNILib",
              "JNI class receiver");
        std::printf("input-%s passed; guest events=%u\n", mode.c_str(), *count);
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "input test: %s\n", error.what());
        return 1;
    }
}
