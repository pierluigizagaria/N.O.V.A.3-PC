#include "abi/guest_hook.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include "platform/angle.h"
#include "platform/guest_extent.h"
#include <GLES3/gl3.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
uint32_t address(auto *value) { return reinterpret_cast<uint32_t>(value); }
uint32_t &word(uint32_t value,uint32_t offset=0) { return *reinterpret_cast<uint32_t *>(value+offset); }
uint32_t __cdecl ignore_log(uint32_t,...) { return 0; }
uint32_t __cdecl thread_zero() { return 0; }
void inspect_present(void *user,int width,int height) {
    const auto extent=*static_cast<nova::platform::GuestExtent *>(user);
    check(width==1280 && height==720,"presentation uses independent output extent");
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    unsigned char edge[4]{};
    glReadPixels(1279,360,1,1,GL_RGBA,GL_UNSIGNED_BYTE,edge);
    const int green=extent.width==1600 ? 223 : 255;
    check(std::abs(int(edge[1])-green)<=2 && std::abs(int(edge[0])-(255-green))<=2,
          "actual native-size edge resamples from1600, rather than a1280 source");
}
constexpr auto resize_name="_ZN3glf19AndroidResizeScreenEii";
constexpr auto log_name="_ZN3glf7Console7PrintlnEPKcz";
constexpr auto target_name="_ZN6glitch5video13IRenderTargetC2EPNS0_12IVideoDriverERKNS_4core11dimension2dIiEENS0_22E_ANTIALIASING_SETTINGE";
}
int main(int argc,char **argv) {
    if(argc!=2 && argc!=3) return 2;
    if(!std::filesystem::exists(argv[1])) return 77;
    try {
        nova::ImportRegistry imports;
        nova::bionic::register_boot(imports);
        nova::audio::register_opensl(imports);
        nova::gles::register_imports(imports);
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1],imports);
        // No initializers, actual App construction, resources, profile or network.
        // Redirect only logging/thread-id dependencies of this isolated native body.
        nova::abi::GuestCall log0(image,resize_name,0x43,log_name,address(&ignore_log));
        nova::abi::GuestCall log1(image,resize_name,0x79,log_name,address(&ignore_log));
        nova::abi::GuestCall log2(image,resize_name,0x9e,log_name,address(&ignore_log));
        nova::abi::GuestCall tid(image,resize_name,0xc1,"_ZN3glf6Thread21GetSequentialThreadIdEv",address(&thread_zero));
        const auto resize=nova::abi::require_function(image,resize_name);
        const auto creation=nova::abi::require_function(image,"_ZNK3glf3App19GetCreationSettingsEv");
        const auto target_ctor=nova::abi::require_function(image,target_name);
        const auto app_impl=image.symbol("_ZN3glf8gAppImplE");
        const auto surface=image.symbol("_ZZN3glf19AndroidResizeScreenEiiE16isSurfaceCreated");
        check(app_impl && surface,"native lifecycle globals are named and mapped");
        for(auto extent:{nova::platform::GuestExtent{},nova::platform::GuestExtent{1600,900}}) {
            check(nova::platform::startup_guest_extent(extent).active==extent,"supported production selection");
            std::vector<uint32_t> app(0x18738/4),impl(4),driver(4);
            std::array<uint32_t,16> target{};
            const auto app_address=address(app.data());
            impl[1]=app_address; word(app_impl)=address(impl.data());
            *reinterpret_cast<uint8_t *>(surface)=0;
            const auto supplied=argc==3 && extent.width==1600 ? nova::platform::GuestExtent{} : extent;
            nova::abi::invoke(resize,{static_cast<uint32_t>(supplied.width),static_cast<uint32_t>(supplied.height)});
            const auto settings=nova::abi::invoke(creation,{app_address});
            check(settings==app_address+0x17adc && word(settings,8)==static_cast<uint32_t>(extent.width) &&
                  word(settings,12)==static_cast<uint32_t>(extent.height),
                  "actual native first resize publishes the actual creation extent");
            // The real factory forwards settings+8 directly as its dimension pair.
            nova::abi::invoke(target_ctor,{address(target.data()),address(driver.data()),settings+8,0});
            check(target[3]==static_cast<uint32_t>(extent.width) && target[4]==static_cast<uint32_t>(extent.height) &&
                  target[5]==0 && target[6]==0 && target[7]==target[3] && target[8]==target[4],
                  "actual native render-target dimensions and full rectangle agree");
            {
                nova::platform::Angle angle("d3d11"); angle.create(nullptr,extent.width,extent.height,0);
                auto &fbo=angle.main_target();
                check(fbo.width==extent.width && fbo.height==extent.height,"actual GLES virtual target extent");
                for(auto attachment:{fbo.color,fbo.depth_stencil}) {
                    glBindRenderbuffer(GL_RENDERBUFFER,attachment);
                    GLint width=0,height=0;
                    glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_WIDTH,&width);
                    glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_HEIGHT,&height);
                    check(width==extent.width && height==extent.height,"actual color/depth storage dimensions");
                }
                glBindFramebuffer(GL_FRAMEBUFFER,fbo.framebuffer);
                glDisable(GL_SCISSOR_TEST); glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
                glEnable(GL_SCISSOR_TEST); glScissor(extent.width-1,0,1,extent.height);
                glClearColor(0,1,0,1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
                angle.presentation_size(1280,720);
                angle.present(inspect_present,&extent);
                check(glGetError()==GL_NO_ERROR,"actual native-size storage is valid");
            }
            nova::abi::invoke(resize,{640,360});
            check(word(app_address,0x17a74)==640 && word(app_address,0x17a78)==360 &&
                  word(settings,8)==static_cast<uint32_t>(extent.width) && word(settings,12)==static_cast<uint32_t>(extent.height),
                  "subsequent native resize cannot replace immutable startup creation dimensions");
            word(app_impl)=0;
        }
        check(nova::platform::startup_guest_extent({1920,1080}).active==nova::platform::GuestExtent{},
              "native1920 remains blocked rather than disguised as supported");
        std::puts("native render extent: first-resize creation/target and real GLES storage agree");
        return 0;
    } catch(const std::exception &error) {
        std::fprintf(stderr,"native render extent: %s\n",error.what()); return 1;
    }
}
