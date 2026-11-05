#include "platform/fov_panel.h"
#include "platform/angle.h"
#include "platform/window_modes.h"
#include "platform/profile.h"
#include "input/controls.h"
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "fixtures/world_fov_contract.h"
#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>
#include <future>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <utility>
using namespace nova::platform;
namespace {
void check(bool ok,const char *message) { if(!ok) { std::fprintf(stderr,"check failed: %s\n",message); throw std::runtime_error(message); } }
template<class T=uint32_t> T &field(uint32_t p,uint32_t offset=0) { return *reinterpret_cast<T *>(p+offset); }
uint32_t address(auto &object) { return reinterpret_cast<uint32_t>(&object); }
void __cdecl fixture_audit(uint32_t) {}
void __cdecl fixture_reset(uint32_t) {}
struct FirstWeaponGeometry { std::array<float,4> values{}; bool captured=false; };
thread_local FirstWeaponGeometry *first_weapon_geometry=nullptr;
void __cdecl fixture_weapon(uint32_t depth,uint32_t phase,uint32_t p) {
    // Existing native fixture phase 1 is after renderInternal and BEFORE every
    // lazy getter. Capture the full near-ray/far-box companion at that boundary.
    if(first_weapon_geometry && depth==0 && phase==1) {
        for(size_t i=0;i<4;++i) first_weapon_geometry->values[i]=field<float>(p,0x13c+uint32_t(i)*4);
        first_weapon_geometry->captured=true;
    }
}
// This loads the existing original synthetic ELF, installs the production
// adapter and observes its actual native CPU/geometry/driver sinks.
struct NativeComposition {
    nova::ImportRegistry imports;
    std::unique_ptr<nova::bionic::CxaRegistry> cxa;
    std::unique_ptr<nova::ElfImage> image;
    std::unique_ptr<nova::camera::NativeWorldFov> runtime;
    WorldFovContract *scene=nullptr;
    uint64_t frame=1;
    bool omit_service=false;
    NativeComposition(const std::filesystem::path &fixture,bool skip_service):omit_service(skip_service) {
        nova::bionic::register_boot(imports);
        imports.function("fov_audit",reinterpret_cast<void *>(&fixture_audit));
        imports.function("fov_reset_callback",reinterpret_cast<void *>(&fixture_reset));
        imports.function("fov_weapon_callback",reinterpret_cast<void *>(&fixture_weapon));
        cxa=std::make_unique<nova::bionic::CxaRegistry>(imports);
        image=std::make_unique<nova::ElfImage>(fixture,imports);
        scene=reinterpret_cast<WorldFovContract *>(image->symbol("fov_scene"));
        check(scene!=nullptr,"existing native optics fixture scene");
        std::vector<nova::camera::LensCallSite> sites;
        using L=nova::camera::LensSite;
        for(auto [name,kind]:std::initializer_list<std::pair<const char *,L>>{
            {"pseudo",L::pseudo_write},{"raw",L::raw},{"weapon_begin",L::weapon_begin},{"save",L::jeep_save},
            {"enable",L::jeep_enable},{"update",L::jeep_update},{"helper",L::jeep_helper},
            {"jeep_write",L::jeep_write},{"weapon_write",L::weapon_write},{"weapon_restore",L::weapon_restore},
            {"diff",L::weapon_diff},{"level_first",L::level_first},{"level_accumulate",L::level_accumulate},
            {"render_read",L::render_read},{"render_write",L::additive_write},{"fast",L::fast_node}}) {
            const std::string owner="fov_site_"+std::string(name);
            const auto start=image->symbol(owner),call=image->symbol(owner+"_call");
            check(start && call>=start,"actual synthetic native producer CALL");
            const uint32_t size=(kind==L::fast_node || kind==L::weapon_diff) ? 5 : 6;
            const auto *bytes=reinterpret_cast<const uint8_t *>(call);
            sites.push_back({owner,call-start,kind,std::vector<uint8_t>(bytes,bytes+size)});
        }
        runtime=std::make_unique<nova::camera::NativeWorldFov>(*image,std::nullopt,&sites,nullptr,std::nullopt,true);
    }
    uint32_t fn(const char *name) const { return nova::abi::require_function(*image,name); }
    void initialize() {
        runtime->bind_render_thread(); std::memset(scene,0,sizeof(*scene));
        nova::abi::invoke(fn("fov_fixture_setup"));
        nova::abi::invoke(fn("fov_fixture_pseudo"),{address(scene->nodes[0]),std::bit_cast<uint32_t>(70.f)});
        nova::abi::invoke(fn("_ZN6glitch5scene16CCameraSceneNode19recalculateMatricesEv"),{address(scene->nodes[0])});
        scene->diff_sample=field<float>(address(scene->weapon_component),0x298);
        check(!runtime->live_status().requested.world && !runtime->live_status().requested.weapon,
              "composition starts prepared Native/Native");
    }
    void observe(const nova::camera::NativeWorldFov::Options &expected) {
        using namespace nova::camera;
        constexpr auto pi=std::numbers::pi;
        const auto p=address(scene->nodes[0]); const auto raw=field<uint32_t>(p,0x128);
        const bool unchanged_native=!expected.world && !expected.weapon && runtime->live_status().committed==expected;
        const auto setups=scene->camera_setups;
        if(!omit_service) runtime->service_options(frame++);
        auto close_enough=[](double a,double b) { return std::isfinite(a) && std::abs(a-b)<.00003; };
        const double world=expected.world ? 2*std::atan(std::tan(*expected.world*pi/360)/reference_aspect) :
            70/reference_aspect*pi/180;
        const double x=1/std::tan(world/2)/reference_aspect;
        // First direct sinks, before another setter, lazy getter or weapon pass.
        check(close_enough(field<float>(p,0x200),x),"widget->production dispatch->safe service first world CPU projection");
        check(close_enough(field<float>(p,0x13c),1/x) && close_enough(field<float>(p,0x144),100/x),
              "widget command reaches full native ray/far geometry before getter");
        if(unchanged_native) {
            check(scene->camera_setups==setups,"unchanged Native service adds no blanket native setup");
            // No new optics transaction exists: the next ordinary native world
            // render publishes before draw, after the preceding weapon pass.
            // Changed/Custom pairs still require the service's FIRST driver sink.
            nova::abi::invoke(fn("_ZN6glitch5scene16CCameraSceneNode14renderInternalEPv"),{p,0});
        }
        check(close_enough(scene->driver_projection[0],x) && close_enough(scene->driver_view[12],7),
              "widget command reaches first native DRIVER projection and view");
        FirstWeaponGeometry first;
        first_weapon_geometry=&first;
        try { nova::abi::invoke(fn("_ZN17CNovaSceneManager11drawWeaponsEv"),{address(scene->manager)}); }
        catch(...) { first_weapon_geometry=nullptr; throw; }
        first_weapon_geometry=nullptr;
        const double native_pass=70/reference_aspect*pi/180+scene->diff_sample;
        const double extent=reference_aspect*std::tan((90/reference_aspect)*pi/360);
        const double weapon=expected.weapon ? 2*std::atan(std::tan(*expected.weapon*pi/360)/extent*std::tan(native_pass/2)) : native_pass;
        const double wx=1/std::tan(weapon/2)/reference_aspect;
        check(close_enough(scene->first_cpu_x[0],wx) && close_enough(scene->first_frustum[0],1/wx),
              "widget command reaches first installed weapon CPU and full geometry");
        const double wy=1/std::tan(weapon/2);
        check(first.captured && close_enough(first.values[0],1/wx) && close_enough(first.values[1],1/wy) &&
              close_enough(first.values[2],100/wx) && close_enough(first.values[3],100/wy),
              "first weapon near rays and far box are complete BEFORE every lazy getter");
        check(close_enough(scene->weapon_gpu_x[0],wx) && close_enough(scene->weapon_gpu_view[0],7),
              "widget command reaches first installed weapon DRIVER projection and view");
        check(field<uint32_t>(p,0x128)==raw,"composition preserves original native raw lens bits");
        if(!omit_service) runtime->service_options(frame++);
        const auto status=runtime->live_status();
        check(status.requested==expected && status.committed==expected,"composition exact requested and committed pair");
        check(status.application==NativeWorldFov::Application::applied,"composition observes native application, not just commit");
    }
};
struct Spy {
    SDL_Window *window=reinterpret_cast<SDL_Window *>(uintptr_t(0x1000));
    unsigned created=0,destroyed=0,captures=0,relative_calls=0,starts=0,stops=0,shapes=0,shows=0,hides=0;
    bool relative=false,captured=false,text=false;
    bool grabbed=false,has_focus=true,ignore_relative=false,ignore_grab=false;
    SDL_WindowFlags flags=SDL_WINDOW_INPUT_FOCUS;
};
thread_local Spy *spy=nullptr;
SDL_Cursor *SDLCALL create_cursor(SDL_SystemCursor id) { ++spy->created; return reinterpret_cast<SDL_Cursor *>(uintptr_t(id)+1); }
void SDLCALL destroy_cursor(SDL_Cursor *) { ++spy->destroyed; }
bool SDLCALL capture(bool enabled) { ++spy->captures; spy->captured=enabled; return true; }
bool SDLCALL cursor(SDL_Cursor *) { ++spy->shapes; return true; }
bool SDLCALL show() { ++spy->shows; return true; }
bool SDLCALL hide() { ++spy->hides; return true; }
bool SDLCALL relative(SDL_Window *,bool enabled) { ++spy->relative_calls; if(!spy->ignore_relative) spy->relative=enabled; return true; }
bool SDLCALL grab(SDL_Window *,bool enabled) { if(!spy->ignore_grab) spy->grabbed=enabled; return true; }
bool SDLCALL grab_state(SDL_Window *) { return spy->grabbed; }
SDL_WindowFlags SDLCALL window_flags(SDL_Window *) { return spy->flags; }
bool SDLCALL start(SDL_Window *) { ++spy->starts; spy->text=true; return true; }
bool SDLCALL stop(SDL_Window *) { ++spy->stops; spy->text=false; return true; }
bool SDLCALL area(SDL_Window *,const SDL_Rect *,int) { return true; }
SDL_MouseButtonFlags SDLCALL global(float *x,float *y) { *x=*y=0; return 0; }
SDL_Window *SDLCALL focus() { return spy->has_focus ? spy->window : nullptr; }
bool SDLCALL position(SDL_Window *,int *x,int *y) { *x=*y=0; return true; }
bool SDLCALL relative_state(SDL_Window *) { return spy->relative; }
char *SDLCALL clipboard() { return SDL_strdup("synthetic"); }
bool SDLCALL set_clipboard(const char *) { return true; }
BOOL WINAPI reject_write(HANDLE,LPCVOID,DWORD,LPDWORD written,LPOVERLAPPED) {
    *written=0; SetLastError(ERROR_WRITE_FAULT); return FALSE;
}
PanelSdlApi fake_api() {
    return {create_cursor,destroy_cursor,capture,cursor,show,hide,relative,start,stop,area,
            global,focus,position,relative_state,clipboard,set_clipboard,grab,grab_state,window_flags};
}
bool SDLCALL synthetic_desktop(SDL_Window *,const SDL_DisplayMode *mode) {
    check(!mode,"Panel transition regression never requests exclusive fullscreen");
    check(!spy->grabbed && !spy->relative && !spy->captured,"Mode mutation follows observed cursor release");
    return true;
}
bool SDLCALL synthetic_deny_fullscreen(SDL_Window *,bool) { return false; }
SDL_Event key(Uint32 type,SDL_Keycode keycode) { SDL_Event e{}; e.type=type; e.key.key=keycode; return e; }
void policy() {
    PanelInputOwner owner;
    nova::input::Controls controls(nova::input::ControlsConfig{}); controls.gameplay=true;
    auto dispatch=[&](const SDL_Event &event) {
        const auto route=owner.route(event);
        if(route.visibility_changed) controls.overlay(owner.visible());
        if(route.release_owned) controls.clear();
        if(!route.consume) controls.handle(event);
        return route;
    };
    dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_W)); dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_LSHIFT));
    SDL_Event button{}; button.type=SDL_EVENT_MOUSE_BUTTON_DOWN; button.button.button=SDL_BUTTON_LEFT;
    dispatch(button); button.button.button=SDL_BUTTON_RIGHT; dispatch(button);
    check(controls.take().down(nova::input::Action::fire),"initial held fire");
    check(dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_INSERT)).consume && owner.visible() && !controls.active(),"exclusive Insert open");
    SDL_Event panel_motion{}; panel_motion.type=SDL_EVENT_MOUSE_MOTION; panel_motion.motion.xrel=3;
    check(dispatch(panel_motion).panel_event,"game motion quarantine preserves panel drag motion");
    controls.gameplay=true; check(!controls.active(),"native refresh cannot erase overlay gate");
    auto repeat=key(SDL_EVENT_KEY_DOWN,SDLK_INSERT); repeat.key.repeat=true;
    dispatch(repeat); check(owner.visible(),"Insert repeat ignored");
    dispatch(key(SDL_EVENT_KEY_UP,SDLK_INSERT));
    check(dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_ESCAPE)).consume && !owner.visible(),"Escape closes without native Back");
    check(dispatch(key(SDL_EVENT_KEY_UP,SDLK_ESCAPE)).consume,"Escape release remains owned");
    controls.clear(); controls.context(false,false); controls.context(true,false);
    check(dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_W)).consume,"held key quarantined after clear/close/focus");
    check(dispatch(key(SDL_EVENT_KEY_UP,SDLK_W)).consume,"quarantined key release consumed");
    check(!dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_W)).consume && controls.take().down(nova::input::Action::forward),"fresh key rearms");
    SDL_Event motion{}; motion.type=SDL_EVENT_MOUSE_MOTION; motion.motion.xrel=123;
    check(dispatch(motion).consume && dispatch(motion).consume,"all stale close motion suppressed");
    owner.relative_applied(true);
    check(dispatch(motion).consume && dispatch(motion).consume,"motion still suppressed until full queue drain");
    owner.events_drained();
    check(!dispatch(motion).consume && controls.take().dx==123,"fresh post-policy motion rearms");
    SDL_Event lost{}; lost.type=SDL_EVENT_WINDOW_FOCUS_LOST; dispatch(lost);
    owner.relative_applied(false);
    lost.type=SDL_EVENT_WINDOW_FOCUS_GAINED; dispatch(lost);
    button.button.button=SDL_BUTTON_MIDDLE;
    check(dispatch(button).consume,"activation click consumed");
    button.type=SDL_EVENT_MOUSE_BUTTON_UP; check(dispatch(button).consume,"activation matching release consumed");
    check(dispatch(motion).consume && dispatch(motion).consume,"focus-regain multi-motion quarantine");
    owner.relative_applied(true); owner.events_drained(); check(!dispatch(motion).consume,"focus motion rearms after policy and drain");
    check(!dispatch(key(SDL_EVENT_KEY_DOWN,SDLK_ESCAPE)).consume,"closed-panel native Escape preserved");
    owner.set_visible(true); owner.relative_applied(false); owner.set_visible(false);
    check(dispatch(motion).consume,"absolute menu motion suppressed before policy acknowledgement");
    owner.relative_applied(false); owner.events_drained();
    check(!dispatch(motion).consume,"absolute menu motion rearms after policy acknowledgement and drain");
    Spy effects; spy=&effects;
    PanelBackendEffects backend; backend.api=fake_api(); backend.synthetic=true; backend.bind();
    unsigned guest_suspends=0,guest_restores=0,forced=0;
    struct Guest { unsigned *s,*r,*f; } guest{&guest_suspends,&guest_restores,&forced};
    GuestTextSync text{&guest,[](void *p) { ++*static_cast<Guest *>(p)->s; },
        [](void *p,bool force) { auto &g=*static_cast<Guest *>(p); ++*g.r; if(force) ++*g.f; }};
    PanelEffectsArbiter arbiter(backend);
    arbiter.apply(effects.window,false,true,true,true,text);
    backend.wants_text=true; backend.capture=true;
    arbiter.apply(effects.window,true,true,true,true,text);
    check(effects.text && !effects.relative && effects.captured && guest_suspends==1,"overlay numeric editor owns text and cursor");
    arbiter.apply(effects.window,true,false,true,true,text);
    check(!effects.text && !effects.captured,"focus loss stops overlay text/capture");
    backend.clear_requests(); arbiter.apply(effects.window,false,false,true,true,text);
    arbiter.apply(effects.window,false,true,true,true,text);
    check(forced>0 && guest_restores>0 && !effects.relative,"unchanged guest revision explicitly restored after close/focus");
    arbiter.apply(effects.window,false,true,true,false,text);
    check(effects.relative && effects.grabbed,"focused gameplay acknowledges raw look and independent window confinement");
    check(!arbiter.apply(effects.window,false,true,false,false,text) && !effects.grabbed && !effects.captured,
          "leaving gameplay releases native menu cursor confinement");
    check(arbiter.apply(effects.window,false,true,true,false,text) && effects.grabbed,
          "returning to gameplay reacquires confinement");
    check(!arbiter.apply(effects.window,true,true,true,false,text) && !effects.grabbed,
          "settings panel releases cursor confinement during gameplay");
    check(!arbiter.apply(effects.window,false,true,true,true,text) && !effects.grabbed,
          "guest text entry releases cursor confinement during gameplay");
    effects.has_focus=false;
    check(!arbiter.apply(effects.window,true,true,true,false,text) && !effects.grabbed && !effects.captured,
          "actual focus loss overrides stale focus intent and releases confinement for Alt+Tab");
    effects.has_focus=true;
    for(auto flag:{SDL_WINDOW_MINIMIZED,SDL_WINDOW_HIDDEN}) {
        effects.flags=SDL_WINDOW_INPUT_FOCUS|flag;
        check(!arbiter.apply(effects.window,false,true,true,false,text) && !effects.grabbed,
              "hidden and minimized windows cannot retain cursor confinement");
    }
    effects.flags=SDL_WINDOW_INPUT_FOCUS;
    effects.ignore_grab=true; bool ignored_grab=false;
    try { arbiter.apply(effects.window,false,true,true,false,text); } catch(const std::runtime_error &) { ignored_grab=true; }
    check(ignored_grab,"SDL success without observed grab cannot acknowledge input ownership");
    effects.ignore_grab=false; effects.relative=false; effects.ignore_relative=true; bool ignored_relative=false;
    try { arbiter.apply(effects.window,false,true,true,false,text); } catch(const std::runtime_error &) { ignored_relative=true; }
    check(ignored_relative,"SDL success without observed relative mode cannot rearm look motion");
    effects.ignore_relative=false;
    arbiter.apply(effects.window,false,false,false,false,text);
    check(!effects.grabbed && !effects.relative && !effects.captured,"close releases all cursor ownership");
    backend.unbind();
    bool rejected=false;
    PanelBackendEffects unsafe; unsafe.synthetic=true;
    try { unsafe.bind(); } catch(const std::logic_error &) { rejected=true; }
    check(rejected,"synthetic backend refuses un-intercepted SDL effects");
    std::puts("panel policy: exclusive pairs, held rearm, queue-drain motion and text/cursor spies passed");
}
std::vector<GLint> gl_state() {
    std::vector<GLint> values;
    for(auto name : {GL_CURRENT_PROGRAM,GL_ACTIVE_TEXTURE,GL_ARRAY_BUFFER_BINDING,
            GL_ELEMENT_ARRAY_BUFFER_BINDING,GL_VERTEX_ARRAY_BINDING,GL_READ_FRAMEBUFFER_BINDING,
            GL_DRAW_FRAMEBUFFER_BINDING,GL_PIXEL_UNPACK_BUFFER_BINDING,GL_UNPACK_ALIGNMENT,
            GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_BLEND_SRC_RGB,
            GL_BLEND_DST_RGB,GL_BLEND_SRC_ALPHA,GL_BLEND_DST_ALPHA,GL_BLEND_EQUATION_RGB,
            GL_BLEND_EQUATION_ALPHA,GL_DEPTH_FUNC,GL_DEPTH_WRITEMASK}) {
        GLint value=0; glGetIntegerv(static_cast<GLenum>(name),&value); values.push_back(value);
    }
    for(auto name : {GL_VIEWPORT,GL_SCISSOR_BOX,GL_COLOR_WRITEMASK}) {
        std::array<GLint,4> value{}; glGetIntegerv(static_cast<GLenum>(name),value.data());
        values.insert(values.end(),value.begin(),value.end());
    }
    for(auto name : {GL_BLEND,GL_CULL_FACE,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,
            GL_PRIMITIVE_RESTART_FIXED_INDEX,GL_RASTERIZER_DISCARD,GL_SAMPLE_COVERAGE,GL_SAMPLE_ALPHA_TO_COVERAGE})
        values.push_back(glIsEnabled(static_cast<GLenum>(name)));
    GLint active=0; glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
    for(auto name : {GL_TEXTURE_BINDING_2D,GL_SAMPLER_BINDING}) { GLint value=0; glGetIntegerv(static_cast<GLenum>(name),&value); values.push_back(value); }
    glActiveTexture(static_cast<GLenum>(active)); return values;
}
struct StateObjects {
    GLuint unpack=0,array=0,element=0,vao=0,sampler=0;
    std::array<GLuint,2> fbo{},textures{};
    StateObjects() {
        glGenBuffers(1,&unpack); glGenBuffers(1,&array); glGenBuffers(1,&element);
        glGenVertexArrays(1,&vao); glGenSamplers(1,&sampler);
        glGenFramebuffers(2,fbo.data()); glGenTextures(2,textures.data());
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        for(size_t i=0;i<2;++i) {
            glBindTexture(GL_TEXTURE_2D,textures[i]); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER,fbo[i]); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
            check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"nondefault state FBO complete");
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack); glBufferData(GL_PIXEL_UNPACK_BUFFER,4096,nullptr,GL_STATIC_DRAW);
    }
    void apply() {
        glUseProgram(0); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,array); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,element);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,textures[0]); glBindSampler(0,sampler); glActiveTexture(GL_TEXTURE3);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo[0]); glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo[1]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
        glPixelStorei(GL_UNPACK_ALIGNMENT,8); glPixelStorei(GL_UNPACK_ROW_LENGTH,19);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,2); glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
        glViewport(7,9,31,33); glScissor(3,4,17,19); glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
        glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_DST_ALPHA,GL_ZERO); glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT,GL_FUNC_ADD);
        for(auto capability : {GL_BLEND,GL_CULL_FACE,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,
                GL_PRIMITIVE_RESTART_FIXED_INDEX,GL_RASTERIZER_DISCARD,GL_SAMPLE_COVERAGE,GL_SAMPLE_ALPHA_TO_COVERAGE})
            glEnable(static_cast<GLenum>(capability));
        glSampleCoverage(0,GL_TRUE); glDepthMask(GL_FALSE); glDepthFunc(GL_GREATER);
        check(glGetError()==GL_NO_ERROR,"nondefault state setup");
    }
    ~StateObjects() {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0); glBindVertexArray(0);
        glDeleteBuffers(1,&unpack); glDeleteBuffers(1,&array); glDeleteBuffers(1,&element);
        glDeleteVertexArrays(1,&vao); glDeleteSamplers(1,&sampler);
        glDeleteFramebuffers(2,fbo.data()); glDeleteTextures(2,textures.data());
    }
};
class GlWorker {
  public:
    GlWorker() :thread_([this] {
        try {
            nova::platform::Angle angle("d3d11"); angle.create(nullptr,1280,720,1);
            StateObjects state;
            { std::lock_guard lock(mutex_); ready_=true; } cv_.notify_all();
            for(;;) {
                std::function<void(Angle &,StateObjects &)> job;
                { std::unique_lock lock(mutex_); cv_.wait(lock,[&] { return stop_ || bool(job_); }); if(stop_) break; job=std::move(job_); }
                try { job(angle,state); }
                catch(const std::exception &error) { std::lock_guard lock(mutex_); error_=error.what(); }
                { std::lock_guard lock(mutex_); complete_=true; } cv_.notify_all();
            }
        } catch(const std::exception &error) {
            { std::lock_guard lock(mutex_); error_=error.what(); ready_=complete_=true; } cv_.notify_all();
        }
    }) { std::unique_lock lock(mutex_); cv_.wait(lock,[&] { return ready_; }); check(error_.empty(),error_.c_str()); }
    ~GlWorker() { { std::lock_guard lock(mutex_); stop_=true; } cv_.notify_all(); if(thread_.joinable()) thread_.join(); }
    void run(std::function<void(Angle &,StateObjects &)> job) {
        std::unique_lock lock(mutex_); check(error_.empty(),error_.c_str()); complete_=false; job_=std::move(job); cv_.notify_all();
        cv_.wait(lock,[&] { return complete_; });
        const auto error=std::exchange(error_,{});
        check(error.empty(),error.c_str());
    }
  private:
    std::mutex mutex_; std::condition_variable cv_; bool ready_=false,complete_=false,stop_=false;
    std::function<void(Angle &,StateObjects &)> job_; std::string error_; std::thread thread_;
};
void rendering(const std::filesystem::path &output,const std::filesystem::path &fixture,
               const std::string &negative={}) {
    NativeComposition optics(fixture,negative=="omit-service");
    check(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"),"dummy video"); check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    Spy effects; spy=&effects; effects.window=SDL_CreateWindow("Owned synthetic panel",1280,720,SDL_WINDOW_HIDDEN);
    check(effects.window!=nullptr,SDL_GetError());
    PanelBackendEffects backend; backend.api=fake_api(); backend.synthetic=true;
    FovPanel panel({}); panel.ui_initialize(effects.window,&backend);
    FovUiFrame frame; frame.running_gameplay=true;
    frame.render_choices=render_catalogue({2048,2048,2048,2048});
    nova::input::Controls controls(nova::input::ControlsConfig{}); controls.gameplay=true;
    PanelInputOwner owner; owner.set_visible(true); controls.overlay(true);
    check(!controls.active(),"composition keeps gameplay input exclusive to the open panel");
    WindowModes *window_router=nullptr;
    auto route_event=[&](const SDL_Event &event) {
        const auto window_route=window_router ? window_router->route(event) : WindowModeRoute{};
        const auto route=owner.route(event,panel.ui_binding_capture());
        check(route.consume,"composition panel input never reaches guest controls");
        if(route.panel_event && !window_route.consume) panel.ui_event(event);
        return window_route;
    };
    check(!panel.ui_frame(frame).changed && !panel.renderer_ready(),"first UI frame waits for GL prewarm");
    GlWorker gl;
    struct Cleanup {
        std::function<void()> fn;
        ~Cleanup() { if(fn) try { fn(); } catch(...) {} }
    } cleanup{[&] { gl.run([&](Angle &,StateObjects &) { panel.gl_shutdown(); }); panel.ui_shutdown(); }};
    gl.run([&](Angle &,StateObjects &state) {
        optics.initialize();
        state.apply(); auto before=gl_state(); bool failed=false;
        try { panel.gl_initialize(true); } catch(const std::runtime_error &) { failed=true; }
        check(failed && gl_state()==before && glGetError()==GL_NO_ERROR,"real prewarm failure restores full GL state");
        panel.gl_initialize(); check(gl_state()==before && glGetError()==GL_NO_ERROR,"real prewarm state/PBO/unpack restored");
    });
    panel.ui_visible(true);
    static_assert(static_cast<size_t>(FovControl::count)==4,"two optics sliders, bob and Reset");
    auto isolated_config=output; isolated_config+=".settings-test.conf";
    std::filesystem::create_directories(isolated_config.parent_path());
    std::filesystem::remove(isolated_config);
    unsigned persistence_calls=0;
    bool reject_persistence=false;
    FovConfigIo failed_io; failed_io.write=reject_write;
    auto build=[&] {
        auto command=panel.ui_frame(frame);
        if(command.save) ++persistence_calls;
        if(negative!="omit-dispatch")
            dispatch_fov_commands(*optics.runtime,command,isolated_config,frame.last_save,
                                  frame.preview_error,reject_persistence ? &failed_io : nullptr);
        return command;
    };
    auto observe=[&](const FovCommands &command) {
        gl.run([&](Angle &,StateObjects &) { optics.observe(command.visible); });
        frame.optical=optics.runtime->live_status();
    };
    auto command=build(); build();
    check(!command.visible.world && !command.visible.weapon && !command.save &&
          !std::filesystem::exists(isolated_config),"opening Default rows never activates values or writes config");
    check(panel.control_rect(FovControl::world_slider).y2<panel.control_rect(FovControl::weapon_slider).y1,
          "Gameplay contains two stacked slider rows");
    const auto defaults=*nova::camera::native_fov_defaults();
    auto numeric=panel.displayed_values();
    check(std::abs(numeric[0]-64.92080257f)<.0001f && std::abs(numeric[1]-80.11600919f)<.0001f,
          "Native initial labels and actual SliderFloat positions use canonical horizontal defaults");
    auto mouse=[&](Uint32 type,float x,float y) {
        SDL_Event event{}; event.type=type;
        if(type==SDL_EVENT_MOUSE_MOTION) { event.motion.windowID=SDL_GetWindowID(effects.window); event.motion.x=x; event.motion.y=y; }
        else { event.button.windowID=SDL_GetWindowID(effects.window); event.button.button=SDL_BUTTON_LEFT; event.button.x=x; event.button.y=y; }
        route_event(event);
    };
    auto click=[&](FovControl control) {
        auto rect=panel.control_rect(control); const float x=(rect.x1+rect.x2)/2,y=(rect.y1+rect.y2)/2;
        check(rect.x2>rect.x1 && rect.y2>rect.y1,"real widget rectangle");
        mouse(SDL_EVENT_MOUSE_MOTION,x,y); build(); mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,x,y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,x,y); return build();
    };
    auto drag=[&](FovControl control,float end=.5f) {
        const auto rect=panel.control_rect(control); const float y=(rect.y1+rect.y2)/2;
        const auto before=persistence_calls;
        mouse(SDL_EVENT_MOUSE_MOTION,rect.x1+8,y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,rect.x1+8,y); auto moving=build();
        check(!moving.save && persistence_calls==before,"slider press only previews");
        for(float portion : {.2f,.35f,end}) {
            mouse(SDL_EVENT_MOUSE_MOTION,rect.x1+(rect.x2-rect.x1)*portion,y); moving=build();
            check(!moving.save && persistence_calls==before,"drag frames never persist");
        }
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,rect.x1+(rect.x2-rect.x1)*end,y);
        auto completed=build();
        check(completed.save && persistence_calls==before+1,"completed slider edit persists exactly once");
        check(!build().save && persistence_calls==before+1,"idle frame never repeats persistence");
        return completed;
    };
    command=drag(FovControl::world_slider);
    check(command.visible.world && !command.visible.weapon,"FOV slider changes its own optional value");
    check(!optics.runtime->live_status().committed.world,"UI dispatch alone does not fake render service/application");
    observe(command);
    command=drag(FovControl::weapon_slider,1.f);
    check(command.visible.world && command.visible.weapon,"Weapon FOV slider independent from world");
    check(command.visible.weapon==104.f,"real weapon slider clamps at the104degree upper boundary");
    check(frame.last_save.saved && frame.last_save.visible_revision==command.visible_revision &&
          frame.optical.committed!=command.visible,"completed visible pair persists before optical acknowledgement");
    observe(command);
    std::filesystem::path obb;
    auto parsed=nova::profile::read_config(isolated_config,obb);
    check(parsed.world_fov==command.visible.world && parsed.weapon_fov==command.visible.weapon,"automatic persistence writes exact visible pair to isolated path");
    {
        // Denied injected mode requests exercise the production preparation and
        // quarantine path without changing even the dummy window's mode.
        WindowSurfaceMailbox mailbox; WindowModeApi mode_api;
        mode_api.desktop_mode=synthetic_desktop; mode_api.fullscreen=synthetic_deny_fullscreen;
        PanelEffectsArbiter transition_arbiter(backend);
        struct Transition { FovPanel *panel; PanelEffectsArbiter *arbiter; SDL_Window *window; };
        Transition transition_owner{&panel,&transition_arbiter,effects.window};
        WindowModes modes(effects.window,mailbox,mode_api,
                          {&transition_owner,[](void *p) {
                              auto &owner=*static_cast<Transition *>(p);
                              owner.panel->ui_window_transition(); owner.arbiter->release_pointer(owner.window);
                          }});
        window_router=&modes;
        const auto rect=panel.control_rect(FovControl::world_slider);
        const float y=(rect.y1+rect.y2)*.5f;
        mouse(SDL_EVENT_MOUSE_MOTION,rect.x1+12,y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,rect.x1+12,y); build();
        mouse(SDL_EVENT_MOUSE_MOTION,rect.x1+(rect.x2-rect.x1)*.4f,y); command=build();
        // SDL backend capture runs before ImGui consumes this frame's events;
        // one further held frame observes the drag distance just established.
        command=build();
        check(!command.save,"held transition-preparation frame does not finalize the drag");
        check(backend.capture,"active synthetic drag requests capture before mode transition");
        const auto edited=command.visible; const auto before=persistence_calls;
        effects.grabbed=effects.relative=effects.captured=true;
        SDL_Event shortcut{}; shortcut.type=SDL_EVENT_KEY_DOWN;
        shortcut.key.windowID=SDL_GetWindowID(effects.window); shortcut.key.key=SDLK_RETURN;
        shortcut.key.mod=SDL_KMOD_ALT;
        const auto transition=route_event(shortcut);
        check(transition.consume && transition.release_owned && !transition.error.empty(),
              "Alt+Enter routes through injected denied transition and releases panel input");
        command=build();
        check(command.save && persistence_calls==before+1 && command.visible==edited &&
              !backend.capture && !backend.wants_text && panel.visible(),
              "mode transition finalizes one valid edit and clears backend drag/text without closing panel");
        SDL_Event released{}; released.type=SDL_EVENT_MOUSE_BUTTON_UP;
        released.button.windowID=SDL_GetWindowID(effects.window); released.button.button=SDL_BUTTON_LEFT;
        check(route_event(released).consume,"held slider release is quarantined instead of reaching ImGui");
        shortcut.type=SDL_EVENT_KEY_UP; check(route_event(shortcut).consume,"shortcut release stays owned");
        modes.input_applied(); modes.events_drained();
        mouse(SDL_EVENT_MOUSE_MOTION,rect.x2-8,y); command=build();
        check(!command.changed && !command.save && command.visible==edited && !backend.capture,
              "post-transition motion cannot resume the old drag or reacquire capture after filtered up");
        window_router=nullptr;
        observe(command);
    }
    auto send_key=[&](Uint32 type,SDL_Keycode code,SDL_Keymod modifiers) {
        SDL_Event event{}; event.type=type; event.key.windowID=SDL_GetWindowID(effects.window);
        event.key.key=code; event.key.mod=modifiers;
        if(code==SDLK_LCTRL) event.key.scancode=SDL_SCANCODE_LCTRL;
        route_event(event); return build();
    };
    for(const char *value : {"999","10","nan","inf"}) {
        const auto rect=panel.control_rect(FovControl::world_slider);
        const float x=(rect.x1+rect.x2)*.5f,y=(rect.y1+rect.y2)*.5f;
        send_key(SDL_EVENT_KEY_DOWN,SDLK_LCTRL,SDL_KMOD_CTRL);
        mouse(SDL_EVENT_MOUSE_MOTION,x,y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,x,y); build(); mouse(SDL_EVENT_MOUSE_BUTTON_UP,x,y); build();
        send_key(SDL_EVENT_KEY_UP,SDLK_LCTRL,SDL_KMOD_NONE);
        check(backend.wants_text,"real Ctrl-click numeric editor requests intercepted IME");
        const auto before=persistence_calls;
        SDL_Event typed{}; typed.type=SDL_EVENT_TEXT_INPUT; typed.text.windowID=SDL_GetWindowID(effects.window); typed.text.text=value;
        route_event(typed); command=build();
        check(!command.save && persistence_calls==before,"numeric typing previews without file write");
        command=send_key(SDL_EVENT_KEY_DOWN,SDLK_RETURN,SDL_KMOD_NONE);
        send_key(SDL_EVENT_KEY_UP,SDLK_RETURN,SDL_KMOD_NONE);
        check(command.visible.world && std::isfinite(*command.visible.world) &&
              *command.visible.world>=60 && *command.visible.world<=120,
              "numeric invalid/range inputs retain accepted live values");
        check(persistence_calls<=before+1,"Enter persists a completed numeric edit at most once");
    }
    {
        const auto world=panel.control_rect(FovControl::world_slider);
        const float x=(world.x1+world.x2)*.5f,y=(world.y1+world.y2)*.5f;
        send_key(SDL_EVENT_KEY_DOWN,SDLK_LCTRL,SDL_KMOD_CTRL);
        mouse(SDL_EVENT_MOUSE_MOTION,x,y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,x,y); build(); mouse(SDL_EVENT_MOUSE_BUTTON_UP,x,y); build();
        send_key(SDL_EVENT_KEY_UP,SDLK_LCTRL,SDL_KMOD_NONE);
        const auto before=persistence_calls;
        SDL_Event typed{}; typed.type=SDL_EVENT_TEXT_INPUT; typed.text.windowID=SDL_GetWindowID(effects.window); typed.text.text="101";
        route_event(typed); command=build();
        check(!command.save && persistence_calls==before,"active typed world edit does not save");
        const auto weapon=panel.control_rect(FovControl::weapon_slider); const float wy=(weapon.y1+weapon.y2)*.5f;
        mouse(SDL_EVENT_MOUSE_MOTION,weapon.x1+(weapon.x2-weapon.x1)*.25f,wy); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,weapon.x1+(weapon.x2-weapon.x1)*.25f,wy); command=build();
        check(!command.save && persistence_calls==before,"world completion cannot persist newly active weapon drag");
        frame.minimized=true; command=build();
        check(!command.save && persistence_calls==before,"minimized frame retains pending save while weapon edit is in progress");
        frame.minimized=false;
        gl.run([&](Angle &,StateObjects &) { panel.gl_presentable(false); }); command=build();
        check(!command.save && persistence_calls==before,"no-swap frame retains pending save while weapon edit is in progress");
        gl.run([&](Angle &,StateObjects &) { panel.gl_presentable(true); }); command=build();
        check(!command.save && persistence_calls==before,"resumed frame still waits for active weapon edit completion");
        for(float portion : {.35f,.55f,.7f}) {
            mouse(SDL_EVENT_MOUSE_MOTION,weapon.x1+(weapon.x2-weapon.x1)*portion,wy); command=build();
            check(!command.save && persistence_calls==before,"cross-row pending save waits through every weapon drag frame");
        }
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,weapon.x1+(weapon.x2-weapon.x1)*.7f,wy); command=build();
        check(command.save && persistence_calls==before+1 && command.visible.world==101.f,
              "cross-row completion persists one final pair");
        parsed=nova::profile::read_config(isolated_config,obb);
        check(parsed.world_fov==command.visible.world && parsed.weapon_fov==command.visible.weapon,
              "cross-row persisted pair contains completed values only");
    }
    {
        const auto rect=panel.control_rect(FovControl::world_slider); const float y=(rect.y1+rect.y2)*.5f;
        const auto before=persistence_calls;
        mouse(SDL_EVENT_MOUSE_MOTION,rect.x1+12,y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,rect.x1+12,y); command=build();
        check(!command.save && persistence_calls==before,"open drag remains unsaved before close finalization");
        panel.ui_visible(false); command=build();
        check(command.save && persistence_calls==before+1,"closing finalizes the last valid edit outside ImGui");
        check(!build().save && persistence_calls==before+1,"close finalization never repeats");
        panel.ui_visible(true); build(); build();
    }
    command=click(FovControl::reset);
    check(command.save && !command.visible.world && !command.visible.weapon && frame.last_save.saved,
          "Reset immediately persists both Native/default values");
    numeric=panel.displayed_values();
    check(numeric[0]==defaults.world && numeric[1]==defaults.weapon,"Reset restores numeric labels and actual thumb storage");
    observe(command);
    command=click(FovControl::reset); observe(command);
    check(panel.displayed_values()==numeric,"repeated Native Reset preserves both normal numeric positions");
    command=drag(FovControl::weapon_slider);
    check(!command.visible.world && command.visible.weapon,"weapon-only widget change starts from Native world");
    observe(command);
    command=click(FovControl::reset); observe(command);
    check(panel.displayed_values()==numeric,"Reset after another Custom edit restores both normal thumbs again");
    frame.width=1600; frame.height=720; build();
    check(panel.displayed_values()==numeric,"Letterboxed window aspect cannot change guest-native optical baseline");
    frame.guest_aspect=1600.0/720; build();
    const auto wide=*nova::camera::native_fov_defaults(1600.0/720);
    check(panel.displayed_values()==std::array<float,2>{wide.world,wide.weapon},"Native numeric baseline follows explicit guest target aspect without a guest query");
    frame.width=1280; frame.height=720; frame.guest_aspect=16.0/9.0; build();
    parsed=nova::profile::read_config(isolated_config,obb);
    check(!parsed.world_fov && !parsed.weapon_fov,"Reset file uses native optional semantics");
    reject_persistence=true; command=drag(FovControl::world_slider);
    check(!frame.last_save.saved && !frame.last_save.error.empty(),"actual automatic-save failure reaches minimal UI error line");
    parsed=nova::profile::read_config(isolated_config,obb);
    check(!parsed.world_fov && !parsed.weapon_fov,"failed automatic save preserves previous isolated file");
    reject_persistence=false; command=click(FovControl::reset); build(); observe(command);
    check(frame.last_save.saved && frame.last_save.error.empty(),"successful Reset clears persistence failure");
    const auto current=build().visible; frame.running_gameplay=false; frame.dpi_scale=1.5f;
    check(build().visible==current && !build().save,"paused host frame and DPI do not persist or change optics");
    frame.running_gameplay=true; frame.dpi_scale=1; build();
    std::vector<uint8_t> pixels;
    auto draw=[&](Angle &angle,StateObjects &state) {
        glBindFramebuffer(GL_FRAMEBUFFER,angle.main_target().framebuffer); glDisable(GL_SCISSOR_TEST);
        glDisable(GL_RASTERIZER_DISCARD); glDisable(GL_SAMPLE_COVERAGE); glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glClearColor(.125f,.25f,.375f,1); glClear(GL_COLOR_BUFFER_BIT);
        std::array<uint8_t,4> source{}; glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,source.data());
        check(source[0]==32 && source[1]==64 && source[2]==96,"synthetic game fixture produces background before inherited-state test");
        state.apply(); const auto before=gl_state();
        struct Capture { FovPanel *panel; std::vector<uint8_t> *pixels; } capture_data{&panel,&pixels};
        angle.present([](void *user,int w,int h) {
            auto &data=*static_cast<Capture *>(user); const auto original=gl_state(); data.panel->gl_draw(w,h);
            check(gl_state()==original,"panel draw preserves all inherited GLES state");
            GLint read=0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read); glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
            data.pixels->resize(size_t(w)*h*4); glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,data.pixels->data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(read));
        },&capture_data);
        check(gl_state()==before && glGetError()==GL_NO_ERROR,"post-blit presentation restores FBO/state");
    };
    gl.run(draw);
    unsigned changed=0;
    for(size_t i=0;i<pixels.size();i+=4) if(pixels[i]!=32 || pixels[i+1]!=64 || pixels[i+2]!=96) ++changed;
    std::filesystem::create_directories(output.parent_path());
    std::vector<uint8_t> top(pixels.size());
    for(size_t y=0;y<720;++y) std::copy_n(pixels.data()+(719-y)*1280*4,1280*4,top.data()+y*1280*4);
    nova::gles::write_png(output,1280,720,top);
    std::printf("panel pixels changed=%u outside=%u,%u,%u first=%u,%u,%u\n",changed,
        pixels[4*(1270+710*1280)],pixels[4*(1270+710*1280)+1],pixels[4*(1270+710*1280)+2],
        pixels[0],pixels[1],pixels[2]);
    check(changed>15000 && pixels[4*(1270+710*1280)]==32,"nonblank real linked panel and preserved outside game pixels");
    gl.run([&](Angle &,StateObjects &state) { state.apply(); auto before=gl_state(); panel.gl_recreate_resources(); check(gl_state()==before && glGetError()==GL_NO_ERROR,"lazy font/shader recreation restores PBO/unpack/FBO/color/enables"); });
    build(); gl.run(draw);
    gl.run([&](Angle &,StateObjects &) { panel.gl_presentable(false); });
    build(); gl.run(draw);
    check(pixels[4*(100+500*1280)]==32,"no-swap invalidates stale draw data");
    gl.run([&](Angle &,StateObjects &) { panel.gl_presentable(true); });
    build();
    auto concurrent=std::async(std::launch::async,[&] { for(int i=0;i<20;++i) gl.run(draw); });
    for(int i=0;i<60;++i) build();
    concurrent.get();
    gl.run([&](Angle &angle,StateObjects &state) {
        state.apply(); const auto original=gl_state(); bool failed=false;
        try { angle.present([](void *,int,int) { throw std::runtime_error("owned callback failure"); },nullptr); }
        catch(const std::runtime_error &) { failed=true; }
        check(failed && gl_state()==original,"presentation exception restores read/draw FBO and state");
        const auto primary=eglGetCurrentContext(); check(angle.make_current(1),"select existing extra context");
        const auto extra=eglGetCurrentContext(); const auto extra_state=gl_state();
        angle.present([](void *expected,int,int) { check(eglGetCurrentContext()==static_cast<EGLContext>(expected),"overlay draws only on primary EGL context"); },primary);
        check(eglGetCurrentContext()==extra && gl_state()==extra_state,"presentation restores guest extra context and its state");
        check(angle.make_current(0),"restore primary context after fixture");
    });
    frame.minimized=true; build(); gl.run(draw);
    check(pixels[4*(100+500*1280)]==32,"minimize invalidates draw data");
    frame.minimized=false; build();
    {
        auto display_click=[&](DisplayControl control) {
            const auto rect=panel.display_control_rect(control);
            check(rect.x2>rect.x1 && rect.y2>rect.y1,"Display widget has actual geometry");
            const auto x=(rect.x1+rect.x2)/2,y=(rect.y1+rect.y2)/2;
            mouse(SDL_EVENT_MOUSE_MOTION,x,y); build();
            mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,x,y); build();
            mouse(SDL_EVENT_MOUSE_BUTTON_UP,x,y); return build();
        };
        frame.display_choices={{{},"Current"},{{1280,720},"1280 x 720"}};
        const auto pending_before=persistence_calls;
        const auto world=panel.control_rect(FovControl::world_slider);
        const float wx=(world.x1+world.x2)*.5f,wy=(world.y1+world.y2)*.5f;
        send_key(SDL_EVENT_KEY_DOWN,SDLK_LCTRL,SDL_KMOD_CTRL);
        mouse(SDL_EVENT_MOUSE_MOTION,wx,wy); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,wx,wy); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,wx,wy); build();
        send_key(SDL_EVENT_KEY_UP,SDLK_LCTRL,SDL_KMOD_NONE);
        check(backend.wants_text,"Gameplay numeric editor is active before Display navigation");
        SDL_Event typed{}; typed.type=SDL_EVENT_TEXT_INPUT;
        typed.text.windowID=SDL_GetWindowID(effects.window); typed.text.text="101";
        route_event(typed); command=build();
        check(command.visible.world==101.f && !command.save && persistence_calls==pending_before,
              "Valid active numeric edit remains unsaved before tab completion");
        display_click(DisplayControl::display_tab); build();
        check(persistence_calls==pending_before+1,"Display tab completes and persists the old valid Gameplay edit once");
        parsed=nova::profile::read_config(isolated_config,obb);
        check(parsed.world_fov==101.f,"Tab-completed edit persists its actual visible value");
        check(!build().save && persistence_calls==pending_before+1,"Idle Display does not repeat tab-completion persistence");
        const auto before=persistence_calls;
        auto display_command=display_click(DisplayControl::fullscreen);
        check(display_command.display && display_command.display->fullscreen && !display_command.changed &&
              !display_command.save && persistence_calls==before,
              "Real Display checkbox emits one video command without optics or premature persistence");
        check(!build().display,"Idle Display frames never emit repeated requests");
        frame.display=*display_command.display;
        display_command=display_click(DisplayControl::fullscreen);
        check(display_command.display && !display_command.display->fullscreen,"Fullscreen toggle supports both directions");
        frame.display={};
        display_click(DisplayControl::resolution); build();
        display_command=display_click(DisplayControl::first_resolution);
        check(display_command.render_extent==GuestExtent{1600,900} && !display_command.display &&
              !display_command.save && persistence_calls==before,
              "Real resolution combo emits exact supported choice without saving before apply");
        frame.render_extent=*display_command.render_extent;
        check(!build().render_extent,"Idle render selection never repeats or resizes SDL output");
        check(!display_click(DisplayControl::apply).apply_render,"Apply refuses unavailable backend without saving or faking active extent");
        frame.render_available=true;
        display_command=display_click(DisplayControl::apply);
        check(display_command.apply_render && !display_command.render_extent && !display_command.save &&
              frame.active_render_extent==GuestExtent{},"Apply emits one command with active extent unchanged until ACK");
        frame.render_busy=true; build();
        check(!display_click(DisplayControl::apply).apply_render,"busy Apply never duplicates the request");
        frame.render_busy=false; frame.active_render_extent=frame.render_extent; build();
        check(!display_click(DisplayControl::apply).apply_render,"acknowledged active resolution disables idle Apply");
        frame.render_confirming=true; frame.render_seconds_remaining=15; build(); build();
        auto confirmation=display_click(DisplayControl::keep);
        check(confirmation.keep_render && !confirmation.apply_render && !confirmation.save,
              "Keep requests only resolution confirmation, never optical persistence or another preview");
        frame.render_busy=true; build();
        check(!display_click(DisplayControl::revert).revert_render,"in-flight confirmation cannot duplicate recovery");
        frame.render_busy=false; build();
        check(display_click(DisplayControl::revert).revert_render,"Revert emits explicit recovery");
        frame.render_confirming=false; build(); build();
        display_click(DisplayControl::resolution); build();
        display_command=display_click(DisplayControl::full_hd_resolution);
        check(display_command.render_extent==GuestExtent{1920,1080} && !display_command.apply_render && !display_command.save,
              "Full HD selection stages only and keeps the running scene until Apply");

        build(); build();
        auto scale_rect=panel.display_control_rect(DisplayControl::ui_scale);
        check(scale_rect.x2>scale_rect.x1 && scale_rect.y2>scale_rect.y1,"UI scale has visible slider geometry");
        const float scale_y=(scale_rect.y1+scale_rect.y2)*.5f;
        mouse(SDL_EVENT_MOUSE_MOTION,scale_rect.x1+8,scale_y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,scale_rect.x1+8,scale_y);
        auto ui_command=build();
        check(ui_command.ui_scale && *ui_command.ui_scale==50 && !ui_command.save_ui_scale,"UI drag previews50 without saving");
        mouse(SDL_EVENT_MOUSE_MOTION,scale_rect.x2-8,scale_y); ui_command=build();
        check(ui_command.ui_scale && *ui_command.ui_scale==300 && !ui_command.save_ui_scale,"UI preview reaches300 independently of resolution");
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,scale_rect.x2-8,scale_y); ui_command=build();
        check(ui_command.save_ui_scale && ui_command.ui_scale==300 && !ui_command.save && !ui_command.apply_render,"UI release saves once without changing FOV or resolution");
        check(!build().save_ui_scale && !build().ui_scale,"idle UI scale emits no repeated updates");
        ui_command=display_click(DisplayControl::ui_scale_reset);
        check(ui_command.ui_scale==100 && ui_command.save_ui_scale,"UI reset restores original100 percent");
        build(); gl.run(draw);
        for(size_t y=0;y<720;++y) std::copy_n(pixels.data()+(719-y)*1280*4,1280*4,top.data()+y*1280*4);
        nova::gles::write_png(output.parent_path()/(output.stem().string()+"-display.png"),1280,720,top);
        mouse(SDL_EVENT_MOUSE_MOTION,scale_rect.x1+8,scale_y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,scale_rect.x1+8,scale_y); build();
        panel.ui_visible(false); ui_command=build();
        check(ui_command.ui_scale==50 && ui_command.save_ui_scale && !build().save_ui_scale,"closing Settings completes pending UI scale once");
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,scale_rect.x1+8,scale_y); build();
        panel.ui_visible(true); build(); build();
        display_click(DisplayControl::ui_scale_reset);

        display_click(DisplayControl::graphics_tab);build();build();
        display_click(DisplayControl::lod_distance);build();
        auto quality_command=display_click(DisplayControl::lod_original);
        check(quality_command.graphics_quality==GraphicsQuality{1,2},"LOD choice preserves highest texture default");
        display_click(DisplayControl::texture_filtering);build();
        quality_command=display_click(DisplayControl::texture_original);
        check(quality_command.graphics_quality==GraphicsQuality{1,0},"texture Original preserves selected LOD");
        check(save_graphics_quality_config(isolated_config,*quality_command.graphics_quality).saved,"quality pair saved transactionally");
        parsed=nova::profile::read_config(isolated_config,obb);
        check(parsed.lod_distance==1 && parsed.texture_filtering==0 && parsed.world_fov==101.f,"restart reads selected quality, preserving optics");
        check(!build().graphics_quality,"idle quality does not autosave repeatedly");
        quality_command=display_click(DisplayControl::lod_reset);
        check(quality_command.graphics_quality==GraphicsQuality{4,0},"detail reset restores best default and preserves texture choice");
        quality_command=display_click(DisplayControl::texture_reset);
        check(quality_command.graphics_quality==GraphicsQuality{},"texture reset restores best default and preserves detail choice");
        build(); gl.run(draw);
        for(size_t y=0;y<720;++y) std::copy_n(pixels.data()+(719-y)*1280*4,1280*4,top.data()+y*1280*4);
        nova::gles::write_png(output.parent_path()/(output.stem().string()+"-graphics.png"),1280,720,top);
        frame.graphics_quality_error="synthetic failure";build();build();
        check(display_click(DisplayControl::graphics_retry).graphics_quality==GraphicsQuality{},"failed quality save has explicit retry");
        frame.graphics_quality_error.clear();build();

        display_click(DisplayControl::gameplay_tab); build();
        check(panel.control_rect(FovControl::world_slider).y2<panel.control_rect(FovControl::weapon_slider).y1,
              "Gameplay stays two stacked sliders and Reset after Display navigation");
    }
    {
        auto click_rect=[&](FovUiRect rect) {
            check(rect.x2>rect.x1 && rect.y2>rect.y1,"Controls widget has actual geometry");
            const auto x=(rect.x1+rect.x2)*.5f,y=(rect.y1+rect.y2)*.5f;
            mouse(SDL_EVENT_MOUSE_MOTION,x,y); build();
            mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,x,y); build();
            mouse(SDL_EVENT_MOUSE_BUTTON_UP,x,y); return build();
        };
        const auto optics_before=build().visible;
        const auto saves_before=persistence_calls;
        // Actual SliderInt -> command: no second Apply action and no per-frame save.
        auto bob=panel.control_rect(FovControl::movement_slider);
        check(bob.x2>bob.x1 && bob.x2<=panel.control_rect(FovControl::world_slider).x2,
              "bob slider fits inside the existing aligned Gameplay rows");
        const auto bob_y=(bob.y1+bob.y2)*.5f;
        mouse(SDL_EVENT_MOUSE_MOTION,bob.x1+8,bob_y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,bob.x1+8,bob_y);
        check(!build().movement_wobble,"bob press does not save repeatedly");
        mouse(SDL_EVENT_MOUSE_MOTION,(bob.x1+bob.x2)*.5f,bob_y);
        check(!build().movement_wobble,"bob drag remains one pending edit");
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,(bob.x1+bob.x2)*.5f,bob_y);
        auto bob_command=build();
        check(bob_command.movement_wobble && bob_command.movement_wobble->percent()>40 &&
              bob_command.movement_wobble->percent()<60 && !bob_command.save,
              "releasing bob applies the selected strength without Apply or an optics save");
        check(!build().movement_wobble,"idle bob emits no duplicate request");
        mouse(SDL_EVENT_MOUSE_MOTION,bob.x1+8,bob_y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,bob.x1+8,bob_y); build();
        panel.ui_window_transition();
        check(build().movement_wobble && !build().movement_wobble,
              "window transition flushes an unfinished bob edit exactly once");
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,bob.x1+8,bob_y); build();
        mouse(SDL_EVENT_MOUSE_MOTION,bob.x2-8,bob_y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,bob.x2-8,bob_y); build();
        frame.focused=false; frame.minimized=true;
        check(build().movement_wobble && !build().movement_wobble,
              "focus loss and minimize preserve the last valid bob edit exactly once");
        frame.focused=true; frame.minimized=false;
        panel.ui_window_transition();
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,bob.x2-8,bob_y); build();
        mouse(SDL_EVENT_MOUSE_MOTION,(bob.x1+bob.x2)*.5f,bob_y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,(bob.x1+bob.x2)*.5f,bob_y); build();
        panel.ui_visible(false);
        check(build().movement_wobble && !build().movement_wobble,
              "closing the panel completes bob without waiting for another visible frame");
        panel.ui_visible(true);
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,(bob.x1+bob.x2)*.5f,bob_y); build(); build();
        click_rect(panel.controls_tab_rect()); build(); build();
        check(!build().bindings && !build().sensitivity,"idle Controls emits no save/runtime mapping or sensitivity");
        gl.run(draw);
        for(size_t y=0;y<720;++y) std::copy_n(pixels.data()+(719-y)*1280*4,1280*4,top.data()+y*1280*4);
        nova::gles::write_png(output.parent_path()/(output.stem().string()+"-controls.png"),1280,720,top);
        auto drag_sensitivity=[&](SensitivityControl control,float fraction) {
            const auto rect=panel.sensitivity_control_rect(control);
            check(rect.x2>rect.x1 && rect.y2>rect.y1,"sensitivity slider has actual widget geometry");
            const float y=(rect.y1+rect.y2)*.5f;
            mouse(SDL_EVENT_MOUSE_MOTION,rect.x1+8,y); build();
            mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,rect.x1+8,y);
            check(!build().sensitivity,"sensitivity press is not a completed save");
            mouse(SDL_EVENT_MOUSE_MOTION,rect.x1+(rect.x2-rect.x1)*fraction,y);
            check(!build().sensitivity,"sensitivity drag does not save repeatedly");
            mouse(SDL_EVENT_MOUSE_BUTTON_UP,rect.x1+(rect.x2-rect.x1)*fraction,y);
            return build();
        };
        auto mouse_settings=drag_sensitivity(SensitivityControl::normal,.65f);
        check(mouse_settings.sensitivity && mouse_settings.sensitivity->normal>.1f &&
              mouse_settings.sensitivity->aim_multiplier==1.f && !mouse_settings.bindings &&
              !mouse_settings.save && !mouse_settings.changed && !mouse_settings.display && !mouse_settings.apply_render,
              "normal slider completes once without changing explicit ADS or other settings");
        const auto changed_normal=mouse_settings.sensitivity->normal;
        check(!build().sensitivity,"idle after sensitivity completion emits no request");
        mouse_settings=drag_sensitivity(SensitivityControl::ads,.55f);
        check(mouse_settings.sensitivity && mouse_settings.sensitivity->normal==changed_normal &&
              mouse_settings.sensitivity->aim_multiplier>0.f && mouse_settings.sensitivity->aim_multiplier<2.f,
              "dimensionless aim multiplier is adjustable inside0..2");
        mouse_settings=drag_sensitivity(SensitivityControl::ads,0.f);
        check(mouse_settings.sensitivity && mouse_settings.sensitivity->aim_multiplier==0.f &&
              mouse_settings.sensitivity->normal==changed_normal,"actual aim slider reaches zero without altering base");
        mouse_settings=drag_sensitivity(SensitivityControl::ads,1.f);
        check(mouse_settings.sensitivity && mouse_settings.sensitivity->aim_multiplier==2.f,
              "actual aim slider reaches exact2x maximum");
        controls.replace_sensitivity(*mouse_settings.sensitivity);
        click_rect(panel.binding_control_rect(nova::input::Action::forward));
        check(panel.ui_binding_capture(),"binding button starts real capture");
        auto captured=send_key(SDL_EVENT_KEY_DOWN,SDLK_K,SDL_KMOD_NONE);
        check(captured.bindings && captured.bindings->keys.at(SDLK_K)==nova::input::Action::forward &&
              !captured.bindings->keys.contains(SDLK_W) && !captured.changed && !captured.save &&
              !captured.display && !captured.apply_render,"one completed Controls command leaves other settings intact");
        controls.replace_bindings(*captured.bindings);
        send_key(SDL_EVENT_KEY_UP,SDLK_K,SDL_KMOD_NONE);
        check(!build().bindings,"completed mapping never repeats on idle");
        click_rect(panel.binding_control_rect(nova::input::Action::forward));
        send_key(SDL_EVENT_KEY_DOWN,SDLK_S,SDL_KMOD_NONE);
        check(panel.ui_binding_capture() && !build().bindings,"conflict stays in capture and emits no mapping");
        send_key(SDL_EVENT_KEY_UP,SDLK_S,SDL_KMOD_NONE);
        send_key(SDL_EVENT_KEY_DOWN,SDLK_ESCAPE,SDL_KMOD_NONE);
        send_key(SDL_EVENT_KEY_UP,SDLK_ESCAPE,SDL_KMOD_NONE);
        check(panel.visible() && owner.visible() && !panel.ui_binding_capture() && !build().bindings,
              "real Escape cancels capture instead of closing Settings");
        click_rect(panel.binding_control_rect(nova::input::Action::forward)); build();
        click_rect(panel.controls_cancel_rect()); build();
        check(!panel.ui_binding_capture() && !build().bindings,"Cancel mouse pair cannot activate newly exposed row");
        click_rect(panel.binding_control_rect(nova::input::Action::forward));
        panel.ui_window_transition(); build();
        check(!panel.ui_binding_capture() && !build().bindings,"window transition cancels uncompleted widget capture");
        captured=click_rect(panel.controls_reset_rect());
        check(captured.bindings && nova::input::same_bindings(*captured.bindings,nova::input::ControlsConfig{}),
              "Controls Reset emits complete defaults once");
        check(captured.visible==optics_before && !captured.save && persistence_calls==saves_before &&
              !captured.display && !captured.apply_render && !captured.sensitivity,
              "binding Reset leaves FOV, Display and explicit sensitivities alone");
        mouse_settings=click_rect(panel.sensitivity_control_rect(SensitivityControl::reset));
        check(mouse_settings.sensitivity==nova::input::MouseSensitivity{} && !mouse_settings.bindings &&
              mouse_settings.visible==optics_before && !mouse_settings.save && !mouse_settings.display &&
              !mouse_settings.apply_render && persistence_calls==saves_before,
              "Reset sensitivities resets both values without touching bindings/FOV/Display");
        check(!build().sensitivity,"sensitivity Reset never repeats on idle");
        const auto sensitivity_rect=panel.sensitivity_control_rect(SensitivityControl::normal);
        const float sensitivity_y=(sensitivity_rect.y1+sensitivity_rect.y2)*.5f;
        mouse(SDL_EVENT_MOUSE_MOTION,sensitivity_rect.x1+8,sensitivity_y); build();
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,sensitivity_rect.x1+8,sensitivity_y); build();
        mouse(SDL_EVENT_MOUSE_MOTION,(sensitivity_rect.x1+sensitivity_rect.x2)*.5f,sensitivity_y); build();
        panel.ui_window_transition();
        check(build().sensitivity && !build().sensitivity,"window transition completes last valid sensitivity edit once");
        mouse(SDL_EVENT_MOUSE_BUTTON_UP,(sensitivity_rect.x1+sensitivity_rect.x2)*.5f,sensitivity_y); build();
        click_rect(panel.display_control_rect(DisplayControl::gameplay_tab)); build(); build();
    }
    const float close_x=panel.control_rect(FovControl::world_slider).x2+10;
    mouse(SDL_EVENT_MOUSE_MOTION,close_x,65); build(); mouse(SDL_EVENT_MOUSE_BUTTON_DOWN,close_x,65); build();
    mouse(SDL_EVENT_MOUSE_BUTTON_UP,close_x,65); command=build();
    check(command.closed && !panel.visible(),"normal window X closes compact panel");
    panel.ui_visible(false); build();
    check(!backend.wants_text && !backend.capture,"closed hint owns no text/drag capture");
    gl.run([&](Angle &,StateObjects &state) { state.apply(); const auto before=gl_state(); panel.gl_shutdown(); check(gl_state()==before,"renderer shutdown preserves state on GL lane"); });
    panel.ui_shutdown(); check(effects.created==effects.destroyed && effects.created==11,"cursor resources intercepted on owning UI lane");
    cleanup.fn={};
    std::filesystem::remove(isolated_config);
    SDL_DestroyWindow(effects.window); SDL_Quit();
    std::printf("panel renderer: real ImGui/GLES3 pixels=%u, guarded lifecycle, widgets and UI/GL interleaving passed\n",changed);
}
}
int main(int argc,char **argv) {
    try { check(argc>=2,"panel test mode required"); if(std::string(argv[1])=="policy") policy(); else {
        check(argc==4 || argc==5,"PNG and existing native fixture paths required");
        rendering(argv[2],argv[3],argc==5 ? argv[4] : "");
    } return 0; }
    catch(const std::exception &error) { std::fprintf(stderr,"FOV panel test: %s\n",error.what()); return 1; }
}
