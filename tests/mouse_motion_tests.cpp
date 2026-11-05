#include "input/trace.h"
#include "input/native_adapter.h"
#include "input/script.h"
#include "abi/guest_hook.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <stdexcept>

using namespace nova::input;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
SDL_Event motion(float x = 1, float y = -.25f) {
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.xrel = x; e.motion.yrel = y; return e;
}
SDL_Event key(Uint32 type, SDL_Keycode code) {
    SDL_Event e{}; e.type = type; e.key.key = code; return e;
}
void route(Trace &trace, Controls &controls, SDL_Event e, uint64_t frame, bool scripted = false) {
    // The same dispatch diagnostics/action mapper as the UI loop. No OS input.
    if (permit_input(false,false,scripted,e.type)) {
        trace.dispatch(e,frame,scripted,controls.active());
        controls.handle(e);
    }
}
bool consume(Trace &trace, uint32_t guest, GuestLookInput &look, std::array<float,2> &actor,
             const FrameInput &input, uint64_t frame, const ControlsConfig &config) {
    auto delta = look_delta(input.dx,input.dy,actor[1],60,config);
    queue_mouse_look(look,actor[0],actor[1],delta);
    bool observed = trace.look_begin(frame,input,delta,60,actor[0],actor[1]);
    nova::abi::invoke(guest,{reinterpret_cast<uint32_t>(&look),reinterpret_cast<uint32_t>(actor.data())});
    trace.look_end(frame,actor[0],actor[1]);
    return observed;
}
}
int main(int argc, char **argv) {
    try {
        check(argc >= 3,"fixture and local diagnostic output paths required");
        const bool measure = argc == 4 && std::string(argv[3]) == "--measure";
        FILE *output = nullptr;
        check(fopen_s(&output,argv[2],"w+b") == 0,"open diagnostic output");
        std::setvbuf(output,nullptr,_IONBF,0); // Identical to the executable's stdout policy.
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        auto guest = nova::abi::require_function(image,"guest_consume_look");
        ControlsConfig config;
        config.sensitivity = .125f; // Exact binary totals make dropped counts unambiguous.

        // A busy/slow sink holds the same CRT FILE lock that UI and GL used to
        // share on every motion. Ordinary motion must finish without acquiring it.
        _lock_file(output);
        auto started = Clock::now();
        auto blocked = std::async(std::launch::async,[&] {
            Controls controls(config); controls.gameplay = true;
            Trace ui(false,output), render(false,output);
            GuestLookInput look{}; std::array<float,2> actor{};
            for (uint64_t frame=1; frame<120; ++frame) {
                for (int i=0; i<64; ++i) route(ui,controls,motion(),frame);
                consume(render,guest,look,actor,controls.take(),frame,config);
            }
            check(actor[0] == -952 && actor[1] == 60,"contended routing preserves counts and pitch clamp");
        });
        bool independent = blocked.wait_for(std::chrono::milliseconds(150)) == std::future_status::ready;
        auto free_time = std::chrono::duration<double,std::milli>(Clock::now()-started).count();
        _unlock_file(output);
        blocked.get();
        std::printf("mouse-log-lock independent=%d deadline_ms=150 elapsed_ms=%.3f\n",independent,free_time);
        if (!measure) check(independent,"physical motion blocks on synchronous diagnostic FILE lock");

        // Sustained bursts through SDL -> production trace -> Controls -> aligned
        // synthetic guest consumer. A concurrent producer races real take() locks.
        check(SDL_Init(SDL_INIT_EVENTS),SDL_GetError());
        SDL_FlushEvents(SDL_EVENT_FIRST,SDL_EVENT_LAST);
        Controls controls(config); controls.gameplay = true;
        Trace ui(false,output), render(false,output);
        GuestLookInput look{}; std::array<float,2> actor{};
        std::atomic<bool> done{false};
        std::atomic<uint64_t> frame_number{1};
        std::atomic<unsigned> routed{0};
        constexpr unsigned events = 32000;
        float total_x = 0, total_y = 0;
        std::vector<Action> edges;
        std::vector<double> frame_ms;
        unsigned look_reports = 0;
        started = Clock::now();
        auto producer = std::async(std::launch::async,[&] {
          try {
            for (unsigned i=0; i<events/64; ++i) {
                for (int n=0; n<64; ++n) {
                    auto e = motion(); check(SDL_PushEvent(&e),SDL_GetError());
                }
                if (i == 10) { auto e=key(SDL_EVENT_KEY_DOWN,SDLK_R); check(SDL_PushEvent(&e),SDL_GetError()); }
                if (i == 11) { auto e=key(SDL_EVENT_KEY_UP,SDLK_R); check(SDL_PushEvent(&e),SDL_GetError()); }
                if (i == 20) { auto e=key(SDL_EVENT_KEY_DOWN,SDLK_SPACE); check(SDL_PushEvent(&e),SDL_GetError()); }
                if (i == 21) { auto e=key(SDL_EVENT_KEY_UP,SDLK_SPACE); check(SDL_PushEvent(&e),SDL_GetError()); }
                if (i == 30 || i == 31) {
                    SDL_Event e{}; e.type=i == 30 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                    e.button.button=SDL_BUTTON_LEFT; check(SDL_PushEvent(&e),SDL_GetError());
                }
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    route(ui,controls,e,frame_number.load());
                    if (e.type == SDL_EVENT_MOUSE_MOTION) ++routed;
                }
            }
          } catch (...) { done = true; throw; }
            done = true;
        });
        auto drain = [&] {
            auto begin = Clock::now();
            auto input = controls.take();
            total_x += input.dx; total_y += input.dy;
            edges.insert(edges.end(),input.pressed.begin(),input.pressed.end());
            if (input.dx || input.dy) {
                look_reports += consume(render,guest,look,actor,input,frame_number,config);
                frame_ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());
            }
            ++frame_number;
        };
        do {
            drain();
            std::this_thread::yield();
        } while (!done);
        producer.get();
        drain();
        double elapsed = std::chrono::duration<double,std::milli>(Clock::now()-started).count();
        check(routed == events && total_x == events && total_y == -.25f*events,
              "sustained SDL routing preserves every relative count");
        check(actor[0] == -4000 && actor[1] == 60,"synthetic native orientation preserves sustained counts");
        check(edges == std::vector<Action>{Action::reload,Action::jump,Action::fire} && controls.take().held.none(),
              "key/button edges remain ordered and releases survive sustained motion");
        std::sort(frame_ms.begin(),frame_ms.end());
        std::printf("mouse-burst events=%u frames=%llu look_reports=%u elapsed_ms=%.3f consume_p95_ms=%.6f consume_max_ms=%.3f\n",
            events,frame_number.load()-1,look_reports,elapsed,
            frame_ms[(frame_ms.size()-1)*95/100],frame_ms.back());

        // Periodic frames also have no default diagnostic I/O. Mouse data still
        // reaches the guest every frame, including all ten former trace points.
        auto offset = std::ftell(output);
        unsigned samples = 0;
        for (uint64_t frame=1; frame<=1200; ++frame) {
            FrameInput input; input.dx=1;
            samples += consume(render,guest,look,actor,input,frame,config);
        }
        long bytes = std::ftell(output)-offset;
        std::printf("mouse-trace-budget frames=1200 samples=%u bytes=%ld\n",samples,bytes);
        if (!measure) check(samples == 0 && bytes == 0,"physical look including periodic frames performs no optional diagnostic output");
        Trace script(true,output);
        offset = std::ftell(output);
        route(script,controls,motion(),1,true);
        check(consume(script,guest,look,actor,controls.take(),1,config),"script look retains full diagnostics");
        check(std::ftell(output)>offset,"script source event retained");
        controls.context(false,false);
        route(ui,controls,motion(),1);
        check(controls.take().dx == 0,"focus loss does not queue motion");
        controls.context(true,false); controls.gameplay = false;
        check(!controls.handle(motion()),"menu routing remains available");
        std::fclose(output); SDL_Quit();
        std::puts(measure ? "mouse motion measurement complete (diagnostic assertions disabled)"
                          : "mouse motion regression passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr,"mouse motion test: %s\n",error.what()); return 1;
    }
}
