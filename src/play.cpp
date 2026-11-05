#include "abi/guest_call.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "camera/native_world_fov.h"
#include "movement_wobble/integration_gate.h"
#include "platform/movement_wobble_dispatch.h"
#include "movement_wobble/play_movement_wobble.h"
#include "platform/fov_panel.h"
#include "weapon_placement/position_gate.h"
#include "gles/runtime.h"
#include "gles/native_texture_filtering.h"
#include "input/input.h"
#include "input/script.h"
#include "input/native_controls.h"
#include "menu/dialog_escape.h"
#include "input/play_gamepad.h"
#include "java/boot.h"
#include "jni/runtime.h"
#include "loader/diagnostics.h"
#include "menu/native_menu_observer.h"
#include "ui/native_scale.h"
#include "ui/native_hud_scale.h"
#include "ui/native_hud_nodes.h"
#include "ui/native_pixel_resources.h"
#include "ui/native_menu_fonts.h"
#include "ui/native_artwork.h"
#include "ui/menu_text_trace.h"
#include "menu/diagnostic_menu_ack.h"
#include <sstream>
#include "loader/report.h"
#include "platform/angle.h"
#include "platform/preflight.h"
#include "platform/profile.h"
#include "platform/window_modes.h"
#include "platform/native_render_extent.h"
#include "platform/render_settings.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <filesystem>
#include <windows.h>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto lib = "com/gameloft/android/ANMP/GloftN3HM/GL2JNILib";
constexpr auto prefix = "Java_com_gameloft_android_ANMP_GloftN3HM_GL2JNILib_";
uint32_t positive(const char *text) {
    uint32_t value = 0;
    auto end = text + std::strlen(text);
    auto parsed = std::from_chars(text, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !value)
        throw std::invalid_argument("Expected a positive frame number");
    return value;
}
struct Timing {
    std::vector<double> frames, steps;
    uint64_t total = 0;
    void report() {
        if (frames.empty())
            return;
        auto sorted = frames;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0, step = 0;
        for (auto n : frames)
            sum += n;
        for (auto n : steps)
            step += n;
        std::printf("frames=%llu samples=%zu frame_ms mean=%.3f p50=%.3f p95=%.3f max=%.3f "
                    "step_ms mean=%.3f\n",
                    total, frames.size(), sum / frames.size(), sorted[sorted.size() / 2],
                    sorted[(sorted.size() - 1) * 95 / 100], sorted.back(), step / steps.size());
        frames.clear();
        steps.clear();
    }
};
[[noreturn]] void finish(int code) {
    nova::bionic::stream_flush(nullptr);
    std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    __assume(false);
}
// A bare double-click on the game executable hands off to the launcher beside
// it, which prepares files and starts the game with its arguments.
bool open_launcher() {
    std::wstring self(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (!count || count >= self.size())
        return false;
    self.resize(count);
    const auto directory = std::filesystem::path(self).parent_path();
    for (const wchar_t *name : {L"NOVA3.exe", L"nova3pc-launcher.exe"}) {
        const auto launcher = directory / name;
        if (!std::filesystem::is_regular_file(launcher))
            continue;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring command = L"\"" + launcher.wstring() + L"\"";
        if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            directory.c_str(), &startup, &process))
            return false;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }
    return false;
}
} // namespace
int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (argc < 2 && open_launcher())
        return 0;
    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage: nova3pc <library> [--profile name] [--profile-root directory] "
                     "[--obb-dir directory] [--backend d3d11|vulkan] [--frames N] "
                     "[--audio sdl|null] [--audio-dump local/output.wav] [--input-script file] "
                     "[--screenshot-after N] (repeatable) [--input-diagnostics] [--initial-menu-observer] [--diagnostic-menu-ack] [--net-log] [--file-log]\n");
        return 2;
    }
    try {
        nova::bionic::PathConfig paths;
        wchar_t executable[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, executable, MAX_PATH))
            throw std::runtime_error("Cannot locate executable");
        auto settings = nova::profile::read_config(
            std::filesystem::path(executable).parent_path() / "nova3pc.conf", paths.obb);
        std::filesystem::path audio_dump;
        std::filesystem::path input_script;
        bool allow_physical_input = false;
        bool input_diagnostics = false;
        bool menu_observer_enabled = false;
        bool menu_ack_enabled = false;
        uint32_t frame_limit = 0;
        std::set<uint32_t> screenshots;
        for (int i = 2; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--allow-physical-input") { allow_physical_input = true; continue; }
            if (option == "--input-diagnostics") { input_diagnostics = true; continue; }
            if (option == "--diagnostic-menu-ack") { menu_ack_enabled=true; continue; }
            if (option == "--initial-menu-observer") { menu_observer_enabled = true; continue; }
            if (option == "--file-log") {
                _putenv_s("NOVA_TRACE_FILES", "1");
                continue;
            }
            if (option == "--net-log") {
                settings.net_log = true;
                continue;
            }
            if (++i >= argc)
                throw std::invalid_argument("Missing option value: " + option);
            if (option == "--profile")
                paths.profile = argv[i];
            else if (option == "--profile-root")
                paths.profile_root = argv[i];
            else if (option == "--obb-dir")
                paths.obb = argv[i];
            else if (option == "--backend")
                settings.graphics_backend = argv[i];
            else if (option == "--audio")
                settings.audio = argv[i];
            else if (option == "--audio-dump")
                audio_dump = argv[i];
            else if (option == "--input-script")
                input_script = argv[i];
            else if (option == "--frames")
                frame_limit = positive(argv[i]);
            else if (option == "--screenshot-after")
                screenshots.insert(positive(argv[i]));
            else
                throw std::invalid_argument("Unknown option: " + option);
        }
        if(menu_ack_enabled) {
            if(allow_physical_input || !input_script.empty() || !audio_dump.empty() || settings.audio!="null" ||
               paths.profile_root.empty() || paths.profile=="default" || !screenshots.empty())
                throw std::invalid_argument("menu-ack requires isolated profile-root, null audio and no other input/capture");
            const auto isolated=std::filesystem::weakly_canonical(std::filesystem::current_path()/"local/diagnostics/menu-ack-once");
            if(std::filesystem::weakly_canonical(paths.profile_root)!=isolated/"profile" ||
               std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path())!=isolated/"runtime")
                throw std::invalid_argument("menu-ack requires copied menu-ack-once/runtime and profile");
            menu_observer_enabled=true;input_diagnostics=true;frame_limit=1200;
        }
        const bool scripted_session=!input_script.empty() || menu_ack_enabled;
        input_diagnostics = input_diagnostics || !input_script.empty();
        nova::input::Script script;
        if (!input_script.empty()) {
            std::ifstream stream(input_script);
            if (!stream)
                throw std::invalid_argument("Cannot open input script: " + input_script.string());
            script = nova::input::Script::parse(stream);
            if (frame_limit && script.last_frame() > frame_limit)
                throw std::invalid_argument("Input script extends beyond --frames");
            std::printf("input-script events=%zu last_frame=%llu\n", script.size(),
                        script.last_frame());
        }
        if (paths.obb.empty())
            paths.obb = std::filesystem::current_path() / "local/game/obb";
        nova::bionic::configure_paths(paths);
        nova::profile::configure(settings);
        nova::platform::preflight_profile();
        nova::audio::configure({settings.audio, audio_dump});
        const auto start = Clock::now();
        nova::ImportRegistry imports;
        nova::bionic::register_boot(imports);
        nova::audio::register_opensl(imports);
        nova::gles::register_imports(imports);
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1], imports);
        nova::Diagnostics diagnostics(image);
        // T09j2 scales the first initial-menu generation at the saved startup
        // extent. Live Apply recreation remains separate; gameplay QA is manual.
        nova::menu::NativeMenuObserver menu_observer(image, menu_observer_enabled, true);
        nova::ui::RequestedScale ui_scale(settings.ui_scale);
        nova::ui::NativeScale native_ui_scale(image,ui_scale);
        nova::ui::NativeHudScale native_hud_scale(image,ui_scale);
        nova::ui::NativeHudNodes native_hud_nodes(image,ui_scale);
        nova::ui::NativeMenuFonts native_menu_fonts(image);
        nova::ui::NativePixelResources native_pixel_resources(image);
        nova::ui::NativeArtwork native_artwork(image);
        // Provisional T09p capture: bounded menu label paths, reported off-hook.
        nova::ui::MenuTextTrace::configure(true);
        nova::platform::NativeDetail native_detail(image,settings.lod_distance);
        nova::gles::NativeTextureFiltering texture_filtering(image,settings.texture_filtering);
        nova::report_imports(image);
        const bool overlay_enabled=!scripted_session;
        nova::camera::NativeWorldFov world_fov(image,settings.world_fov,nullptr,nullptr,
                                               settings.weapon_fov,overlay_enabled);
        nova::movement_wobble::RequestedStrength movement_options(settings.movement_wobble);
        nova::movement_wobble::PlayMovementWobble movement(image,movement_options,
            input_script.empty() && !input_diagnostics && frame_limit==0 && !menu_observer_enabled && !menu_ack_enabled);
        nova::platform::NativeRenderExtent native_render(image,argv[1],world_fov);
        auto guest_extent=nova::platform::startup_guest_extent(settings.render_extent,&native_render.profile()).active;
        const auto startup_extent=guest_extent;
        auto java_extent=std::make_shared<nova::platform::GuestExtentState>(guest_extent);
        nova::platform::RenderSettings render_settings(guest_extent);
        image.run_initializers();
        const auto controls_path=std::filesystem::path(executable).parent_path()/"controls.conf";
        nova::input::Controls controls(nova::input::ControlsConfig::read(controls_path));
        nova::input::NativeControls native_controls(image, controls, input_diagnostics);
        nova::menu::DialogEscape dialog_escape(image);
        nova::input::Trace input_trace(input_diagnostics);
        nova::jni::Runtime runtime(image);
        nova::java::register_boot(runtime,guest_extent,java_extent);
        uint32_t onload_args[] = {reinterpret_cast<uint32_t>(runtime.vm()), 0};
        auto version = nova::abi::call_guest(image.lookup("JNI_OnLoad"), onload_args, 2);
        std::printf("JNI_OnLoad returned=0x%08x UI_thread=%lu\n", version, GetCurrentThreadId());
        if (version != 0x10004 && version != 0x10006)
            throw std::runtime_error("Unsupported JNI version");
        nova::java::startup(runtime);

        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE,"1");
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE,"0");
        // Window/taskbar icon from the executable's icon resource 1, when built with one.
        SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON,"1");
        SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL,"1");
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw std::runtime_error(SDL_GetError());
        SDL_Window *window = SDL_CreateWindow("N.O.V.A. 3 PC", 1280, 720,
                                            SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        if(!SDL_SetWindowMinimumSize(window,640,360)) throw std::runtime_error(SDL_GetError());
        nova::input::Input input(runtime, window, guest_extent.width, guest_extent.height);
        nova::input::PlayGamepad gamepad(controls,nova::input::PlayGamepad::physical_session(
            scripted_session,input_diagnostics,frame_limit!=0,menu_observer_enabled));
        nova::platform::PanelBackendEffects panel_effects;
        nova::platform::PanelEffectsArbiter effects_arbiter(panel_effects);
        nova::platform::PanelInputOwner panel_owner;
        std::string movement_save_error,ui_scale_error,graphics_quality_error;
        nova::platform::FovPanel panel({settings.world_fov,settings.weapon_fov},controls.snapshot_config(),settings.movement_wobble,settings.ui_scale);
        panel.initialize_graphics_quality({settings.lod_distance,settings.texture_filtering});
        panel.initialize_weapon_position(settings.weapon_position);
        if(overlay_enabled) panel.ui_initialize(window,&panel_effects);
        nova::platform::FovSaveResult last_save;
        std::string preview_error,controls_error;
        std::string weapon_position_error;
        bool bindings_unsaved=false,sensitivity_unsaved=false;
        const auto config_path=std::filesystem::path(executable).parent_path()/"nova3pc.conf";
        const nova::platform::GuestTextSync guest_text{
            &input,
            [](void *user) { static_cast<nova::input::Input *>(user)->suspend_keyboard(true); },
            [](void *user,bool force) {
                auto &guest=*static_cast<nova::input::Input *>(user);
                guest.suspend_keyboard(false); guest.sync_keyboard(force);
            }
        };
        auto ownership=[&](const nova::platform::PanelRoute &route) {
            if(route.visibility_changed) {
                controls.overlay(panel_owner.visible());
                panel.ui_visible(panel_owner.visible());
            }
            if(route.release_owned) { gamepad.suspend(); controls.clear(); input.release_all(); }
            if(route.invalidate_text) input.invalidate_keyboard();
            if(route.invalidate_draw) panel.invalidate_draw();
        };
        nova::platform::WindowSurfaceMailbox window_surface;
        auto prepare_window_transition=[&] {
            gamepad.suspend();
            controls.clear(); input.release_all(); input.invalidate_keyboard();
            if(overlay_enabled) {
                panel.ui_window_transition();
                panel_owner.relative_applied(false);
                effects_arbiter.release_pointer(window);
            }
        };
        const nova::platform::WindowModeHooks window_hooks{
            &prepare_window_transition,
            [](void *user) { (*static_cast<decltype(prepare_window_transition) *>(user))(); }
        };
        nova::platform::WindowModes window_modes(window,window_surface,{},window_hooks);
        std::string display_error;
        const auto initial_display=window_modes.startup(settings.display);
        display_error=initial_display.error;
        auto persist_display=[&](const nova::platform::WindowModeRoute &route) {
            display_error=route.error;
            if(route.changed && route.error.empty()) {
                const auto saved=nova::platform::save_display_config(config_path,window_modes.settings());
                if(!saved.saved) display_error="Could not save display settings: "+saved.error;
            }
        };
        input.window_viewport(nova::platform::window_presentation(window_surface.snapshot(),guest_extent.width,guest_extent.height).logical);
        auto hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                           SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!hwnd)
            throw std::runtime_error("SDL did not expose the Win32 window");
        const auto display = SDL_GetDisplayForWindow(window);
        const auto mode = SDL_GetCurrentDisplayMode(display);
        const double refresh = mode && mode->refresh_rate >= 30 && mode->refresh_rate <= 360
                                   ? mode->refresh_rate
                                   : 60.0;
        const auto frame_period = std::chrono::nanoseconds(static_cast<int64_t>(1e9 / refresh));
        std::printf("window visible=%d display_refresh=%.3f Hz; V-Sync plus frame ceiling\n",
                    IsWindowVisible(static_cast<HWND>(hwnd)) ? 1 : 0, refresh);
        std::atomic<nova::platform::Angle *> angle{nullptr};
        // The Java class starts at 100: suppress exactly the first swap.
        std::atomic<int> no_swap{100};
        std::atomic<bool> stop{false}, done{false}, end_requested{false}, ready{false};
        std::atomic<uint64_t> completed_frames{0};
        // At script boundaries only, keep the next guest step behind UI dispatch.
        // Menu JNI input runs on UI; native gameplay actions are drained on GL.
        constexpr uint64_t no_gate = UINT64_MAX;
        std::atomic<uint64_t> input_gate{no_gate};
        std::mutex gate_mutex;
        std::atomic<bool> ack_pending{false},ack_pair_complete{false},ack_failed{false},ack_observed{false};
        int ack_width=0,ack_height=0; // Published before input_gate; GL parked until UI receipt.
        nova::diagnostic::t09h::Gate ack_policy;
        std::unique_ptr<nova::diagnostic::t09h::NativeSnapshotReader> ack_reader;
        if(menu_ack_enabled) ack_reader=std::make_unique<nova::diagnostic::t09h::NativeSnapshotReader>(image,native_render.profile());
        std::condition_variable gate_cv;
        auto synchronize_input = [&](uint64_t frame) {
            if (menu_ack_enabled || !script.scheduled(frame))
                return;
            std::unique_lock lock(gate_mutex);
            if (end_requested)
                return;
            input_gate = frame;
            gate_cv.wait(lock, [&] { return input_gate == no_gate; });
        };
        std::string render_error;
        runtime.register_method(lib, "setCurrentContext", "(I)Z", [&](auto &, auto, const auto &a) {
            nova::jni::Value result;
            auto egl = angle.load();
            result.z = egl && egl->make_current(a[0].i);
            return result;
        });
        runtime.register_method(
            lib, "setViewSettings", "(IIIII)V", [](auto &, auto, const auto &a) {
                std::printf("view request: color=%d depth=%d stencil=%d coverage=%d nonlinear=%d; "
                            "port surface=RGBA8/D24S8\n",
                            a[0].i, a[1].i, a[2].i, a[3].i, a[4].i);
                return nova::jni::Value{};
            });
        runtime.register_method(lib, "setNoSwapFrames", "(I)V", [&](auto &, auto, const auto &a) {
            int old = no_swap.load();
            while (a[0].i > old && !no_swap.compare_exchange_weak(old, a[0].i)) {
            }
            return nova::jni::Value{};
        });
        std::thread render([&] {
            nova::platform::Angle *egl=nullptr;
            bool panel_gl_attempted=false;
            try {
                world_fov.bind_render_thread();
                movement.bind_render_thread();
                std::printf("GL_thread=%lu (separate from UI)\n", GetCurrentThreadId());
                // One image per process: keep graphics alive for guest loading threads until exit.
                egl = new nova::platform::Angle(settings.graphics_backend);
                auto klass = runtime.find_class(lib);
                auto native = [&](const char *name, std::vector<uint32_t> args = {}) {
                    return runtime.native(std::string(prefix) + name, klass, args);
                };
                native("initGL"); // ConfigChooser calls these before creating the Android context.
                native("InitViewSettings");
                native("SetDepthValue", {24});
                int requested = static_cast<int>(native("getNumExtraContext"));
                int actual = egl->create(hwnd, startup_extent.width, startup_extent.height, requested,&native_render.profile());
                const nova::platform::GuestExtent actual_startup{egl->main_target().width,egl->main_target().height};
                java_extent->publish(actual_startup);
                render_settings.initialize(actual_startup,egl->render_limits());
                menu_observer.configure_initial_targets(actual_startup,egl->render_limits());
                angle.store(egl);
                if(overlay_enabled) { panel_gl_attempted=true; panel.gl_initialize(); }
                if (actual != requested)
                    native("setNumExtraContext", {static_cast<uint32_t>(actual)});
                native("resize", {static_cast<uint32_t>(actual_startup.width),static_cast<uint32_t>(actual_startup.height)});
                std::printf("resize complete; entering step loop\n");
                ready = true;
                synchronize_input(0);
                const auto step = image.lookup(std::string(prefix) + "step");
                uint32_t args[] = {reinterpret_cast<uint32_t>(runtime.attach()),
                                   reinterpret_cast<uint32_t>(klass)};
                Timing timing;
                auto apply_render=[&](const nova::platform::RenderRequest &request) {
                    if(request.action==nova::platform::RenderAction::keep) {
                        const auto saved=nova::platform::save_render_config(config_path,request.extent);
                        render_settings.complete(request,true,saved.saved,saved.saved ? std::string{} : "Could not save resolution: "+saved.error);
                        return;
                    }
                    struct MainContext {
                        nova::platform::Angle &egl; int previous;
                        explicit MainContext(nova::platform::Angle &value):egl(value),previous(value.current_index()) {
                            if(previous<0 || (previous!=0 && !egl.make_current(0)))
                                throw std::runtime_error("Cannot select resolution context");
                        }
                        ~MainContext() { if(previous!=0 && !egl.make_current(previous)) std::terminate(); }
                    } context(*egl);
                    const auto before=render_settings.snapshot().active;
                    if(const auto reason=native_render.unavailable_reason(); !reason.empty()) {
                        std::printf("render-apply rejected requested=%dx%d reason=%s\n",
                            request.extent.width,request.extent.height,reason.c_str());
                        render_settings.complete(request,false,false,reason); return;
                    }
                    std::unique_ptr<nova::platform::Angle::TargetChange> change;
                    try {
                        change=egl->prepare_target(request.extent.width,request.extent.height,&native_render.profile());
                        change->activate();
                    } catch(const std::exception &error) {
                        render_settings.complete(request,false,false,std::string("Could not prepare resolution: ")+error.what()); return;
                    }
                    java_extent->publish(request.extent);
                    // An exception from the native adapter means recovery could
                    // not be proved. Do not convert it into an unchanged result.
                    const auto applied=native_render.apply(request.extent);
                    if(!applied.applied) {
                        java_extent->publish(before); change->rollback();
                        if(!egl->target_matches(before.width,before.height) || native_render.current()!=before)
                            throw std::runtime_error("Resolution rollback verification failed");
                        std::printf("render-apply rejected requested=%dx%d reason=%s\n",
                            request.extent.width,request.extent.height,applied.error.c_str());
                        render_settings.complete(request,false,false,applied.error); return;
                    }
                    if(!egl->target_matches(request.extent.width,request.extent.height) || native_render.current()!=request.extent) {
                        java_extent->publish(before);
                        const auto restored=native_render.apply(before);
                        change->rollback();
                        if(!restored.applied || !egl->target_matches(before.width,before.height) || native_render.current()!=before)
                            throw std::runtime_error("Resolution recovery verification failed");
                        render_settings.complete(request,false,false,"Could not verify the new resolution."); return;
                    }
                    change->commit();
                    render_settings.complete(request,true,false,{});
                    std::printf("render-apply active=%dx%d action=%s\n",request.extent.width,request.extent.height,
                        request.action==nova::platform::RenderAction::revert ? "revert" : "preview");
                };
                while (!stop) {
                    auto begin = Clock::now();
                    const auto surface=window_surface.snapshot();
                    const bool presentable=!surface.minimized && surface.pixel_width>0 && surface.pixel_height>0;
                    if(presentable) egl->presentation_size(surface.pixel_width,surface.pixel_height);
                    if(const auto request=render_settings.take()) apply_render(*request);
                    // JNI local references are scoped to each onDrawFrame native entry.
                    auto env = runtime.attach();
                    env->frames.emplace_back();
                    if(overlay_enabled) world_fov.service_options(timing.total + 1);
                    movement.prepare_step(); // HOST: before borrowed native Main/JNI storage.
                    native_controls.begin_frame(timing.total + 1);
                    nova::abi::call_guest(step, args, 2);
                    native_controls.end_frame();
                    movement.finish_step(); // HOST: release leases before further callbacks.
                    // T09p also reports the already-collected initial artwork/RTT
                    // selection, so source density can be joined to label evidence.
                    menu_observer.report(stdout);
                    nova::ui::MenuTextTrace::report(stdout);
                    auto refs = std::move(env->frames.back());
                    env->frames.pop_back();
                    for (auto reference : refs)
                        runtime.release(reference, 1);
                    auto after_step = Clock::now();
                    ++timing.total;
                    // Requests can now be attempted. Scene/resource readiness
                    // is checked only on Apply, never through per-frame GL queries.
                    if(timing.total==1) render_settings.available(true);
                    completed_frames = timing.total;
                    if(menu_ack_enabled && !end_requested) {
                        using namespace nova::diagnostic::t09h;
                        try {
                            const auto fresh=ack_reader->snapshot(timing.total,controls.gameplay.load(),menu_observer);
                            const auto decision=ack_policy.observe(fresh.state);
                            if(decision.action==Action::dispatch_balanced_pair) {
                                std::unique_lock lock(gate_mutex);
                                std::printf("menu-ack ready frame=%llu splash=%llx manager=%llx generation=%llu below_menu=%u moga_clear=%u ack_clear=%u no_player=%u no_gameplay=%u touch=%dx%d\n",
                                    timing.total,fresh.state.splash_identity,fresh.state.manager_identity,fresh.state.generation,
                                    unsigned(fresh.state.underlying_menu),unsigned(fresh.state.moga_clear),unsigned(fresh.state.acknowledgement_clear),
                                    unsigned(fresh.state.player_absent),unsigned(fresh.state.gameplay_absent),fresh.width,fresh.height);
                                ack_width=fresh.width;ack_height=fresh.height;ack_pending=true;
                                input_gate=timing.total;
                                gate_cv.wait(lock,[&]{return input_gate==no_gate;});
                                if(!ack_policy.delivered_pair(ack_pair_complete.load())) { ack_failed=true;end_requested=true; }
                                ack_pending=false;
                            } else if(decision.action==Action::stop_failed || decision.action==Action::stop_observed) {
                                ack_failed=decision.action==Action::stop_failed;ack_observed=decision.action==Action::stop_observed;end_requested=true;
                                std::printf("menu-ack terminal frame=%llu action=%u reason=%u\n",timing.total,unsigned(decision.action),unsigned(decision.reason));
                            }
                        } catch(const std::exception& error) {
                            std::fprintf(stderr,"menu-ack snapshot failure: %s\n",error.what());
                            ack_failed=true;end_requested=true;
                        }
                    }
                    // Preserve ordinary GamePause cooperation: GL remains live until UI sets stop.
                    if (timing.total == 1)
                        std::printf(
                            "first_step_ms=%.3f\n",
                            std::chrono::duration<double, std::milli>(after_step - start).count());
                    if (screenshots.contains(static_cast<uint32_t>(timing.total))) {
                        auto filename = paths.profile + "-" + settings.graphics_backend +
                                        "-frame-" + std::to_string(timing.total) + ".png";
                        egl->capture(std::filesystem::current_path() / "local/screenshots" /
                                     filename);
                    }
                    int skip = no_swap.load();
                    if (skip > 0 || !presentable) {
                        if(overlay_enabled) panel.gl_presentable(false);
                        while (skip>0 && !no_swap.compare_exchange_weak(
                            skip, skip == 100 ? 0 : std::max(0, skip - 1))) {
                        }
                        if(!presentable) SDL_Delay(16);
                    } else {
                        if(overlay_enabled) {
                            panel.gl_presentable(true);
                            egl->present([](void *user,int width,int height) {
                                static_cast<nova::platform::FovPanel *>(user)->gl_draw(width,height);
                            },&panel);
                        } else egl->present();
                        // Some drivers return immediately for an occluded window even with
                        // interval 1. Avoid running the Android frame loop unbounded there.
                        auto remaining = begin + frame_period - Clock::now();
                        if (remaining > Clock::duration::zero())
                            SDL_DelayPrecise(static_cast<Uint64>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(remaining)
                                    .count()));
                    }
                    synchronize_input(timing.total);
                    // Optional reports can themselves wait on the shared log sink.
                    // Disabled mode retains no samples; enabled mode clears each 120.
                    if (input_diagnostics) {
                        timing.frames.push_back(
                            std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
                        timing.steps.push_back(
                            std::chrono::duration<double, std::milli>(after_step - begin).count());
                        if (timing.total % 120 == 0)
                            timing.report();
                    }
                    if (frame_limit && timing.total >= frame_limit)
                        end_requested = true;
                }
                timing.report();
                runtime.release(klass, 1);
                if(overlay_enabled) {
                    if(!egl->make_current(0)) throw std::runtime_error("Cannot select overlay shutdown context");
                    panel.gl_shutdown(); panel_gl_attempted=false;
                }
                egl->make_current(-1);
                runtime.detach();
            } catch (const std::exception &error) {
                movement.finish_step();
                render_error = error.what();
                if(panel_gl_attempted && egl && egl->make_current(0)) {
                    try { panel.gl_shutdown(); }
                    catch(const std::exception &cleanup) { render_error+="; overlay cleanup: "; render_error+=cleanup.what(); }
                }
            }
            done = true;
        });
        bool pause_sent = false, focused = SDL_GetKeyboardFocus()==window, captured = false;
        std::string input_error;
        auto synchronize_render_input=[&] {
            const auto state=render_settings.snapshot();
            if(state.busy) return false;
            if(state.active!=guest_extent) {
                input.surface_extent(state.active.width,state.active.height); guest_extent=state.active;
                input.window_viewport(nova::platform::window_presentation(window_surface.snapshot(),guest_extent.width,guest_extent.height).logical);
            }
            return true;
        };
        auto gamepad_context=[&] {
            return nova::input::PlayGamepadContext{ready.load(),focused,input.keyboard_visible(),
                overlay_enabled && panel_owner.visible(),overlay_enabled && panel.ui_binding_capture(),
                render_settings.snapshot().busy,end_requested.load()};
        };
        auto ui_previous=Clock::now();
        while (!done) {
            const auto gate = input_gate.load();
            const auto ack_before=input.diagnostic_touch_receipt();
            unsigned ack_events=0;
            try {
                synchronize_render_input();
                if(overlay_enabled) input.suspend_keyboard(panel_owner.visible() || !focused);
                input.sync_keyboard();
                controls.context(focused,input.keyboard_visible());
                gamepad.context(gamepad_context());
                bool capture = controls.pointer_capture();
                if (capture != captured) {
                    input.release_all();
                    controls.clear_except_movement();
                    // Script runs exercise the capture policy without warping, hiding or grabbing
                    // the operator's real cursor. Real sessions use Windows raw relative input.
                    if (!overlay_enabled && !scripted_session && !SDL_SetWindowRelativeMouseMode(window,capture))
                        throw std::runtime_error(SDL_GetError());
                    captured = capture;
                    std::printf("input-capture frame=%llu active=%d mode=%s\n",completed_frames.load(),
                        capture,!scripted_session ? "SDL-relative" : "script-logical");
                }
                if (gate != no_gate && !end_requested) {
                    if(menu_ack_enabled) {
                        float x=0,y=0;
                        if(!ack_pending || !input.diagnostic_center(ack_width,ack_height,x,y))
                            throw std::runtime_error("menu-ack invalid UI readiness");
                        std::printf("menu-ack UI center=%.3f,%.3f native_extent=%dx%d\n",x,y,ack_width,ack_height);
                        std::ostringstream text;text.precision(9);
                        text<<"frame 0\ndown "<<x<<' '<<y<<"\nup "<<x<<' '<<y<<'\n';
                        std::istringstream source(text.str());script=nova::input::Script::parse(source);
                    }
                    script.push(
                        menu_ack_enabled?0:gate, SDL_GetWindowID(window),
                        std::chrono::duration<double, std::milli>(Clock::now() - start).count());
                    auto audio = nova::audio::statistics();
                    std::printf("input-audio frame=%llu players=%llu submitted_bytes=%llu\n", gate,
                                audio.players, audio.submitted_bytes);
                }
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    bool native_only = false;
                    bool script_source = nova::input::Script::unwrap(event,&native_only);
                    if(menu_ack_enabled && script_source) {
                        const auto expected=ack_events==0?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
                        if(end_requested || !ack_pending || gate==no_gate || ack_events>=2 || event.type!=expected ||
                           event.button.button!=SDL_BUTTON_LEFT || event.button.windowID!=SDL_GetWindowID(window))
                            throw std::runtime_error("menu-ack unexpected synthetic event");
                        input.handle(event,completed_frames.load());++ack_events;continue;
                    }
                    if(menu_ack_enabled && ack_pending && event.type!=SDL_EVENT_QUIT && event.type!=SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                        continue; // Freeze viewport and ownership while consuming the pair.
                    if (!nova::input::permit_input(scripted_session,allow_physical_input,script_source,event.type))
                        continue;
                    if (gamepad.handle(event,gamepad_context())) continue;
                    if (nova::input::input_event(event.type) && SDL_GetWindowFromEvent(&event) != window)
                        continue;
                    if((event.type==SDL_EVENT_WINDOW_FOCUS_LOST || event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) &&
                       event.window.windowID!=SDL_GetWindowID(window)) continue;
                    input_trace.dispatch(event,completed_frames.load(),script_source,controls.active());
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                        focused = false; controls.context(false,input.keyboard_visible());
                    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                        focused = true; controls.context(true,input.keyboard_visible());
                    }
                    if (event.type == SDL_EVENT_QUIT ||
                        event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                        end_requested = true;
                    gamepad.context(gamepad_context());
                    if((event.type==SDL_EVENT_WINDOW_MINIMIZED || event.type==SDL_EVENT_WINDOW_HIDDEN) &&
                       event.window.windowID==SDL_GetWindowID(window)) gamepad.suspend();
                    if (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED && !event.adevice.recording)
                        nova::audio::device_removed(event.adevice.which);
                    if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED && !event.adevice.recording)
                        nova::audio::device_added(event.adevice.which);
                    const auto window_route=window_modes.route(event);
                    if(window_route.consume && (window_route.changed || !window_route.error.empty())) persist_display(window_route);
                    if(window_route.release_owned) {
                        gamepad.suspend();
                        controls.clear(); input.release_all(); input.invalidate_keyboard();
                    }
                    if(window_route.release_owned && !window_route.consume && overlay_enabled)
                        panel.ui_window_transition();
                    if(window_route.invalidate_draw && overlay_enabled) panel.invalidate_draw();
                    if(!window_route.error.empty()) std::fprintf(stderr,"window mode: %s\n",window_route.error.c_str());
                    if(event.type>=SDL_EVENT_WINDOW_FIRST && event.type<=SDL_EVENT_WINDOW_LAST &&
                       event.window.windowID==SDL_GetWindowID(window)) {
                        window_modes.refresh();
                        input.window_viewport(nova::platform::window_presentation(window_surface.snapshot(),guest_extent.width,guest_extent.height).logical);
                    }
                    if(overlay_enabled) {
                        const auto route=panel_owner.route(event,panel.ui_binding_capture());
                        ownership(route);
                        if(route.panel_event && !window_route.consume) panel.ui_event(event);
                        if(route.consume || window_route.consume) continue;
                    }
                    if(window_route.consume) continue;
                    if (native_only && !controls.active()) {
                        std::printf("input-native-rejected frame=%llu reason=inactive\n",completed_frames.load());
                        continue;
                    }
                    if(!synchronize_render_input()) continue;
                    if (ready && !end_requested && !controls.handle(event))
                        input.handle(event, completed_frames.load());
                }
                if(menu_ack_enabled && gate!=no_gate) {
                    const auto after=input.diagnostic_touch_receipt();
                    if(!nova::diagnostic::t09h::balanced_pair(ack_before,after,ack_events) || end_requested)
                        throw std::runtime_error("menu-ack partial pair or release failure");
                    ack_pair_complete=true;
                    std::printf("menu-ack pair frame=%llu events=2 down=1 up=1 held=0\n",gate);
                }
                window_modes.refresh();
                const auto current_flags=SDL_GetWindowFlags(window);
                focused=SDL_GetKeyboardFocus()==window && (current_flags&SDL_WINDOW_INPUT_FOCUS) &&
                    !(current_flags&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED));
                controls.context(focused,input.keyboard_visible());
                input.window_viewport(nova::platform::window_presentation(window_surface.snapshot(),guest_extent.width,guest_extent.height).logical);
                window_modes.events_drained();
                if(!panel_owner.visible() || !focused || end_requested) render_settings.decide(false);
                if(overlay_enabled) panel_owner.events_drained();
                if(overlay_enabled && !end_requested) {
                    int width=0,height=0;
                    if(!SDL_GetWindowSize(window,&width,&height)) throw std::runtime_error(SDL_GetError());
                    const auto now=Clock::now();
                    nova::platform::FovUiFrame frame;
                    frame.width=static_cast<float>(width); frame.height=static_cast<float>(height);
                    const auto surface=window_surface.snapshot();
                    frame.guest_aspect=guest_extent.aspect();
                    if(width>0 && height>0) {
                        frame.framebuffer_scale_x=static_cast<float>(surface.pixel_width)/width;
                        frame.framebuffer_scale_y=static_cast<float>(surface.pixel_height)/height;
                    }
                    frame.delta_seconds=std::chrono::duration<float>(now-ui_previous).count(); ui_previous=now;
                    frame.dpi_scale=surface.display_scale;
                    frame.focused=focused; frame.minimized=surface.minimized;
                    frame.running_gameplay=controls.gameplay.load();
                    frame.optical=world_fov.live_status(); frame.last_save=last_save;
                    frame.movement_wobble_error=movement_save_error;
                    frame.ui_scale_error=ui_scale_error;
                    frame.graphics_quality_error=graphics_quality_error;
                    frame.preview_error=preview_error;
                    const auto render_state=render_settings.snapshot();
                    frame.render_extent=render_state.selected; frame.active_render_extent=render_state.active;
                    frame.render_available=render_state.available; frame.render_busy=render_state.busy;
                    frame.render_unsaved=render_state.unsaved; frame.render_error=render_state.error;
                    frame.render_confirming=render_state.confirming;
                    frame.render_seconds_remaining=render_state.seconds_remaining;
                    frame.display=window_modes.settings();
                    frame.display_choices=window_modes.choices(frame.display.fullscreen);
                    std::vector<nova::platform::GuestExtent> display_modes;
                    for(const auto &mode:frame.display_choices)
                        if(mode.resolution.width>0 && mode.resolution.height>0)
                            display_modes.push_back({mode.resolution.width,mode.resolution.height});
                    const auto desktop=window_modes.desktop_resolution();
                    frame.render_choices=nova::platform::render_catalogue(render_state.limits,display_modes,
                                                                          {desktop.width,desktop.height});
                    frame.display_error=display_error;
                    frame.weapon_position_error=weapon_position_error;
                    frame.controls_error=controls_error;
                    const auto command=panel.ui_frame(frame);
                    nova::platform::dispatch_fov_commands(world_fov,command,config_path,last_save,preview_error);
                    if(command.ui_scale) {
                        ui_scale.set(*command.ui_scale);
                        if(command.save_ui_scale) ui_scale_error=nova::platform::save_ui_scale_config(config_path,*command.ui_scale).error;
                    }
                    if(command.graphics_quality)
                        graphics_quality_error=nova::platform::save_graphics_quality_config(config_path,*command.graphics_quality).error;
                    if(nova::platform::dispatch_movement_wobble_request(movement_options,command)) {
                        const auto saved=nova::platform::save_movement_wobble_config(config_path,*command.movement_wobble);
                        movement_save_error=saved.error;
                    }
                    if constexpr(nova::weapon_placement::kPositionControlAvailable) {
                        if(command.weapon_position) {
                            // Host preference only. No renderer adapter or live effect is installed.
                            settings.weapon_position=*command.weapon_position;
                            weapon_position_error=nova::platform::save_weapon_position_config(
                                config_path,settings.weapon_position).error;
                        }
                    }
                    if(command.bindings) {
                        // UI owns map replacement; native C04 held desire is
                        // released by the existing overlay gate, never toggled.
                        controls.replace_bindings(*command.bindings);
                        bindings_unsaved=true;
                    }
                    if(command.sensitivity) {
                        controls.replace_sensitivity(*command.sensitivity);
                        sensitivity_unsaved=true;
                    }
                    if(command.bindings || command.sensitivity || command.retry_bindings_save) {
                        const auto saved=nova::platform::save_controls_preferences(controls_path,controls.snapshot_config(),
                            bindings_unsaved,sensitivity_unsaved);
                        controls_error=saved.error; // Explicit retry only; no idle writes.
                        if(saved.saved) bindings_unsaved=sensitivity_unsaved=false;
                    }
                    if(command.render_extent) {
                        render_settings.select(*command.render_extent);
                    }
                    if(command.apply_render) {
                        controls.clear(); input.release_all(); input.invalidate_keyboard();
                        render_settings.request();
                    }
                    if(command.keep_render) render_settings.decide(true);
                    if(command.revert_render || command.closed) render_settings.decide(false);
                    if(command.display) {
                        const auto applied=window_modes.apply(*command.display);
                        persist_display(applied);
                        input.window_viewport(nova::platform::window_presentation(window_surface.snapshot(),guest_extent.width,guest_extent.height).logical);
                    }
                    if(command.closed) ownership(panel_owner.set_visible(false));
                    const bool actual_relative=effects_arbiter.apply(window,panel_owner.visible(),focused,controls.gameplay.load() && !controls.communication_panel(),
                                          input.keyboard_visible(),guest_text);
                    panel_owner.relative_applied(actual_relative);
                    window_modes.input_applied();
                } else if(!overlay_enabled && !end_requested) {
                    // Script mode acknowledges the logical capture policy only;
                    // it never takes OS cursor ownership.
                    window_modes.input_applied();
                }
                gamepad.poll(gamepad_context());
            } catch (const std::exception &error) {
                input_error = error.what();
                end_requested = true;
                if(menu_ack_enabled) {
                    ack_failed=true;ack_pair_complete=false;
                    // GL is still parked: retire only our already-owned contact before unpark.
                    try { input.release_all(); } catch(const std::exception& cleanup) {
                        input_error+="; menu-ack cleanup: ";input_error+=cleanup.what();
                    }
                }
            }
            if(end_requested) gamepad.shutdown();
            if (gate != no_gate) {
                {
                    std::lock_guard lock(gate_mutex);
                    input_gate = no_gate;
                }
                gate_cv.notify_one();
            }
            if(end_requested && overlay_enabled) {
                try {
                    effects_arbiter.apply(window,false,false,false,input.keyboard_visible(),guest_text);
                    panel_owner.relative_applied(false);
                } catch(const std::exception &error) {
                    if(input_error.empty()) input_error=error.what();
                }
            }
            if (end_requested && ready && !pause_sent && !render_settings.snapshot().busy) {
                {
                    std::lock_guard lock(gate_mutex);
                    input_gate = no_gate;
                }
                gate_cv.notify_one();
                // Android dispatches GamePause on its UI thread while the GL thread still runs.
                pause_sent = true;
                input.release_all();
                auto klass = runtime.find_class(lib);
                runtime.native(std::string(prefix) + "GamePause", klass);
                runtime.release(klass, 1);
                stop = true;
                gate_cv.notify_all();
            }
            SDL_Delay(1);
        }
        gamepad.shutdown();
        render.join();
        movement.shutdown(); // Guest quiescent; finish() bypasses C++ destructors.
        menu_observer.report(stdout, true);
        nova::ui::MenuTextTrace::report(stdout);
        if(overlay_enabled) {
            effects_arbiter.apply(window,false,false,false,input.keyboard_visible(),guest_text);
            panel.ui_shutdown();
        }
        // Release a real session's cursor explicitly before process shutdown.
        // Script sessions have never entered OS relative mode.
        if (!scripted_session && captured &&
            !SDL_SetWindowRelativeMouseMode(window,false) && input_error.empty())
            input_error = SDL_GetError();
        controls.clear();
        nova::audio::shutdown();
        if(menu_ack_enabled && (!ack_observed || ack_failed) && input_error.empty()) input_error="menu-ack diagnostic stopped without relevant draw";
        if (!input_error.empty()) {
            std::fprintf(stderr, "input failure: %s\n", input_error.c_str());
            finish(1);
        }
        if (!render_error.empty()) {
            std::fprintf(stderr, "render failure: %s\n", render_error.c_str());
            finish(1);
        }
        std::printf("frame loop stopped; GamePause=%d\n", pause_sent ? 1 : 0);
        finish(0);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "nova3pc: %s\n", error.what());
        finish(1);
    }
}
