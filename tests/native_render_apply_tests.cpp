#include "platform/native_render_extent.h"
#include "platform/angle.h"
#include "camera/native_world_fov.h"
#include "abi/guest_hook.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
uint32_t address(auto p) { return reinterpret_cast<uint32_t>(p); }
uint32_t &word(uint32_t p,uint32_t o=0) { return *reinterpret_cast<uint32_t *>(p+o); }
uint32_t __cdecl ignore_log(uint32_t,...) { return 0; }
uint32_t __cdecl thread_zero() { return 0; }
std::array<int32_t,4> last_viewport{};
uint32_t owner_slot=0,replacement_device=0,viewport_calls=0;
bool replace_owner=false;
uint32_t poison_effect_target=0;
bool effect_target_poisoned=false;
uint32_t original_effect_bind=0;
bool compile_native_attachments=false;
struct EffectResizeFixture {
    uint32_t fx=0,color_object=0,release=0;
    GLuint *color=nullptr;
    std::array<std::array<uint32_t,8>,8> depths{};
    size_t count=0;
    bool fail_color_once=false;
};
EffectResizeFixture *resize_fixture=nullptr;
void __cdecl resize_effects(uint32_t fx,uint32_t width,uint32_t height) {
    auto &f=*resize_fixture;
    check(f.fx==fx && f.count<f.depths.size(),"isolated Resize owner/capacity");
    word(fx,4)=width; word(fx,8)=height; word(fx,0xc)=width/4; word(fx,0x10)=height/4;
    auto &depth=f.depths[f.count++];
    depth[1]=2; // FX owner plus a fixture sentinel; observe every native release.
    depth[2]=0x2d; depth[3]=width; depth[4]=height;
    depth[5]=word(word(fx,0x90),8); // Original compileAttachment lazily allocates GL backing.
    const auto old=word(fx,0x12c); word(fx,0x12c)=address(depth.data());
    nova::abi::invoke(f.release,{old});
    glDeleteTextures(1,f.color); glGenTextures(1,f.color); glBindTexture(GL_TEXTURE_2D,*f.color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,f.fail_color_once ? 16 : width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    f.fail_color_once=false;
    word(f.color_object,0x24)=*f.color;
    for(auto slot:{0x90u,0xa8u}) {
        const auto target=word(fx,slot);
        word(target,0xc)=word(target,0x1c)=width;
        word(target,0x10)=word(target,0x20)=height;
        word(target,0x5c)|=0x01000000;
    }
    // Exact evidenced defect: native Resize publishes the new shared depth at
    // +12c but leaves +98/+b0 AND both RTT attachment records on the old owner.
}
void __cdecl viewport(uint32_t,const int32_t *rect) {
    // Only the real driver's GL/cache implementation is isolated. The native
    // onResize and IRenderTarget::setViewport construct the observed rectangle.
    std::memcpy(last_viewport.data(),rect,sizeof last_viewport);
    ++viewport_calls;
    if(replace_owner) { word(owner_slot)=replacement_device; replace_owner=false; }
    glViewport(rect[0],rect[1],rect[2]-rect[0],rect[3]-rect[1]);
}
void __cdecl effect_bind(uint32_t target) {
    if(compile_native_attachments) {
        // Execute original attachment compilation, isolating only the unrelated
        // material refresh of this fixture's uninitialized scene driver.
        const auto driver=word(target,8);
        word(driver,0x540)=1;
        nova::abi::invoke(original_effect_bind,{target});
        word(driver,0x540)=0;
        return;
    }
    if(!poison_effect_target) { glBindFramebuffer(GL_FRAMEBUFFER,word(target,0x28)); return; }
    // Checked bind-entry seam for the resource-verification owner negative.
    // Preserve bind's actual viewport callback boundary, then make any later
    // cached RTT read observable as a refusal/fault instead of a false success.
    const auto driver=word(target,8);
    nova::abi::invoke(word(word(driver),0x9c),{driver,target+0x14});
    DWORD old=0;
    effect_target_poisoned=VirtualProtect(reinterpret_cast<void *>(poison_effect_target),4096,PAGE_NOACCESS,&old)!=FALSE;
}
constexpr auto resize_name="_ZN3glf19AndroidResizeScreenEii";
constexpr auto log_name="_ZN3glf7Console7PrintlnEPKcz";
constexpr auto ctor_name="_ZN6glitch5video13IRenderTargetC2EPNS0_12IVideoDriverERKNS_4core11dimension2dIiEENS0_22E_ANTIALIASING_SETTINGE";
constexpr auto viewport_name="_ZN6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE15setViewportImplERKNS_4core4rectIiEE";
constexpr auto effect_bind_name="_ZN6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE13CRenderTarget4bindEv";
constexpr auto fx_resize_name="_ZN11PostEffects6ResizeEii";
}
int main(int argc,char **argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=2) return 2;
    if(!std::filesystem::exists(argv[1])) return 77;
    try {
        nova::ImportRegistry imports;
        nova::bionic::register_boot(imports); nova::audio::register_opensl(imports); nova::gles::register_imports(imports);
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1],imports);
        nova::camera::NativeWorldFov fov(image,{}); fov.bind_render_thread();
        nova::platform::NativeRenderExtent native(image,argv[1],fov);
        // No initializers/App construction/profile resources/scene/input/network.
        // Image-bound admission occurs before these isolated dependency hooks.
        nova::abi::GuestCall log0(image,resize_name,0x43,log_name,address(&ignore_log));
        nova::abi::GuestCall log1(image,resize_name,0x79,log_name,address(&ignore_log));
        nova::abi::GuestCall log2(image,resize_name,0x9e,log_name,address(&ignore_log));
        nova::abi::GuestCall tid(image,resize_name,0xc1,"_ZN3glf6Thread21GetSequentialThreadIdEv",address(&thread_zero));
        nova::abi::GuestHook vp(image,viewport_name,{0x55,0x89,0xe5,0x83,0xec,0x78},address(&viewport));
        std::vector<uint32_t> app(0x18738/4),impl(4),device(0x60/4),driver(0x1000/4),target(0x40/4),touch(0x1c/4);
        std::array<uint32_t,1> stack{address(target.data())};
        const auto app_at=address(app.data()),driver_at=address(driver.data()),touch_at=address(touch.data());
        word(image.address(0x115f818,4))=app_at;
        word(image.address(0x115f810,4))=address(impl.data()); impl[1]=app_at;
        word(image.address(0x115683c,4))=address(device.data()); device[2]=driver_at; device[0x5c/4]=app_at;
        driver[0]=image.address(0x1124cec); driver[0x114/4]=address(stack.data()); driver[0x118/4]=address(stack.data())+4;
        touch[0]=image.address(0x1107e28); word(image.address(0x11568c4,4))=touch_at;
        const std::array<int32_t,2> initial{1280,720};
        nova::abi::invoke(nova::abi::require_function(image,ctor_name),{address(target.data()),driver_at,address(initial.data()),0});
        target[0]=image.address(0x1126fa8); // Exact derived CFramebuffer constructor vptr/zero fields.
        const std::array<int16_t,8> touch_values{0,0,1280,720,0,0,1280,720};
        std::memcpy(reinterpret_cast<void *>(touch_at+8),touch_values.data(),sizeof touch_values);
        word(image.address(0x11437f0,4))=1280; word(image.address(0x11437f4,4))=720;
        nova::abi::invoke(nova::abi::require_function(image,resize_name),{1280,720});
        nova::platform::Angle angle("d3d11"); angle.create(nullptr,1280,720,0,&native.profile());
        // The isolated driver must advertise its real framebuffer backend.
        // Otherwise native default bind enters the uninitialized legacy path.
        driver[0xe4/4]|=0x40000;
        GLint native_limit=0; glGetIntegerv(GL_MAX_TEXTURE_SIZE,&native_limit);
        driver[0x64/4]=driver[0x68/4]=static_cast<uint32_t>(native_limit);
        // FX verification must query real texture storage on the pinned ES3.0
        // backend, not merely trust native descriptions or a non-null proc.
        using TextureQuery=void(GL_APIENTRY *)(GLenum,GLint,GLenum,GLint *);
        const auto query=reinterpret_cast<TextureQuery>(eglGetProcAddress("glGetTexLevelParameterivANGLE"));
        check(query!=nullptr,"actual ANGLE texture-size query is available");
        GLint previous_texture=0,w=0,h=0; GLuint texture=0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous_texture); glGenTextures(1,&texture);
        glBindTexture(GL_TEXTURE_2D,texture); glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,16,8);
        query(GL_TEXTURE_2D,0,0x1000,&w); query(GL_TEXTURE_2D,0,0x1001,&h);
        glBindTexture(GL_TEXTURE_2D,previous_texture); glDeleteTextures(1,&texture);
        check(w==16 && h==8 && glGetError()==GL_NO_ERROR,"actual FX texture-size query observes real storage dimensions");
        const auto context=eglGetCurrentContext();
        check(native.available() && native.current()==nova::platform::GuestExtent{},"native primary lifecycle is ready");
        auto candidates=std::vector<nova::platform::GuestExtent>(nova::platform::standard_render_extents.begin(),nova::platform::standard_render_extents.end());
        candidates.push_back({1280,720});
        for(const auto extent:candidates) {
            if(!angle.render_limits().admits(extent)) continue;
            auto gpu=angle.prepare_target(extent.width,extent.height,&native.profile()); gpu->activate();
            const auto applied=native.apply(extent);
            check(applied.applied && applied.error.empty() && native.current()==extent,"actual native resize acknowledged");
            check(angle.target_matches(extent.width,extent.height),"actual ANGLE color/depth storage agrees with native dimensions");
            GLint observed[4]{}; glGetIntegerv(GL_VIEWPORT,observed);
            check(observed[2]==extent.width && observed[3]==extent.height && last_viewport[2]==extent.width &&
                  last_viewport[3]==extent.height,"real native target setter reaches the actual new GL viewport");
            check(word(app_at,0x17ae4)==1280 && word(app_at,0x17ae8)==720,"native creation settings remain the startup record");
            check(eglGetCurrentContext()==context,"Apply preserves the existing GL context");
            gpu->commit();
            std::printf("native catalogue verified=%dx%d\n",extent.width,extent.height);
        }
        driver[0x64/4]=driver[0x68/4]=1536;
        check(!native.apply({1600,900}).applied && native.current()==nova::platform::GuestExtent{},
              "native texture limit refuses before dimensions or resources change");
        driver[0x64/4]=driver[0x68/4]=static_cast<uint32_t>(native_limit);
        const auto before=angle.main_target().framebuffer;
        check(!native.apply({800,600}).applied && native.current()==nova::platform::GuestExtent{} &&
              angle.main_target().framebuffer==before,"unsupported extent refuses without state or storage mutation");
        driver[0x118/4]+=4;
        check(!native.available() && !native.apply({1600,900}).applied,"non-boundary target stack cannot accept Apply");
        driver[0x118/4]-=4;
        check(native.current()==nova::platform::GuestExtent{} && angle.target_matches(1280,720),"negative leaves native/GPU extent coherent");
        std::vector<uint32_t> other_device=device;
        owner_slot=image.address(0x115683c,4); replacement_device=address(other_device.data());
        {
            std::vector<uint32_t> level(0x5b4/4),effects(0x134/4);
            auto free_page=[](void *p) { if(p) VirtualFree(p,0,MEM_RELEASE); };
            std::unique_ptr<void,decltype(free_page)> page(
                VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE),free_page);
            check(page!=nullptr,"allocate isolated native effects target page");
            const auto target_at=address(page.get());
            nova::abi::invoke(nova::abi::require_function(image,ctor_name),{target_at,driver_at,address(initial.data()),0});
            std::array<uint32_t,8> target_vtable{}; target_vtable[4]=image.address(0x92c7f0);
            word(target_at)=address(target_vtable.data());
            effects[1]=1280; effects[2]=720; effects[3]=320; effects[4]=180;
            effects[0x90/4]=target_at; level[0x5b0/4]=address(effects.data());
            const auto level_slot=image.address(0x1156264,4); // Exact GetLevel global.
            const auto saved_level=word(level_slot); word(level_slot)=address(level.data());
            nova::abi::GuestHook effect(image,effect_bind_name,{0x55,0x89,0xe5,0x57,0x56,0x53},address(&effect_bind));
            original_effect_bind=effect.original();
            // Actual GPU backing for the two mandatory targets; disabled native
            // quarter/grading effects have no textures and no target requirement.
            GLuint color=0,framebuffer=0;
            glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
            glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,1280,720);
            glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
            word(target_at,0x28)=framebuffer;effects[0xa8/4]=target_at;
            check(native.available(),"disabled optional effects do not prevent resolution Apply");
            effects[3]=640; effects[4]=360; // PostEffects constructor: half-size, not Resize's quarter-size.
            const auto initial_refusal=native.unavailable_reason();
            if(!initial_refusal.empty()) std::printf("initial effects refusal: %s\n",initial_refusal.c_str());
            check(initial_refusal.empty(),"constructor half-size effects admit Apply with coherent full-size buffers");
            {
                std::array<uint32_t,12> native_color{},payload{},native_depth{};
                native_color[0x10/4]=address(payload.data()); native_color[0x24/4]=color;
                payload[0x24/4]=1; payload[0x2c/4]=8;
                word(target_at,0x2c)=0; word(target_at,0x30)=address(native_color.data());
                GLuint depth=0; glGenRenderbuffers(1,&depth); glBindRenderbuffer(GL_RENDERBUFFER,depth);
                glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,1280,720);
                native_depth[0x18/4]=depth;
                word(target_at,0x4c)=1; word(target_at,0x50)=address(native_depth.data());
                word(target_at,0x54)=1; word(target_at,0x58)=address(native_depth.data());
                word(target_at,0x5c)=0x01010000; // Color count=1, dirty=true.
                effects[0x98/4]=effects[0xb0/4]=address(native_depth.data());
                compile_native_attachments=true;
                const auto reason=native.unavailable_reason();
                if(!reason.empty()) std::printf("native depth/stencil refusal: %s\n",reason.c_str());
                check(reason.empty(),"original RTT bind compiles color and shared packed depth/stencil backing");
                compile_native_attachments=false;
                GLuint resized_color=0;
                glGenTextures(1,&resized_color); glBindTexture(GL_TEXTURE_2D,resized_color);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1024,576,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
                glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,resized_color,0);
                const auto stale_depth_status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
                std::printf("resized color with retained old depth: status=%u\n",stale_depth_status);
                check(stale_depth_status==GL_FRAMEBUFFER_UNSUPPORTED,"reproduce the user's ANGLE status 36061 with stale depth dimensions");
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
                glDeleteTextures(1,&resized_color);
                ++native_depth[0x18/4];
                check(native.unavailable_reason().find("depth mismatch")!=std::string::npos,
                      "wrong depth object identity reports depth even when its dimensions match");
                --native_depth[0x18/4];
                glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,0);
                check(native.unavailable_reason().find("depth mismatch")!=std::string::npos,
                      "missing required depth remains rejected on an otherwise complete FBO");
                glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
                check(native.available(),"restoring native depth restores admission");
                {
                    std::array<uint32_t,24> second_target{};
                    std::memcpy(second_target.data(),reinterpret_cast<void *>(target_at),sizeof second_target);
                    GLuint second_fbo=0; glGenFramebuffers(1,&second_fbo);
                    second_target[0x28/4]=second_fbo; second_target[0x5c/4]|=0x01000000;
                    effects[0xa8/4]=address(second_target.data());
                    native_depth[1]=8; native_depth[2]=0x2d;
                    native_depth[3]=1280; native_depth[4]=720; native_depth[5]=driver_at;
                    driver[(0x560+0x2d*20)/4]=GL_DEPTH24_STENCIL8;
                    effects[0x12c/4]=address(native_depth.data());
                    EffectResizeFixture fixture;
                    fixture.fx=address(effects.data()); fixture.color_object=address(native_color.data());
                    fixture.color=&color;
                    fixture.release=nova::abi::require_function(image,"_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE");
                    resize_fixture=&fixture;
                    nova::abi::GuestHook resize_hook(image,fx_resize_name,
                        {0x55,0x89,0xe5,0x57,0x56,0x53},address(&resize_effects));
                    compile_native_attachments=true;
                    check(native.available(),"two original full RTTs share coherent starting depth");
                    for(auto extent:{nova::platform::GuestExtent{1024,576},{1920,1080},{1280,720}}) {
                        auto changed=angle.prepare_target(extent.width,extent.height,&native.profile()); changed->activate();
                        const auto result=native.apply(extent);
                        if(!result.applied) std::printf("FX resize refusal: %s\n",result.error.c_str());
                        check(result.applied && native.current()==extent && native.available(),
                              "Apply/revert reconnect both RTTs to resized native depth/stencil");
                        check(word(effects[0x12c/4],4)==8 && native_depth[1]==1,
                              "new depth owns shared+two refs+four attachments; retired original retains only fixture sentinel");
                        for(size_t i=0;i+1<fixture.count;++i) check(fixture.depths[i][1]==1,"retired depth references released exactly");
                        changed->commit();
                    }
                    fixture.fail_color_once=true;
                    auto changed=angle.prepare_target(1024,576,&native.profile()); changed->activate();
                    const auto failed=native.apply({1024,576});
                    check(!failed.applied && native.current()==nova::platform::GuestExtent{1280,720} && native.available(),
                          "failed color verification recovers previous extent with newly reconnected depth");
                    check(word(effects[0x12c/4],4)==8,"recovery depth has exactly the live native owners");
                    for(size_t i=0;i+1<fixture.count;++i) check(fixture.depths[i][1]==1,"failed/recovery depths retire all native references");
                    changed->rollback();
                    compile_native_attachments=false; resize_fixture=nullptr;
                    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,0);
                    glDeleteFramebuffers(1,&second_fbo);
                    for(size_t i=0;i<fixture.count;++i) glDeleteRenderbuffers(1,&fixture.depths[i][6]);
                    effects[0xa8/4]=target_at; effects[0x12c/4]=0;
                }
                effects[0x98/4]=effects[0xb0/4]=0;
                glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,0);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,0);
                glDeleteRenderbuffers(1,&depth);
            }
            for(const int divisor:{2,4}) {
                const std::array<int32_t,2> reduced_size{1280/divisor,720/divisor};
                effects[3]=reduced_size[0]; effects[4]=reduced_size[1];
                std::array<uint32_t,24> reduced_target{};
                nova::abi::invoke(nova::abi::require_function(image,ctor_name),
                    {address(reduced_target.data()),driver_at,address(reduced_size.data()),0});
                reduced_target[0]=address(target_vtable.data());
                GLuint reduced_color=0,reduced_framebuffer=0;
                glGenTextures(1,&reduced_color); glBindTexture(GL_TEXTURE_2D,reduced_color);
                glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,reduced_size[0],reduced_size[1]);
                glGenFramebuffers(1,&reduced_framebuffer); glBindFramebuffer(GL_FRAMEBUFFER,reduced_framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,reduced_color,0);
                reduced_target[0x28/4]=reduced_framebuffer;
                for(unsigned offset:{0xc0u,0xd8u,0xf0u}) {
                    effects[offset/4]=address(reduced_target.data()); effects[(offset+4)/4]=reduced_color;
                }
                check(native.available(),"all active reduced targets match native constructor/resize metadata and actual GPU storage");
                effects[3]=divisor==2 ? 320 : 640; effects[4]=divisor==2 ? 180 : 360;
                check(!native.available(),"otherwise-valid reduced metadata cannot conceal mismatched target storage");
                effects[3]=reduced_size[0]; effects[4]=reduced_size[1];
                ++reduced_target[0xc/4];
                check(!native.available(),"native target dimensions remain checked independently");
                --reduced_target[0xc/4];
                glBindTexture(GL_TEXTURE_2D,reduced_color); glDeleteTextures(1,&reduced_color);
                glGenTextures(1,&reduced_color); glBindTexture(GL_TEXTURE_2D,reduced_color);
                glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,16,16);
                glBindFramebuffer(GL_FRAMEBUFFER,reduced_framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,reduced_color,0);
                const auto storage_error=native.unavailable_reason();
                std::printf("storage refusal: %s\n",storage_error.c_str());
                check(storage_error.find("color mismatch")!=std::string::npos &&
                      storage_error.find("16x16")!=std::string::npos,
                      "wrong actual GPU storage reports color and its measured dimensions");
                for(unsigned offset:{0xc0u,0xd8u,0xf0u}) effects[offset/4]=effects[(offset+4)/4]=0;
                glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&reduced_framebuffer);
                glDeleteTextures(1,&reduced_color);
            }
            effects[3]=640; effects[4]=180;
            check(!native.available() && native.unavailable_reason().find("640x180")!=std::string::npos,
                  "mixed-axis scaling is rejected and reports the actual reduced pair");
            effects[3]=320; effects[4]=180;
            for(unsigned offset:{0xc0u,0xd8u,0xf0u,0x108u}) {
                effects[(offset+4)/4]=color;
                check(!native.available(),"enabled optional effect still requires its valid target");
                effects[(offset+4)/4]=0;
            }
            effects[0xa8/4]=0;
            check(!native.available(),"a missing mandatory full target still blocks Apply");
            effects[0xa8/4]=target_at;
            glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&color);
            const auto calls=viewport_calls;
            poison_effect_target=target_at; replace_owner=true; effect_target_poisoned=false;
            bool stopped=false;
            try { (void)native.available(); }
            catch(const std::runtime_error &error) {
                stopped=std::strcmp(error.what(),"Fatal: native render owner changed during effects verification")==0;
            }
            check(stopped && effect_target_poisoned && viewport_calls==calls+1 && word(owner_slot)==replacement_device,
                  "effects bind owner replacement is fatal before cached RTT reads or old default cleanup callback");
            word(owner_slot)=address(device.data()); word(level_slot)=saved_level;
        }
        const auto calls=viewport_calls;
        auto gpu=angle.prepare_target(1600,900,&native.profile()); gpu->activate(); replace_owner=true;
        bool stopped=false;
        try { (void)native.apply({1600,900}); }
        catch(const std::runtime_error &error) {
            stopped=std::strcmp(error.what(),"Fatal: native render owner changed before recovery")==0;
        }
        check(stopped && viewport_calls==calls+1 && word(owner_slot)==replacement_device &&
              word(image.address(0x11437f0,4))==1280,
              "callback owner replacement stops before old-owner publication or recovery mutation");
        word(owner_slot)=address(device.data()); gpu->rollback();
        std::puts("native Apply kernel: admitted catalogue actual target/global/touch/GL storage+viewport, same context and boundary negatives passed");
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"native Apply kernel: %s\n",error.what()); return 1; }
}
