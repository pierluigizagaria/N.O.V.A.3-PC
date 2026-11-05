#include "gles/native_texture_filtering.h"
#include "platform/angle.h"
#include "platform/profile.h"
#include "platform/fov_config.h"
#include "audio/opensl.h"
#include "bionic/runtime.h"
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace {
constexpr auto name="_ZNK6glitch5video15CCommonGLDriverINS0_21CProgrammableGLDriverINS0_30CProgrammableShaderHandlerBaseINS0_18CGLSLShaderHandlerEEEEENS0_6detail33CProgrammableGLFunctionPointerSetEE8CTexture16updateParametersEv";
void check(bool ok,const char *what) { if(!ok) throw std::runtime_error(what); }
uint32_t ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
uint32_t &word(void *p,size_t offset) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p)+offset); }
struct Sampler {
    alignas(4) std::array<uint8_t,0x40> texture{},state{};
    alignas(4) std::array<uint8_t,0x960> driver{};
    Sampler(float maximum) {
        word(texture.data(),0x10)=ptr(state.data());word(state.data(),0x14)=ptr(driver.data());
        driver[0xe7]=maximum>=1;word(driver.data(),0x548)=std::bit_cast<uint32_t>(maximum);
    }
    void apply(const nova::ElfImage &image,unsigned filter=3,unsigned mips=5,unsigned dirty=0x108,unsigned type=1,float af=2) {
        word(state.data(),0x24)=type|(filter<<14);
        *reinterpret_cast<uint16_t *>(state.data()+0x2a)=static_cast<uint16_t>(dirty);
        state[0x2f]=static_cast<uint8_t>(mips);word(state.data(),0x30)=std::bit_cast<uint32_t>(af);
        const auto before=state;
        nova::abi::invoke(nova::abi::require_function(image,name),{ptr(texture.data())});
        for(size_t i=0;i<state.size();++i) if(i!=0x2a && i!=0x2b)
            check(state[i]==before[i],"native sampler metadata and resource ownership unchanged");
        check((*reinterpret_cast<uint16_t *>(state.data()+0x2a)&0x3ff8)==0,"original sampler consumes dirty bits");
    }
};
GLint min_filter(GLenum target=GL_TEXTURE_2D) { GLint value=0;glGetTexParameteriv(target,GL_TEXTURE_MIN_FILTER,&value);return value; }
float anisotropy() { GLfloat value=0;glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_ANISOTROPY_EXT,&value);return value; }
GLuint shader(GLenum type,const char *source) {
    auto s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);check(ok!=0,"GPU fixture shader compilation");return s;
}
GLuint program(bool lod) {
    auto v=shader(GL_VERTEX_SHADER,"#version 300 es\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}");
    auto f=shader(GL_FRAGMENT_SHADER,lod ?
        "#version 300 es\nprecision highp float;uniform sampler2D tex;in vec2 uv;out vec4 c;void main(){c=textureLod(tex,uv,1.5);}" :
        "#version 300 es\nprecision highp float;uniform sampler2D tex;in vec2 uv;out vec4 c;void main(){c=texture(tex,uv*vec2(16,1));}");
    auto p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);check(ok!=0,"GPU fixture program link");
    glDeleteShader(v);glDeleteShader(f);return p;
}
std::array<uint8_t,4> sample(GLuint p) {
    glUseProgram(p);glDrawArrays(GL_TRIANGLES,0,3);std::array<uint8_t,4> pixel{};
    glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());return pixel;
}
int contrast(GLuint p) {
    glUseProgram(p);glDrawArrays(GL_TRIANGLES,0,3);std::array<uint8_t,64*4> pixels{};
    glReadPixels(32,0,1,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());int low=255,high=0;
    for(size_t i=0;i<pixels.size();i+=4) {low=std::min(low,int(pixels[i]));high=std::max(high,int(pixels[i]));}
    return high-low;
}
void gpu(const nova::ElfImage &image,nova::platform::Angle &angle) {
    check(angle.make_current(0),"primary context");
    const float maximum=angle.main_target().max_anisotropy;
    Sampler sampler(maximum);
    GLuint texture;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    for(int level=0,side=16;side;side/=2,++level) {
        std::vector<uint8_t> pixels(size_t(side)*side*4,0);
        for(size_t i=0;i<pixels.size();i+=4) {pixels[i+(level==1?1:2)]=255;pixels[i+3]=255;}
        glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,side,side,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    }
    const auto blend=program(true);
    sampler.apply(image,3,5,0x108,1,1);const auto original=sample(blend);
    check(original[1]<5 || original[2]<5,"native nearest-mip path selects one level");
    for(int quality:{0,1,2}) {
        nova::gles::NativeTextureFiltering policy(image,quality);
        bool rejected=false;try {nova::gles::NativeTextureFiltering duplicate(image);} catch(const std::runtime_error &) {rejected=true;}
        check(rejected,"duplicate filtering owner rejected");
        sampler.apply(image);
        check(min_filter()==(quality?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR_MIPMAP_NEAREST),"native minification choice reaches GPU");
        if(maximum>=1) check(anisotropy()==(quality==2?std::min(16.f,maximum):std::min(2.f,maximum)),"GPU anisotropy honors choice and capability");
        sampler.apply(image,3,5,0x108,1,1);const auto pixel=sample(blend);
        if(quality==1) check(pixel[1]>=120 && pixel[1]<=135 && pixel[2]>=120 && pixel[2]<=135,"GPU blends adjacent original mip levels with anisotropy disabled");
        for(auto filter:{0u,1u,2u,4u}) {
            sampler.apply(image,filter);
            constexpr std::array<int,5> modes{GL_NEAREST,GL_LINEAR,GL_NEAREST_MIPMAP_NEAREST,GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR};
            check(min_filter()==modes[filter],"authored nearest and non-mip filters preserved");
            if(maximum>=1) check(anisotropy()==std::min(2.f,maximum),"retire forced anisotropy on sampler mode change");
        }
        sampler.apply(image,3,1);check(min_filter()==GL_LINEAR_MIPMAP_NEAREST,"single-level UI/RTT resource excluded");
        sampler.apply(image,3,5);
        const auto retained=min_filter();sampler.apply(image,1,1,0);
        check(min_filter()==retained,"clean native sampler does not generate override writes");
        // Simulated missing/limited extension capability; no unsupported extra token.
        auto &context=angle.main_target();context.max_anisotropy=0;sampler.apply(image);
        check(min_filter()==(quality?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR_MIPMAP_NEAREST),"trilinear survives missing anisotropy");
        context.max_anisotropy=maximum;
        if(maximum>=4) {context.max_anisotropy=4;sampler.apply(image);check(anisotropy()==(quality==2?4.f:2.f),"quality clamps to lower GPU maximum");context.max_anisotropy=maximum;}
        GLuint cube=0;glGenTextures(1,&cube);glBindTexture(GL_TEXTURE_CUBE_MAP,cube);
        sampler.apply(image,3,5,8,3);check(min_filter(GL_TEXTURE_CUBE_MAP)==GL_LINEAR_MIPMAP_NEAREST,"cube samplers remain original");
        glDeleteTextures(1,&cube);
        nova::gles::set_context(nullptr);sampler.apply(image);
        check(min_filter()==GL_LINEAR_MIPMAP_NEAREST,"no owned context forwards native sampler without overrides");
        nova::gles::set_context(&context);
        check(glGetError()==GL_NO_ERROR,"all sampler paths leave valid GLES state");
    }
    sampler.apply(image);check(min_filter()==GL_LINEAR_MIPMAP_NEAREST,"hook removal restores native sampler path");
    // Perspective-like elongated footprint: fine stripes along the short axis.
    std::vector<uint8_t> stripes(256*256*4,255);
    for(int y=0;y<256;++y) for(int x=0;x<256;++x) for(int c=0;c<3;++c) stripes[size_t(y*256+x)*4+c]=(y/4)%2?255:0;
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,stripes.data());glGenerateMipmap(GL_TEXTURE_2D);
    const auto oblique=program(false);sampler.apply(image,3,9);const int native_contrast=contrast(oblique);
    {nova::gles::NativeTextureFiltering policy(image,2);sampler.apply(image,3,9);const int enhanced=contrast(oblique);
     std::printf("anisotropy maximum=%.0f; oblique stripe contrast native=%d enhanced=%d\n",maximum,native_contrast,enhanced);
     if(maximum>=8) check(enhanced>native_contrast+20,"GPU anisotropy retains detail lost by native oblique minification");}
    check(angle.make_current(1),"loading context");
    {nova::gles::NativeTextureFiltering policy(image,2);glBindTexture(GL_TEXTURE_2D,texture);sampler.apply(image,3,9);glFinish();}
    check(angle.make_current(0),"restore main context");glBindTexture(GL_TEXTURE_2D,texture);
    check(min_filter()==GL_LINEAR_MIPMAP_LINEAR,"loading context publishes enhanced shared sampler");
    // Fresh texture names have no host side cache to inherit.
    glDeleteTextures(1,&texture);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    check(min_filter()==GL_NEAREST_MIPMAP_LINEAR,"fresh texture retains GLES defaults");
    glDeleteTextures(1,&texture);glUseProgram(0);glDeleteProgram(blend);glDeleteProgram(oblique);
    check(glGetError()==GL_NO_ERROR,"GPU sampling and context lifecycle leave no errors");
}
void config(const std::filesystem::path &fixture) {
    using namespace nova::platform;
    auto file=fixture.parent_path()/"texture-quality-test.conf";
    struct Cleanup {std::filesystem::path file;~Cleanup(){std::error_code ec;std::filesystem::remove(file,ec);}} cleanup{file};
    std::filesystem::path obb;
    check(nova::profile::Config{}.lod_distance==4 && nova::profile::Config{}.texture_filtering==2,"best settings are defaults");
    for(int lod=1;lod<=4;++lod) for(int texture=0;texture<3;++texture) {
        {std::ofstream out(file);out<<"# keep\nworld_fov = 90\nui_scale = 70\nlod_distance = 2\nlod_distance = 3\ntexture_filtering = 1\n";}
        check(save_graphics_quality_config(file,{lod,texture}).saved,"atomic quality-pair save");
        auto loaded=nova::profile::read_config(file,obb);
        check(loaded.lod_distance==lod && loaded.texture_filtering==texture && loaded.world_fov==90.f && loaded.ui_scale==70,"quality restart roundtrip preserves other preferences");
        check(save_fov_config(file,{{95.f},{90.f}},1).saved,"optical edits accept and preserve quality keys");
        loaded=nova::profile::read_config(file,obb);
        check(loaded.lod_distance==lod && loaded.texture_filtering==texture,"other settings writers preserve selected graphics quality");
    }
    for(auto bad:{"-1","3","nan","1junk","1.5"}) {
        {std::ofstream out(file);out<<"texture_filtering = "<<bad<<'\n';}
        bool rejected=false;try {(void)nova::profile::read_config(file,obb);}catch(const std::runtime_error &){rejected=true;}
        check(rejected,"invalid texture quality rejected");
    }
}
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    const bool real=std::strcmp(argv[2],"--real")==0;
    if(real && !std::filesystem::exists(argv[1]))return 77;
    try {
        if(!real) config(argv[1]);
        nova::ImportRegistry imports;nova::gles::register_imports(imports);
        if(real) {nova::bionic::register_boot(imports);nova::audio::register_opensl(imports);}
        nova::bionic::CxaRegistry cxa(imports);nova::ElfImage image(argv[1],imports);
        nova::platform::Angle angle("d3d11");check(angle.create(nullptr,64,64,1)==1,"shared GPU contexts");
        const auto address=nova::abi::require_function(image,name);std::array<uint8_t,6> before{};std::memcpy(before.data(),reinterpret_cast<void *>(address),6);
        gpu(image,angle);check(!std::memcmp(before.data(),reinterpret_cast<void *>(address),6),"exact native hook restoration");
        std::puts("texture filtering: native sampler, GPU pixels, exclusions, capability, context and settings PASS");return 0;
    }catch(const std::exception &e){std::fprintf(stderr,"texture filtering: %s\n",e.what());return 1;}
}
