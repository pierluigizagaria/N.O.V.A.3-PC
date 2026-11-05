#include "platform/native_render_extent.h"
#include "camera/native_world_fov.h"
#include "abi/guest_hook.h"
#include <windows.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace nova::platform {
namespace {
uint32_t addr(const void *p) { return reinterpret_cast<uint32_t>(p); }
bool memory(uint32_t p,size_t bytes) {
    if(!p || uint64_t(p)+bytes>UINT32_MAX) return false;
    MEMORY_BASIC_INFORMATION area{};
    if(!VirtualQuery(reinterpret_cast<void *>(p),&area,sizeof area) || area.State!=MEM_COMMIT ||
       (area.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
    return uint64_t(p)+bytes<=uint64_t(addr(area.BaseAddress))+area.RegionSize;
}
uint32_t get(uint32_t p,uint32_t offset=0) {
    if(uint64_t(p)+offset>UINT32_MAX || !memory(p+offset,4)) throw std::runtime_error("Native render object is unavailable");
    return *reinterpret_cast<const uint32_t *>(p+offset);
}
void put(uint32_t p,uint32_t offset,uint32_t value) {
    if(uint64_t(p)+offset>UINT32_MAX || !memory(p+offset,4)) throw std::runtime_error("Native render object is unavailable");
    *reinterpret_cast<uint32_t *>(p+offset)=value;
}
void short_pair(uint32_t p,uint32_t offset,int w,int h) {
    if(!memory(p+offset,4)) throw std::runtime_error("Native touch dimensions are unavailable");
    const std::array<int16_t,2> pair{static_cast<int16_t>(w),static_cast<int16_t>(h)};
    std::memcpy(reinterpret_cast<void *>(p+offset),pair.data(),sizeof pair);
}
bool supported(GuestExtent e) {
    return supported_render_extent(e);
}
constexpr auto resize_name="_ZN3glf19AndroidResizeScreenEii";
constexpr auto driver_resize_name="_ZN6glitch5video12IVideoDriver8onResizeERKNS_4core11dimension2dIiEE";
constexpr auto fx_resize_name="_ZN11PostEffects6ResizeEii";
constexpr auto touch_rect_name="_ZN15TouchScreenBase7setRectERKN6glitch4core4rectIsEE";
constexpr auto level_name="_ZN6CLevel8GetLevelEv";
constexpr auto depth_target_name="_ZN6glitch5video19CCommonGLDriverBase17CRenderTargetBase9setTargetENS0_13IRenderTarget17E_ATTACHMENT_TYPEERKN5boost13intrusive_ptrINS0_13IRenderBufferEEEj";
constexpr auto release_name="_ZN6glitch21intrusive_ptr_releaseEPKNS_17IReferenceCountedE";
struct NativeSnapshot {
    uint32_t app=0,device=0,driver=0,target=0,touch=0,level=0,fx=0;
    GuestExtent extent;
};
struct GlBindings {
    GLint draw=0,read=0,renderbuffer=0,texture=0;
    GlBindings() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);
        glGetIntegerv(GL_RENDERBUFFER_BINDING,&renderbuffer);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
    }
    ~GlBindings() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw); glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
        glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer); glBindTexture(GL_TEXTURE_2D,texture);
    }
};
struct NativeOwnerLost final:std::runtime_error {
    NativeOwnerLost():std::runtime_error("Fatal: native render owner changed during effects verification") {}
};
struct TextureSize { GLint width=0,height=0; bool queried=false; };
TextureSize texture_size(GLuint name,GLint level=0) {
    using Query=void(GL_APIENTRY *)(GLenum,GLint,GLenum,GLint *);
    static const auto query=reinterpret_cast<Query>(eglGetProcAddress("glGetTexLevelParameterivANGLE"));
    if(!query || !name || !glIsTexture(name)) return {};
    TextureSize size;
    glBindTexture(GL_TEXTURE_2D,name);
    query(GL_TEXTURE_2D,level,0x1000,&size.width); // GL_TEXTURE_WIDTH, GL_ANGLE_get_tex_level_parameter.
    query(GL_TEXTURE_2D,level,0x1001,&size.height); // GL_TEXTURE_HEIGHT.
    size.queried=true;
    return size;
}
bool texture_dimensions(GLuint name,int width,int height) {
    const auto size=texture_size(name);
    return size.queried && size.width==width && size.height==height;
}
struct Attachment {
    GLint type=0,name=0,width=0,height=0,level=0;
    bool matches(int w,int h,bool required,GLuint expected,bool require_renderbuffer=false) const {
        if(type==GL_NONE) return !required;
        if(require_renderbuffer && (type!=GL_RENDERBUFFER || static_cast<GLuint>(name)!=expected)) return false;
        return (type==GL_TEXTURE || type==GL_RENDERBUFFER) && width==w && height==h;
    }
    std::string describe() const {
        const char *kind=type==GL_NONE ? "none" : type==GL_TEXTURE ? "texture" : type==GL_RENDERBUFFER ? "renderbuffer" : "unknown";
        return std::string(kind)+"#"+std::to_string(name)+" "+std::to_string(width)+"x"+std::to_string(height)+
            " mip="+std::to_string(level);
    }
};
Attachment attachment(GLenum point) {
    Attachment result;
    auto &[type,name,w,h,level]=result;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,point,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
    if(type==GL_NONE) return result;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,point,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&name);
    if(type==GL_TEXTURE) {
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,point,GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL,&level);
        // Preserve the existing level-zero admission policy until an original
        // nonzero-level RTT is evidenced; report the actual attached level.
        const auto size=texture_size(static_cast<GLuint>(name)); w=size.width; h=size.height;
    } else if(type==GL_RENDERBUFFER) {
        glBindRenderbuffer(GL_RENDERBUFFER,static_cast<GLuint>(name));
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&w);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&h);
    }
    return result;
}
bool target_dimensions(uint32_t target,GuestExtent extent) {
    return get(target,0xc)==static_cast<uint32_t>(extent.width) &&
        get(target,0x10)==static_cast<uint32_t>(extent.height) && !get(target,0x14) && !get(target,0x18) &&
        get(target,0x1c)==static_cast<uint32_t>(extent.width) && get(target,0x20)==static_cast<uint32_t>(extent.height);
}
}
struct NativeRenderExtent::Impl {
    const ElfImage &image;
    camera::NativeWorldFov &fov;
    NativeDeviceProfile profile;
    uint32_t resize=0,driver_resize=0,fx_resize=0,touch_rect=0,level_get=0;
    uint32_t depth_target=0,release=0;
    uint32_t lane=0;
    Impl(const ElfImage &i,const std::filesystem::path &path,camera::NativeWorldFov &lens)
        :image(i),fov(lens),profile(i,path) {
        auto exact=[&](const char *name,uint32_t va,size_t bytes) {
            const auto function=abi::require_function(image,name);
            const auto source=image.source_bytes();
            if(function!=image.address(va,bytes) || va>source.size() || bytes>source.size()-va ||
               std::memcmp(reinterpret_cast<const void *>(function),source.data()+va,bytes))
                throw std::runtime_error("Native render function identity mismatch");
            return function;
        };
        resize=exact(resize_name,0xaebb50,214);
        driver_resize=exact(driver_resize_name,0x89ea40,414);
        fx_resize=exact(fx_resize_name,0x71b870,2539);
        touch_rect=exact(touch_rect_name,0x3d4340,42);
        level_get=exact(level_name,0x370fe0,22);
        depth_target=exact(depth_target_name,0x8d5270,155);
        release=exact(release_name,0xe1850,47);
    }
    NativeSnapshot snapshot() const {
        NativeSnapshot s;
        s.device=get(image.address(0x115683c,4)); // Named g_device; Init's authoritative owner.
        s.driver=get(s.device,8); s.app=get(s.device,0x5c);
        if(s.app!=get(image.address(0x115f818,4)) || get(s.driver)!=image.address(0x1124cec))
            throw std::runtime_error("Native render owner/driver is not the admitted primary device");
        const auto begin=get(s.driver,0x114),end=get(s.driver,0x118);
        if(end!=begin+4) throw std::runtime_error("Native render target stack is not at a frame boundary");
        s.target=get(begin);
        if(get(s.target)!=image.address(0x1126fa8) || (get(s.driver,0x2b4)&1))
            throw std::runtime_error("Native render orientation/target is not supported");
        s.extent={static_cast<int>(get(s.target,0xc)),static_cast<int>(get(s.target,0x10))};
        if(!supported(s.extent) || !target_dimensions(s.target,s.extent) ||
           get(s.app,0x17a74)!=static_cast<uint32_t>(s.extent.width) ||
           get(s.app,0x17a78)!=static_cast<uint32_t>(s.extent.height) ||
           get(image.address(0x11437f0,4))!=static_cast<uint32_t>(s.extent.width) ||
           get(image.address(0x11437f4,4))!=static_cast<uint32_t>(s.extent.height))
            throw std::runtime_error("Native render dimensions are not coherent");
        s.touch=get(image.address(0x11568c4,4));
        if(get(s.touch)!=image.address(0x1107e28)) throw std::runtime_error("Native touch owner changed");
        std::array<int16_t,8> touch{};
        if(!memory(s.touch+8,sizeof touch)) throw std::runtime_error("Native touch bounds unavailable");
        std::memcpy(touch.data(),reinterpret_cast<const void *>(s.touch+8),sizeof touch);
        if(touch[0] || touch[1] || touch[2]!=s.extent.width || touch[3]!=s.extent.height ||
           touch[6]!=s.extent.width || touch[7]!=s.extent.height)
            throw std::runtime_error("Native touch bounds do not match the primary render extent");
        s.level=abi::invoke(level_get);
        if(s.level) s.fx=get(s.level,0x5b0);
        return s;
    }
    bool owners(const NativeSnapshot &s) const {
        // Reacquire authoritative roots first. Never dereference a cached old
        // object after a native callback has published a replacement owner.
        if(get(image.address(0x115683c,4))!=s.device ||
           get(image.address(0x115f818,4))!=s.app ||
           get(image.address(0x11568c4,4))!=s.touch || abi::invoke(level_get)!=s.level) return false;
        if(get(s.device,8)!=s.driver || get(s.device,0x5c)!=s.app) return false;
        const auto begin=get(s.driver,0x114);
        return get(s.driver,0x118)==begin+4 && get(begin)==s.target &&
            (!s.level || get(s.level,0x5b0)==s.fx);
    }
    bool effects(const NativeSnapshot &s,GuestExtent e,bool resized=false) const {
        auto require_owner=[&] { if(!owners(s)) throw NativeOwnerLost{}; };
        require_owner();
        const auto fx=s.fx;
        if(!fx) return true; // A menu has no current CLevel/PostEffects owner.
        if(get(fx,4)!=static_cast<uint32_t>(e.width) || get(fx,8)!=static_cast<uint32_t>(e.height))
            throw std::runtime_error("Effects resolution differs from the main target ("+
                std::to_string(get(fx,4))+"x"+std::to_string(get(fx,8))+" versus "+render_extent_name(e)+").");
        // Original constructor 73efee/73effe publishes half-size effects;
        // original Resize 71b8e6/71b8f7 publishes quarter-size effects. Both
        // states are valid at admission. After our Resize (including recovery)
        // require its exact quarter-size result, never accept a stale half-size.
        const GuestExtent reduced{static_cast<int>(get(fx,0xc)),static_cast<int>(get(fx,0x10))};
        if(reduced!=GuestExtent{e.width/4,e.height/4} &&
           (resized || reduced!=GuestExtent{e.width/2,e.height/2}))
            throw std::runtime_error("Effects reduced resolution is invalid ("+
                std::to_string(reduced.width)+"x"+std::to_string(reduced.height)+
                " for "+render_extent_name(e)+").");
        GlBindings saved;
        auto restore_default=[&] {
            // A native RTT bind may change authoritative roots through its
            // viewport callback. Never dereference or call the old default then.
            require_owner();
            abi::invoke(get(get(s.target),0x10),{s.target});
            require_owner();
        };
        bool result=false;
        try {
        result=[&] {
        // Native Resize always updates the two full targets. Reduced targets
        // and grading are conditional on their texture refs (+c4/+dc/+f4/+10c),
        // not mandatory resources. Disabled effects must not block Apply.
        for(const auto offset:{0x90u,0xa8u,0xc0u,0xd8u,0xf0u,0x108u}) {
            if(offset>=0xc0 && !get(fx,offset+4)) continue;
            const auto target=get(fx,offset);
            if(!target) throw std::runtime_error("Missing effects target at slot "+std::to_string(offset));
            const GuestExtent size=offset==0x108 ? GuestExtent{32,32} :
                offset<0xc0 ? e : reduced;
            if(!target_dimensions(target,size)) throw std::runtime_error("Effects target dimensions or viewport differ at slot "+std::to_string(offset));
            if(get(get(target),0x10)!=image.address(0x92c7f0)) throw std::runtime_error("Unsupported effects target at slot "+std::to_string(offset));
            // CRenderTarget uses+0x28 (CFramebuffer's+0x3c is a different class).
            // Its original bind commits lazy changed attachments after Resize.
            abi::invoke(get(get(target),0x10),{target});
            require_owner(); // Before target+28, depth owner or the next FX slot.
            const auto object=get(target,0x28);
            if(!object) throw std::runtime_error("Effects framebuffer is missing at slot "+std::to_string(offset));
            glBindFramebuffer(GL_FRAMEBUFFER,object);
            // Resize passes the full targets' optional IRenderBuffer refs at
            // FX+98/+b0. A color-only COMPLETE FBO cannot prove that depth owner.
            const auto depth=offset<0xc0 ? get(fx,offset+8) : 0;
            const auto depth_name=depth ? get(depth,0x18) : 0;
            const auto status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
            const auto color=attachment(GL_COLOR_ATTACHMENT0),depth_attachment=attachment(GL_DEPTH_ATTACHMENT),
                stencil=attachment(GL_STENCIL_ATTACHMENT);
            const char *failed=status!=GL_FRAMEBUFFER_COMPLETE ? "framebuffer" :
                !color.matches(size.width,size.height,true,0) ? "color" :
                !depth_attachment.matches(size.width,size.height,depth!=0,depth_name,depth!=0) ? "depth" :
                !stencil.matches(size.width,size.height,false,0) ? "stencil" : nullptr;
            if(failed) {
                std::fprintf(stderr,"render effects slot=0x%x framebuffer=%u status=0x%x expected=%dx%d "
                    "depth_owner=0x%x depth_name=%u color={%s} depth={%s} stencil={%s} "
                    "native_color=0x%x native_depth=0x%x native_stencil=0x%x\n",
                    offset,object,status,size.width,size.height,depth,depth_name,color.describe().c_str(),
                    depth_attachment.describe().c_str(),stencil.describe().c_str(),
                    get(target,0x30),get(target,0x50),get(target,0x58));
                const auto detail=failed==std::string("color") ? color.describe() :
                    failed==std::string("depth") ? depth_attachment.describe() :
                    failed==std::string("stencil") ? stencil.describe() : "status="+std::to_string(status);
                throw std::runtime_error("Effects "+std::string(failed)+" mismatch at slot "+std::to_string(offset)+
                    ": "+detail+"; expected "+std::to_string(size.width)+"x"+std::to_string(size.height));
            }
        }
        // Resize's optional shared full-size depth texture is also checked.
        // fillTextureDesc proves ITexture+14/+18; original GL RTT bind uses its
        // derived texture object name at+24. Do not omit this seventh resource.
        if(const auto texture=get(fx,0x130)) {
            if(get(texture,0x14)!=static_cast<uint32_t>(e.width) ||
               get(texture,0x18)!=static_cast<uint32_t>(e.height) ||
               !texture_dimensions(get(texture,0x24),e.width,e.height))
                throw std::runtime_error("Effects depth texture dimensions do not match");
        }
        return true;
        }();
        } catch(const NativeOwnerLost &) {
            throw; // Observed owner loss never permits old-object cleanup.
        } catch(...) {
            // Only stable owners permit native cleanup. On lost ownership even
            // cleanup is a forbidden old-object call; propagate fatal directly.
            require_owner();
            restore_default();
            throw;
        }
        restore_default();
        return result;
    }
    void reconnect_effect_depth(const NativeSnapshot &s,GuestExtent e) {
        // Original Resize replaces FX+12c, but reattaches the old RTT refs at
        // +98/+b0. Its setter rejects their old dimensions and leaves stale GPU
        // depth/stencil behind. Complete that ownership transition using the
        // original setter and intrusive release, for Apply AND recovery.
        if(!s.fx) return;
        auto require_owner=[&] { if(!owners(s)) throw NativeOwnerLost{}; };
        require_owner();
        const auto shared=get(s.fx,0x12c);
        const std::array<uint32_t,2> slots{0x90,0xa8};
        bool needed=false;
        for(auto slot:slots) needed|=get(s.fx,slot+8)!=0;
        if(!needed) return; // Native texture-depth/color-only paths retain their lifecycle.
        if(!shared || get(shared,0xc)!=static_cast<uint32_t>(e.width) ||
           get(shared,0x10)!=static_cast<uint32_t>(e.height) || get(shared,0x14)!=s.driver ||
           (get(shared,8)!=0x2a && get(shared,8)!=0x2d))
            throw std::runtime_error("Resized effects depth owner is invalid");
        std::array<uint32_t,2> kinds{};
        for(size_t i=0;i<slots.size();++i) {
            const auto old=get(s.fx,slots[i]+8);
            if(!old) continue;
            const auto target=get(s.fx,slots[i]);
            const auto stencil=get(target,0x58);
            if((get(target,0x4c)&0xffff)!=1 || get(target,0x50)!=old ||
               (stencil && (stencil!=old || (get(target,0x54)&0xffff)!=1)) ||
               get(old,8)!=get(shared,8) || get(old,0x14)!=s.driver)
                throw std::runtime_error("Effects depth attachment ownership is unsupported");
            kinds[i]=stencil ? 3u : 1u;
        }
        for(size_t i=0;i<slots.size();++i) {
            if(!kinds[i]) continue;
            require_owner();
            const auto target=get(s.fx,slots[i]),ref=s.fx+slots[i]+8,old=get(ref);
            if(old!=shared) {
                // Match native boost intrusive_ptr assignment: retain, publish,
                // release. Attachment references stay alive until native setter.
                InterlockedIncrement(reinterpret_cast<volatile LONG *>(shared+4));
                put(ref,0,shared);
                abi::invoke(release,{old});
                require_owner();
            }
            const bool attached=abi::invoke_bool(depth_target,{target,kinds[i],ref,0});
            require_owner();
            if(!attached || get(target,0x50)!=shared ||
               (kinds[i]==3 && get(target,0x58)!=shared))
                throw std::runtime_error("Could not reconnect resized effects depth");
        }
    }
    void mutate(const NativeSnapshot &s,GuestExtent e) {
        const std::array<int32_t,2> dimension{e.width,e.height};
        abi::invoke(resize,{static_cast<uint32_t>(e.width),static_cast<uint32_t>(e.height)});
        if(!owners(s)) throw std::runtime_error("Native render owner changed during window publication");
        abi::invoke(driver_resize,{s.driver,addr(dimension.data())});
        if(!owners(s)) throw std::runtime_error("Native render owner changed during driver resize");
        if(!target_dimensions(s.target,e)) throw std::runtime_error("Native driver resize was not applied");
        // Exact Init0x6b9364..0x6b93be publication, from the actual default target.
        const auto w=get(s.target,0xc),h=get(s.target,0x10);
        put(image.address(0x11437f0,4),0,std::max(w,h));
        put(image.address(0x11437f4,4),0,std::min(w,h));
        const std::array<int16_t,4> bounds{0,0,static_cast<int16_t>(e.width),static_cast<int16_t>(e.height)};
        abi::invoke(touch_rect,{s.touch,addr(bounds.data())});
        if(!owners(s)) throw std::runtime_error("Native render owner changed during touch resize");
        // Exact Android touch constructor normalization fields, alongside setRect.
        short_pair(s.touch,0x14,e.width,e.height);
        if(s.fx) abi::invoke(fx_resize,{s.fx,static_cast<uint32_t>(e.width),static_cast<uint32_t>(e.height)});
        if(!owners(s)) throw std::runtime_error("Native render owner changed during effects resize");
        reconnect_effect_depth(s,e);
        // runImpl's normal next-frame window comparison still posts the original
        // old/new resize event. No level, camera, context or creation record reset.
    }
};
NativeRenderExtent::NativeRenderExtent(const ElfImage &image,const std::filesystem::path &path,camera::NativeWorldFov &fov)
    :impl_(std::make_unique<Impl>(image,path,fov)) {}
NativeRenderExtent::~NativeRenderExtent()=default;
const NativeDeviceProfile &NativeRenderExtent::profile() const { return impl_->profile; }
bool NativeRenderExtent::available() const {
    return unavailable_reason().empty();
}
std::string NativeRenderExtent::unavailable_reason() const {
    try { const auto s=impl_->snapshot(); return impl_->effects(s,s.extent) ? std::string{} : "Effects buffers are not ready"; }
    catch(const NativeOwnerLost &) { throw; }
    catch(const std::exception &error) { return error.what(); }
}
GuestExtent NativeRenderExtent::current() const { return impl_->snapshot().extent; }
NativeRenderExtent::ApplyResult NativeRenderExtent::apply(GuestExtent next) {
    if(!supported(next)) return {false,"Unsupported native render resolution"};
    const auto thread=GetCurrentThreadId();
    if(impl_->lane && impl_->lane!=thread) return {false,"Render apply requires the owning GL lane"};
    impl_->lane=thread;
    NativeSnapshot old;
    try { old=impl_->snapshot(); if(!impl_->effects(old,old.extent)) return {false,"Current native effects buffers are not coherent"}; }
    catch(const NativeOwnerLost &) { throw; }
    catch(const std::exception &error) { return {false,error.what()}; }
    if(old.extent==next) return {true,{}};
    // genericDriverInit publishes GL_MAX_TEXTURE_SIZE into these two native
    // dimension limits (+64/+68). Preserve its own admission before FX mutation.
    if(static_cast<uint32_t>(next.width)>get(old.driver,0x64) ||
       static_cast<uint32_t>(next.height)>get(old.driver,0x68))
        return {false,"Resolution exceeds native texture limits"};
    try {
        if(!impl_->fov.prepare_render_extent(old.extent.width,old.extent.height,next.width,next.height))
            return {false,"Waiting for a current native camera lens before applying resolution"};
    } catch(const std::exception &error) { return {false,error.what()}; }
    try {
        impl_->mutate(old,next);
        const auto now=impl_->snapshot();
        if(now.device!=old.device || now.driver!=old.driver || now.target!=old.target ||
           now.app!=old.app || now.touch!=old.touch || now.level!=old.level || now.fx!=old.fx ||
           now.extent!=next || !impl_->effects(now,next,true) || !impl_->fov.verify_render_extent(true))
            throw std::runtime_error("Native render resource/lens verification failed");
    } catch(const NativeOwnerLost &) {
        throw; // Observed effects-owner loss never becomes a recovery attempt.
    } catch(const std::exception &failure) {
        const std::string message=failure.what();
        // Native ITexture::recreate destroys old backing before initialization;
        // this is checked recovery, not an atomic native allocation guarantee.
        if(!impl_->owners(old))
            throw std::runtime_error("Fatal: native render owner changed before recovery");
        impl_->mutate(old,old.extent);
        const auto restored=impl_->snapshot();
        if(restored.device!=old.device || restored.driver!=old.driver || restored.target!=old.target ||
           restored.app!=old.app || restored.touch!=old.touch || restored.extent!=old.extent ||
           restored.level!=old.level || restored.fx!=old.fx ||
           !impl_->effects(restored,old.extent,true) || !impl_->fov.verify_render_extent(false))
            throw std::runtime_error("Fatal: native render resolution recovery is unproved");
        impl_->fov.finish_render_extent(false);
        return {false,message};
    }
    impl_->fov.finish_render_extent(true);
    return {true,{}};
}
} // namespace nova::platform
