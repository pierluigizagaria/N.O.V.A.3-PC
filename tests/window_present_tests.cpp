#include "platform/angle.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
struct Observation { bool called=false; EGLContext expected=EGL_NO_CONTEXT; };
void inspect(void *user,int width,int height) {
    auto &result=*static_cast<Observation *>(user); result.called=true;
    check(width==128 && height==96,"Overlay uses whole native drawable dimensions");
    check(eglGetCurrentContext()==result.expected,"Primary context owns letterbox presentation");
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    std::array<unsigned char,4> bar{},content{},edge{};
    glReadPixels(64,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bar.data());
    glReadPixels(64,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,content.data());
    glReadPixels(127,83,1,1,GL_RGBA,GL_UNSIGNED_BYTE,edge.data());
    check(bar[0]==0 && bar[1]==0 && bar[2]==0,"Letterbox bars are black despite inherited guest masks/scissor");
    check(content[0]==255 && content[1]==0 && edge[0]==255,"Guest content fills fitted rectangle, including its far edge");
}
std::array<unsigned char,4> checker(int x,int y) {
    return {static_cast<unsigned char>((x/4+y/4)%2 ? 255 : 0),
            static_cast<unsigned char>(x<32 ? 0 : 255),
            static_cast<unsigned char>((x/8)%2 ? 64 : 192),255};
}
struct Resampling {
    int width,height,content_height,bottom;
    EGLContext context;
    bool called=false;
};
void inspect_resampling(void *user,int width,int height) {
    auto &sample=*static_cast<Resampling *>(user); sample.called=true;
    check(width==sample.width && height==sample.height && eglGetCurrentContext()==sample.context,
          "Resampling observes the requested drawable on the primary context");
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*4);
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    size_t interpolated=0;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const size_t offset=(static_cast<size_t>(y)*width+x)*4;
        if(y<sample.bottom || y>=sample.bottom+sample.content_height) {
            check(pixels[offset]==0 && pixels[offset+1]==0 && pixels[offset+2]==0,
                  "Noninteger resampling preserves both black letterbox bars");
            continue;
        }
        // Independent pixel-center reference. GL image coordinates are bottom-up.
        const double u=(x+.5)*64/width-.5;
        const double v=(y-sample.bottom+.5)*36/sample.content_height-.5;
        const int left=static_cast<int>(std::floor(u)),lower=static_cast<int>(std::floor(v));
        const double fx=u-left,fy=v-lower;
        const auto a=checker(std::clamp(left,0,63),std::clamp(lower,0,35));
        const auto b=checker(std::clamp(left+1,0,63),std::clamp(lower,0,35));
        const auto c=checker(std::clamp(left,0,63),std::clamp(lower+1,0,35));
        const auto d=checker(std::clamp(left+1,0,63),std::clamp(lower+1,0,35));
        for(size_t channel=0;channel<4;++channel) {
            const double expected=(1-fy)*((1-fx)*a[channel]+fx*b[channel])+
                                  fy*((1-fx)*c[channel]+fx*d[channel]);
            const int tolerance=width==64 && height==36 ? 0 : 2;
            check(std::abs(double(pixels[offset+channel])-expected)<=tolerance,
                  "Presented checker and color edge match bilinear pixel-center reference");
        }
        if(pixels[offset]>2 && pixels[offset]<253) ++interpolated;
    }
    check(width==64 || interpolated>100,"Resized checker contains interpolated edge samples");
}
void resampling(nova::platform::Angle &angle) {
    check(angle.make_current(0),"Select primary context for resampling");
    auto &target=angle.main_target();
    GLint samples=-1,component=0;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glGetIntegerv(GL_SAMPLES,&samples);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_BACK,
        GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,&component);
    check(samples==0 && component==GL_UNSIGNED_NORMALIZED,
          "Actual presentation destination is single-sample normalized color");
    glBindFramebuffer(GL_FRAMEBUFFER,target.framebuffer);
    glGetIntegerv(GL_SAMPLES,&samples);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
        GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,&component);
    check(samples==0 && component==GL_UNSIGNED_NORMALIZED,
          "Actual guest source permits linear color blits without multisample resolve");
    glDisable(GL_RASTERIZER_DISCARD); glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    for(int y=0;y<36;y+=4) for(int x=0;x<64;x+=4) {
        const auto color=checker(x,y);
        glScissor(x,y,4,4);
        glClearColor(color[0]/255.f,color[1]/255.f,color[2]/255.f,1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    std::vector<unsigned char> before(64*36*4),after(before.size());
    glReadPixels(0,0,64,36,GL_RGBA,GL_UNSIGNED_BYTE,before.data());
    // Preserve a distinct read binding plus inherited masks/scissor/discard.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glEnable(GL_RASTERIZER_DISCARD); glScissor(3,4,5,6);
    glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
    glClearColor(.2f,.3f,.4f,.5f); glViewport(-2,3,60,30);
    const auto primary=eglGetCurrentContext();
    // Pbuffer is 128x96. These outputs exercise unequal noninteger X/Y
    // scaling, odd-sized bars, downscaling and exact one-to-one identity.
    for(auto sample:std::array<Resampling,3>{{{119,93,66,14,primary},
                                             {47,43,26,9,primary},
                                             {64,36,36,0,primary}}}) {
        angle.presentation_size(sample.width,sample.height);
        angle.present(inspect_resampling,&sample);
        GLint draw=0,read=-1,viewport[4]{},scissor[4]{};
        GLboolean mask[4]{}; GLfloat color[4]{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_VIEWPORT,viewport); glGetIntegerv(GL_SCISSOR_BOX,scissor);
        glGetBooleanv(GL_COLOR_WRITEMASK,mask); glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
        check(sample.called && target.width==64 && target.height==36 &&
              static_cast<GLuint>(draw)==target.framebuffer && read==0 &&
              viewport[0]==-2 && viewport[1]==3 && viewport[2]==60 && viewport[3]==30 &&
              scissor[0]==3 && scissor[1]==4 && scissor[2]==5 && scissor[3]==6 &&
              !mask[0] && mask[1] && !mask[2] && !mask[3] && color[0]==.2f && color[3]==.5f &&
              glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_RASTERIZER_DISCARD),
              "Linear presentation preserves guest size, separate bindings and inherited GL state");
        check(glGetError()==GL_NO_ERROR,"Linear color blit is valid on the actual RGBA8 pbuffer path");
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER,target.framebuffer);
    glReadPixels(0,0,64,36,GL_RGBA,GL_UNSIGNED_BYTE,after.data());
    check(before==after,"Presentation resampling never changes the guest color target");
}
}
namespace {
void render_transaction(nova::platform::Angle &angle) {
    check(angle.make_current(0),"transaction primary context");
    auto &target=angle.main_target(); const auto old_framebuffer=target.framebuffer;
    const auto old_width=target.width,old_height=target.height;
    GLint max_texture=0,max_renderbuffer=0,max_viewport[2]{};
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture); glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&max_renderbuffer);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS,max_viewport);
    const auto limits=angle.render_limits();
    check(limits.texture==max_texture && limits.renderbuffer==max_renderbuffer &&
          limits.viewport_width==max_viewport[0] && limits.viewport_height==max_viewport[1],
          "catalogue uses actual current ANGLE texture/renderbuffer/viewport capabilities");
    glDisable(GL_SCISSOR_TEST); glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,target.framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER,target.color);
    glEnable(GL_SCISSOR_TEST); glScissor(3,4,5,6); glEnable(GL_RASTERIZER_DISCARD);
    glViewport(-2,3,60,30); glClearColor(.2f,.3f,.4f,.5f);
    const auto old_context=nova::gles::current_context();
    auto change=angle.prepare_target(1600,900);
    GLint draw=0,read=0,renderbuffer=0,viewport[4]{}; GLfloat clear[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer); glGetIntegerv(GL_VIEWPORT,viewport); glGetFloatv(GL_COLOR_CLEAR_VALUE,clear);
    check(target.framebuffer==old_framebuffer && target.width==old_width && target.height==old_height &&
          GLuint(draw)==old_framebuffer && GLuint(read)==old_framebuffer && GLuint(renderbuffer)==target.color &&
          viewport[0]==-2 && viewport[2]==60 && clear[0]==.2f && clear[3]==.5f &&
          glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_RASTERIZER_DISCARD) && nova::gles::current_context()==old_context,
          "preallocation preserves live backing, separate bindings, TLS owner and inherited state");
    change->activate(); const auto staged_framebuffer=target.framebuffer;
    check(angle.target_matches(1600,900) && staged_framebuffer!=old_framebuffer,"activation publishes actual replacement color/depth storage");
    std::array<uint8_t,4> pixel{}; glBindFramebuffer(GL_READ_FRAMEBUFFER,target.framebuffer);
    glReadPixels(800,450,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    check(pixel[0]==255 && pixel[1]==0 && pixel[2]==0,"preallocated replacement retains the visible scene color");
    change->rollback(); change.reset();
    check(target.framebuffer==old_framebuffer && angle.target_matches(old_width,old_height) && !glIsFramebuffer(staged_framebuffer),
          "failed native transaction can restore exact old backing and delete only the candidate");
    GLint limit=0; glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE,&limit); bool rejected=false;
    try { auto invalid=angle.prepare_target(limit+1,1); } catch(const std::invalid_argument &) { rejected=true; }
    check(rejected && target.framebuffer==old_framebuffer && angle.target_matches(old_width,old_height) &&
          nova::gles::current_context()==old_context,"capability preflight refusal preserves active backing and TLS owner");
    check(glGetError()==GL_NO_ERROR,"capability refusal issues no invalid storage request");
    change=angle.prepare_target(1280,720); change->activate(); change->commit(); change.reset();
    check(angle.target_matches(1280,720) && !glIsFramebuffer(old_framebuffer),"success commits new storage and retires old backing only after ACK");
    check(glGetError()==GL_NO_ERROR,"resolution transaction leaves no GL error");
    for(const auto extent:{nova::platform::GuestExtent{2560,1440},nova::platform::GuestExtent{3840,2160}}) {
        if(!limits.admits(extent)) continue;
        auto higher=angle.prepare_target(extent.width,extent.height); higher->activate();
        check(angle.target_matches(extent.width,extent.height),"higher catalogue mode allocates actual color/depth storage");
        higher->rollback();
        check(angle.target_matches(1280,720),"higher mode preflight retains exact lower backing for recovery");
    }
}
}
int main() {
    try {
        // The native profile prerequisite remains enforced before any GL work.
        nova::gles::Context refused;
        bool rejected=false;
        try { nova::gles::initialize(refused,1920,1080); }
        catch(const std::invalid_argument &) { rejected=true; }
        check(rejected && refused.width==0 && refused.height==0 && refused.framebuffer==0 &&
              refused.color==0 && refused.depth_stencil==0,
              "1920 profile guard rejects before target allocation or state publication");
        // Pbuffer only: no SDL window, desktop mutation or guest/game library.
        nova::platform::Angle angle("d3d11"); angle.create(nullptr,128,96,1);
        auto &target=angle.main_target();
        nova::gles::destroy(target); nova::gles::initialize(target,64,36);
        glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        Observation result; result.expected=eglGetCurrentContext();
        angle.presentation_size(128,96);
        // Seed the whole host backbuffer with a visible old frame: a clear
        // skipped by inherited rasterizer-discard must leave a failing bar.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0); glClearColor(0,1,0,1); glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target.framebuffer);
        glEnable(GL_RASTERIZER_DISCARD);
        glEnable(GL_SCISSOR_TEST); glScissor(0,0,1,1);
        glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
        glClearColor(.2f,.3f,.4f,.5f); glViewport(-2,3,60,30);
        angle.present(inspect,&result);
        GLboolean mask[4]{}; GLfloat color[4]{}; GLint viewport[4]{},draw=0,read=0;
        glGetBooleanv(GL_COLOR_WRITEMASK,mask); glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
        glGetIntegerv(GL_VIEWPORT,viewport); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        check(result.called && target.width==64 && target.height==36,"Presentation never changes guest target size");
        check(!mask[0] && mask[1] && !mask[2] && !mask[3] && color[0]==.2f && color[3]==.5f &&
              viewport[0]==-2 && viewport[1]==3 && viewport[2]==60 && viewport[3]==30 &&
              glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_RASTERIZER_DISCARD) && static_cast<GLuint>(draw)==target.framebuffer && static_cast<GLuint>(read)==target.framebuffer,
              "Letterbox clear/blit restores masks, clear color, viewport, scissor, discard and framebuffer bindings");
        check(angle.make_current(1),"Select synthetic loading context");
        const auto previous=eglGetCurrentContext(); result.called=false;
        angle.present(inspect,&result);
        check(result.called && eglGetCurrentContext()==previous,"Presentation restores prior owned loading context");
        angle.present();
        check(eglGetCurrentContext()==previous,"Presentation without overlay also restores loading context");
        check(glGetError()==GL_NO_ERROR,"Presentation leaves no GL error");
        resampling(angle);
        render_transaction(angle);
        std::puts("PASS platform-window-present (pbuffer only)"); return 0;
    } catch(const std::exception &error) {
        std::fprintf(stderr,"window present test: %s\n",error.what()); return 1;
    }
}
