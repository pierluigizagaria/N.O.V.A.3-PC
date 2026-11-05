#include "platform/angle.h"
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <cstdio>
#include <array>
#include <utility>
#include <exception>
#include <stdexcept>

namespace nova::platform {
namespace {
std::runtime_error failure(const char *stage) {
    char text[160];
    std::snprintf(text, sizeof text, "%s failed: EGL error 0x%04x", stage, eglGetError());
    return std::runtime_error(text);
}
} // namespace
Angle::Angle(const std::string &backend) {
    if (backend != "d3d11" && backend != "vulkan")
        throw std::invalid_argument("graphics_backend must be d3d11 or vulkan");
    auto get_display =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!get_display)
        throw std::runtime_error("ANGLE lacks eglGetPlatformDisplayEXT");
    const EGLint attributes[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE,
                                 backend == "vulkan" ? EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE
                                                     : EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
                                 EGL_NONE};
    std::printf("ANGLE requested backend=%s\n", backend.c_str());
    display_ = get_display(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attributes);
    EGLint major = 0, minor = 0;
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, &major, &minor)) {
        if (backend == "vulkan")
            std::fprintf(stderr, "Vulkan initialization failed; the pinned vcpkg ANGLE port "
                                 "does not include its Vulkan renderer. No backend fallback.\n");
        throw failure("eglInitialize");
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        throw failure("eglBindAPI");
    std::printf("EGL %d.%d vendor=%s\n", major, minor, eglQueryString(display_, EGL_VENDOR));
}
int Angle::create(void *window, int width, int height, int extra_contexts,const NativeDeviceProfile *profile) {
    if (!slots_.empty() || extra_contexts < 0 || extra_contexts > 16)
        throw std::invalid_argument("Invalid EGL context creation request");
    const EGLint attributes[] = {EGL_RED_SIZE,
                                 8,
                                 EGL_GREEN_SIZE,
                                 8,
                                 EGL_BLUE_SIZE,
                                 8,
                                 EGL_ALPHA_SIZE,
                                 8,
                                 EGL_DEPTH_SIZE,
                                 24,
                                 EGL_STENCIL_SIZE,
                                 8,
                                 EGL_SURFACE_TYPE,
                                 EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                                 EGL_RENDERABLE_TYPE,
                                 EGL_OPENGL_ES3_BIT_KHR,
                                 EGL_NONE};
    EGLint found = 0;
    if (!eglChooseConfig(display_, attributes, &config_, 1, &found) || found != 1)
        throw failure("eglChooseConfig(ES3 RGBA8 D24S8)");
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    for (int index = 0; index <= extra_contexts; ++index) {
        auto slot = std::make_unique<Slot>();
        int w = index == 0 ? width : 1, h = index == 0 ? height : 1;
        const EGLint pbuffer_attributes[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
        slot->surface = index == 0 && window
                            ? eglCreateWindowSurface(display_, config_,
                                                     static_cast<EGLNativeWindowType>(window), nullptr)
                            : eglCreatePbufferSurface(display_, config_, pbuffer_attributes);
        slot->context = eglCreateContext(
            display_, config_, index == 0 ? EGL_NO_CONTEXT : slots_[0]->context, context_attributes);
        if (slot->surface == EGL_NO_SURFACE || slot->context == EGL_NO_CONTEXT) {
            if (slot->surface != EGL_NO_SURFACE)
                eglDestroySurface(display_, slot->surface);
            if (slot->context != EGL_NO_CONTEXT)
                eglDestroyContext(display_, slot->context);
            if (index == 0)
                throw failure("eglCreateContext/Surface");
            std::fprintf(stderr, "EGL extra context %d unavailable; reporting reduced count\n", index);
            break;
        }
        slots_.push_back(std::move(slot));
        if (!make_current(index))
            throw failure("eglMakeCurrent");
        if(index==0) {
            GLint viewport[2]{};
            glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limits_.texture);
            glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&limits_.renderbuffer);
            glGetIntegerv(GL_MAX_VIEWPORT_DIMS,viewport);
            limits_.viewport_width=viewport[0]; limits_.viewport_height=viewport[1];
            if(!limits_.storage(w,h)) {
                if(!limits_.storage(1280,720)) throw std::runtime_error("Renderer cannot allocate the native fallback target");
                std::printf("render startup requested=%dx%d exceeds renderer limits; actual=1280x720\n",w,h);
                w=1280; h=720;
            }
        }
        try { gles::initialize(slots_.back()->target,w,h,false,profile); }
        catch(const std::exception &) {
            if(index!=0 || (w==1280 && h==720)) throw;
            // Saved settings can outlive the GPU/memory conditions on which
            // they applied. Retire partial host allocation before guest startup.
            gles::destroy(slots_.back()->target);
            for(int error=0;error<16 && glGetError()!=GL_NO_ERROR;++error) {}
            std::printf("render startup requested=%dx%d allocation failed; actual=1280x720\n",w,h);
            gles::initialize(slots_.back()->target,1280,720,false,profile);
        }
    }
    if (!make_current(0) || !eglSwapInterval(display_, 1))
        throw failure("eglMakeCurrent/eglSwapInterval");
    std::printf("EGL main + %zu shared contexts; swap_interval=1\n", slots_.size() - 1);
    return static_cast<int>(slots_.size()) - 1;
}
bool Angle::make_current(int index) {
    if (index < 0) {
        if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
            return false;
        gles::set_context(nullptr);
        return true;
    }
    if (static_cast<size_t>(index) >= slots_.size())
        return false;
    auto &slot = *slots_[index];
    if (!eglMakeCurrent(display_, slot.surface, slot.surface, slot.context)) {
        std::fprintf(stderr, "eglMakeCurrent index=%d error=0x%x\n", index, eglGetError());
        return false;
    }
    gles::set_context(&slot.target);
    return true;
}
int Angle::current_index() const {
    const auto context=eglGetCurrentContext();
    for(size_t i=0;i<slots_.size();++i) if(slots_[i]->context==context) return static_cast<int>(i);
    return -1;
}
void Angle::presentation_size(int width,int height) {
    if(width<=0 || height<=0) throw std::invalid_argument("Invalid presentation dimensions");
    output_width_=width; output_height_=height;
}
void Angle::present(gles::PresentationOverlay overlay,void *user) {
    int previous=-1;
    const auto current=eglGetCurrentContext();
    for(size_t index=0;index<slots_.size();++index)
        if(slots_[index]->context==current) previous=static_cast<int>(index);
    if(previous<0) throw std::runtime_error("Presentation requires an owned EGL context");
    if(previous!=0 && !make_current(0)) throw failure("Presentation primary context");
    try { gles::present(slots_[0]->target,display_,slots_[0]->surface,overlay,user,output_width_,output_height_); }
    catch(...) { if(previous!=0) make_current(previous); throw; }
    if(previous!=0 && !make_current(previous)) throw failure("Restore guest EGL context");
}
void Angle::capture(const std::filesystem::path &path) { gles::screenshot(slots_[0]->target, path); }
gles::Context &Angle::main_target() { return slots_.at(0)->target; }
namespace {
struct TargetBindings {
    GLint draw=0,read=0,renderbuffer=0;
    std::array<GLint,4> viewport{};
    std::array<GLfloat,4> clear{};
    GLboolean scissor=GL_FALSE,discard=GL_FALSE;
    gles::Context *owner=gles::current_context();
    TargetBindings() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer); glGetIntegerv(GL_VIEWPORT,viewport.data());
        glGetFloatv(GL_COLOR_CLEAR_VALUE,clear.data());
        scissor=glIsEnabled(GL_SCISSOR_TEST); discard=glIsEnabled(GL_RASTERIZER_DISCARD);
    }
    ~TargetBindings() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw); glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer); glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glClearColor(clear[0],clear[1],clear[2],clear[3]);
        if(scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if(discard) glEnable(GL_RASTERIZER_DISCARD); else glDisable(GL_RASTERIZER_DISCARD);
        gles::set_context(owner);
    }
};
}
std::unique_ptr<Angle::TargetChange> Angle::prepare_target(int width,int height,const NativeDeviceProfile *profile) {
    if(slots_.empty() || eglGetCurrentContext()!=slots_[0]->context)
        throw std::logic_error("Render target preparation requires the primary GL context");
    if(!limits_.storage(width,height)) throw std::invalid_argument("Resolution exceeds renderer resource limits");
    auto change=std::unique_ptr<TargetChange>(new TargetChange(*this));
    TargetBindings bindings;
    try {
        gles::initialize(change->retained_,width,height,false,profile);
        // Preserve the visible frame while the native scene is retained. All
        // targets are single-sample RGBA8/D24S8; depth/stencil use nearest.
        const auto &old=main_target();
        glDisable(GL_SCISSOR_TEST); glDisable(GL_RASTERIZER_DISCARD);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,old.framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,change->retained_.framebuffer);
        glBlitFramebuffer(0,0,old.width,old.height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        glBlitFramebuffer(0,0,old.width,old.height,0,0,width,height,
                          GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT,GL_NEAREST);
    } catch(...) {
        gles::destroy(change->retained_); change->owner_=nullptr; throw;
    }
    return change;
}
void Angle::TargetChange::swap() {
    if(!owner_ || eglGetCurrentContext()!=owner_->slots_[0]->context)
        throw std::logic_error("Render target change requires its primary GL context");
    auto &target=owner_->main_target();
    const auto old_framebuffer=target.framebuffer,old_color=target.color,old_depth=target.depth_stencil;
    const auto pending=target.pending_error;
    GLint draw=0,read=0,renderbuffer=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);
    std::swap(target,retained_); target.pending_error=pending; retained_.pending_error=GL_NO_ERROR;
    if(GLuint(draw)==old_framebuffer) glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target.framebuffer);
    if(GLuint(read)==old_framebuffer) glBindFramebuffer(GL_READ_FRAMEBUFFER,target.framebuffer);
    if(GLuint(renderbuffer)==old_color) glBindRenderbuffer(GL_RENDERBUFFER,target.color);
    else if(GLuint(renderbuffer)==old_depth) glBindRenderbuffer(GL_RENDERBUFFER,target.depth_stencil);
    gles::set_context(&target);
}
void Angle::TargetChange::activate() { if(!active_) { swap(); active_=true; } }
void Angle::TargetChange::rollback() { if(active_) { swap(); active_=false; } }
void Angle::TargetChange::commit() {
    if(!active_) throw std::logic_error("Cannot commit an inactive render target");
    gles::destroy(retained_); owner_=nullptr; active_=false;
}
Angle::TargetChange::~TargetChange() {
    if(!owner_) return;
    try { rollback(); gles::destroy(retained_); }
    catch(...) { std::terminate(); }
}
bool Angle::target_matches(int width,int height) {
    if(slots_.empty() || eglGetCurrentContext()!=slots_[0]->context) return false;
    const auto &target=main_target();
    if(target.width!=width || target.height!=height) return false;
    TargetBindings bindings;
    glBindFramebuffer(GL_FRAMEBUFFER,target.framebuffer);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return false;
    for(const auto &[name,format]:{std::pair{target.color,GL_RGBA8},std::pair{target.depth_stencil,GL_DEPTH24_STENCIL8}}) {
        glBindRenderbuffer(GL_RENDERBUFFER,name);
        GLint w=0,h=0,samples=0,actual_format=0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&w);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&h);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,&samples);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_INTERNAL_FORMAT,&actual_format);
        if(w!=width || h!=height || samples!=0 || actual_format!=format) return false;
    }
    return true;
}
Angle::~Angle() {
    // Product guest runs terminate the process; this cleanup serves synthetic tests.
    for (size_t index = 0; index < slots_.size(); ++index) {
        if (make_current(static_cast<int>(index)))
            gles::destroy(slots_[index]->target);
    }
    make_current(-1);
    for (auto &slot : slots_) {
        eglDestroyContext(display_, slot->context);
        eglDestroySurface(display_, slot->surface);
    }
    if (display_ != EGL_NO_DISPLAY)
        eglTerminate(display_);
}
} // namespace nova::platform
