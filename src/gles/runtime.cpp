#include "gles/runtime.h"
#include "menu/rtt_backing_capture.h"
#include "ui/scale.h"
#include "bionic/runtime.h"
#include "platform/presentation.h"
#include "platform/native_device_profile.h"
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace nova::gles {
namespace {
thread_local Context *current = nullptr;
using Backing=nova::menu::RttBackingCapture;
using BackingOp=Backing::Op;
void error(GLenum code) {
    if (current && current->pending_error == GL_NO_ERROR)
        current->pending_error = code;
}
bool binding_query(GLenum name) {
    return name == GL_FRAMEBUFFER_BINDING || name == GL_READ_FRAMEBUFFER_BINDING;
}
bool default_bound(GLenum target) {
    if (!current ||
        (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER))
        return false;
    GLint value = 0;
    ::glGetIntegerv(target == GL_READ_FRAMEBUFFER ? GL_READ_FRAMEBUFFER_BINDING
                                                  : GL_DRAW_FRAMEBUFFER_BINDING,
                    &value);
    return static_cast<GLuint>(value) == current->framebuffer;
}
#define NOVA_GL_FORWARD(ret, name, count, params, args, target)                                         \
    ret __cdecl w_##name params { return ::target args; }
#define NOVA_GL_CUSTOM(ret, name, count, params, args, target) ret __cdecl w_##name params;
#include "gles_entries.inc"
#undef NOVA_GL_FORWARD
#undef NOVA_GL_CUSTOM

void __cdecl w_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    const GLuint physical=current && framebuffer==0 ? current->framebuffer : framebuffer;
    Backing::forward({BackingOp::bind_framebuffer,{target,framebuffer,physical}},
        [&] { ::glBindFramebuffer(target,physical); });
}
void __cdecl w_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    ::glDeleteFramebuffers(n, framebuffers);
    if (!current)
        return;
    // GLES deletion binds physical zero; restore our virtual default in both slots.
    for (auto [target, query] : {std::pair{GL_DRAW_FRAMEBUFFER, GL_DRAW_FRAMEBUFFER_BINDING},
                                 std::pair{GL_READ_FRAMEBUFFER, GL_READ_FRAMEBUFFER_BINDING}}) {
        GLint value = -1;
        ::glGetIntegerv(query, &value);
        if (value == 0)
            ::glBindFramebuffer(target, current->framebuffer);
    }
}
void __cdecl w_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                         GLuint renderbuffer) {
    if (default_bound(target)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    Backing::forward({BackingOp::framebuffer_renderbuffer,{target,attachment,renderbuffertarget,renderbuffer}},
        [&] { ::glFramebufferRenderbuffer(target,attachment,renderbuffertarget,renderbuffer); });
}
void __cdecl w_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                      GLint level) {
    if (default_bound(target)) {
        error(GL_INVALID_OPERATION);
        return;
    }
    Backing::forward({BackingOp::framebuffer_texture_2d,{target,attachment,textarget,texture,static_cast<uint32_t>(level)}},
        [&] { ::glFramebufferTexture2D(target,attachment,textarget,texture,level); });
}
// Passive scalar observations of original guest calls. No output-pointer reads,
// added GL queries or synthetic success/error checks. Disabled capture is TLS-null.
void __cdecl w_glActiveTexture(GLenum texture) {
    Backing::forward({BackingOp::active_texture,{texture}},[&] { ::glActiveTexture(texture); });
}
void __cdecl w_glBindTexture(GLenum target,GLuint texture) {
    Backing::forward({BackingOp::bind_texture,{target,texture}},[&] { ::glBindTexture(target,texture); });
}
void __cdecl w_glTexImage2D(GLenum target,GLint level,GLint internalformat,GLsizei width,GLsizei height,
                          GLint border,GLenum format,GLenum type,const void *pixels) {
    Backing::forward({BackingOp::tex_image_2d,{target,static_cast<uint32_t>(level),static_cast<uint32_t>(internalformat),
        static_cast<uint32_t>(width),static_cast<uint32_t>(height),static_cast<uint32_t>(border),format,type,pixels ? 1u : 0u}},
        [&] { ::glTexImage2D(target,level,internalformat,width,height,border,format,type,pixels); });
}
void __cdecl w_glBindRenderbuffer(GLenum target,GLuint renderbuffer) {
    Backing::forward({BackingOp::bind_renderbuffer,{target,renderbuffer}},[&] { ::glBindRenderbuffer(target,renderbuffer); });
}
void __cdecl w_glRenderbufferStorage(GLenum target,GLenum internalformat,GLsizei width,GLsizei height) {
    Backing::forward({BackingOp::renderbuffer_storage,{target,internalformat,static_cast<uint32_t>(width),static_cast<uint32_t>(height)}},
        [&] { ::glRenderbufferStorage(target,internalformat,width,height); });
}
GLenum __cdecl w_glCheckFramebufferStatus(GLenum target) {
    return Backing::forward({BackingOp::check_framebuffer,{target}},[&] { return ::glCheckFramebufferStatus(target); });
}
void __cdecl w_glViewport(GLint x,GLint y,GLsizei width,GLsizei height) {
    const auto factor=static_cast<GLint>(ui::raster_factor);
    x*=factor; y*=factor; width*=factor; height*=factor;
    Backing::forward({BackingOp::viewport,{static_cast<uint32_t>(x),static_cast<uint32_t>(y),static_cast<uint32_t>(width),static_cast<uint32_t>(height)}},
        [&] { ::glViewport(x,y,width,height); });
}
void __cdecl w_glGetIntegerv(GLenum name, GLint *data) {
    ::glGetIntegerv(name, data);
    if (current && binding_query(name) && static_cast<GLuint>(*data) == current->framebuffer)
        *data = 0;
}
void __cdecl w_glGetFloatv(GLenum name, GLfloat *data) {
    if (binding_query(name)) {
        GLint value = 0;
        w_glGetIntegerv(name, &value);
        *data = static_cast<GLfloat>(value);
    } else {
        ::glGetFloatv(name, data);
    }
}
const GLubyte *__cdecl w_glGetString(GLenum name) {
    auto result = current && name == GL_EXTENSIONS
                      ? reinterpret_cast<const GLubyte *>(current->extensions.c_str())
                      : ::glGetString(name);
    bionic::log_once("glGetString query=" + std::to_string(name) +
                     " value=" + (result ? reinterpret_cast<const char *>(result) : "<null>"));
    return result;
}
GLenum __cdecl w_glGetError() {
    if (current && current->pending_error != GL_NO_ERROR) {
        GLenum result = current->pending_error;
        current->pending_error = GL_NO_ERROR;
        return result;
    }
    return ::glGetError();
}
void *__cdecl w_glMapBufferOES(GLenum target, GLenum access) {
    if (access != GL_WRITE_ONLY_OES) {
        error(GL_INVALID_ENUM);
        return nullptr;
    }
    // ANGLE's OES implementation supplies the original write-only semantics.
    static auto map = reinterpret_cast<PFNGLMAPBUFFEROESPROC>(eglGetProcAddress("glMapBufferOES"));
    if (map)
        return map(target, access);
    GLint length = 0;
    ::glGetBufferParameteriv(target, GL_BUFFER_SIZE, &length);
    return ::glMapBufferRange(target, 0, length, GL_MAP_WRITE_BIT);
}
GLboolean __cdecl w_glUnmapBufferOES(GLenum target) { return ::glUnmapBuffer(target); }

struct UnpackState {
    GLint alignment = 4, row_length = 0, skip_rows = 0, skip_pixels = 0;
    UnpackState() {
        ::glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        ::glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
        ::glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
        ::glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
        ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        ::glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        ::glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    }
    ~UnpackState() {
        ::glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        ::glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
        ::glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
        ::glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
    }
};
bool validate_etc1(GLsizei width, GLsizei height, GLsizei size, const void *data) {
    GLint pbo = 0;
    ::glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
    if (pbo) {
        error(GL_INVALID_OPERATION); // ES2 guest has no unpack-buffer imports.
        return false;
    }
    if (!data || width < 1 || height < 1 || width > 16384 || height > 16384 ||
        size != ((width + 3) / 4) * ((height + 3) / 4) * 8) {
        error(GL_INVALID_VALUE);
        return false;
    }
    return true;
}
void __cdecl w_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                                      GLsizei height, GLint border, GLsizei imageSize,
                                      const void *data) {
    if (internalformat != GL_ETC1_RGB8_OES || !current || current->native_etc1) {
        ::glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
        return;
    }
    if (border != 0 || !validate_etc1(width, height, imageSize, data)) {
        error(GL_INVALID_VALUE);
        return;
    }
    auto pixels =
        decode_etc1(width, height, {static_cast<const uint8_t *>(data), static_cast<size_t>(imageSize)});
    UnpackState saved;
    ::glTexImage2D(target, level, GL_RGB8, width, height, border, GL_RGB, GL_UNSIGNED_BYTE,
                   pixels.data());
}
void __cdecl w_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                         GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
                                         const void *data) {
    if (format != GL_ETC1_RGB8_OES || !current || current->native_etc1) {
        ::glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize,
                                    data);
        return;
    }
    if (!validate_etc1(width, height, imageSize, data))
        return;
    auto pixels =
        decode_etc1(width, height, {static_cast<const uint8_t *>(data), static_cast<size_t>(imageSize)});
    UnpackState saved;
    ::glTexSubImage2D(target, level, xoffset, yoffset, width, height, GL_RGB, GL_UNSIGNED_BYTE,
                      pixels.data());
}
template <class R, class... A> constexpr unsigned arity(R(__cdecl *)(A...)) {
    static_assert(((sizeof(A) <= 4) && ...), "Guest GL arguments must fit an x86 stack word");
    return sizeof...(A);
}
template <class R, class... A> constexpr unsigned host_arity(R(GL_APIENTRY *)(A...)) {
    return sizeof...(A);
}
#define NOVA_GL_FORWARD(ret, name, count, params, args, target)                                         \
    static_assert(arity(&w_##name) == count && host_arity(&::target) == count);
#define NOVA_GL_CUSTOM(ret, name, count, params, args, target) static_assert(arity(&w_##name) == count);
#include "gles_entries.inc"
#undef NOVA_GL_FORWARD
#undef NOVA_GL_CUSTOM
const Entry table[] = {
#define NOVA_GL_FORWARD(ret, name, count, params, args, target)                                         \
    {#name, reinterpret_cast<const void *>(&w_##name), count},
#define NOVA_GL_CUSTOM NOVA_GL_FORWARD
#include "gles_entries.inc"
#undef NOVA_GL_FORWARD
#undef NOVA_GL_CUSTOM
};
static_assert(std::size(table) == 100);
} // namespace
std::span<const Entry> entries() { return table; }
void register_imports(ImportRegistry &registry) {
    for (auto &entry : table)
        registry.function(entry.name, entry.address);
}
void set_context(Context *context) { current = context; }
Context *current_context() { return current; }
void initialize(Context &context, int width, int height, bool force_etc1_fallback,
                const platform::NativeDeviceProfile *profile) {
    if (width < 1 || height < 1 || (width == 1920 && (!profile || !profile->applied())))
        throw std::invalid_argument("Invalid render size (1920-wide tier override is forbidden)");
    context.width = width;
    context.height = height;
    const auto extensions = ::glGetString(GL_EXTENSIONS);
    if (!extensions)
        throw std::runtime_error("A current ES3 context is required");
    context.extensions = reinterpret_cast<const char *>(extensions);
    auto has = [&](const char *ext) {
        return (" " + context.extensions + " ").find(std::string(" ") + ext + " ") != std::string::npos;
    };
    context.native_etc1 = !force_etc1_fallback && has("GL_OES_compressed_ETC1_RGB8_texture");
    context.max_anisotropy=0;
    if(has("GL_EXT_texture_filter_anisotropic")) {
        GLfloat maximum=0;
        ::glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&maximum);
        if(std::isfinite(maximum) && maximum>=1) context.max_anisotropy=maximum;
    }
    for (auto ext : {"GL_OES_compressed_ETC1_RGB8_texture", "GL_OES_texture_3D", "GL_OES_mapbuffer"})
        if (!has(ext))
            context.extensions += std::string(" ") + ext;
    ::glGenFramebuffers(1, &context.framebuffer);
    ::glBindFramebuffer(GL_FRAMEBUFFER, context.framebuffer);
    ::glGenRenderbuffers(1, &context.color);
    ::glBindRenderbuffer(GL_RENDERBUFFER, context.color);
    ::glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    ::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, context.color);
    ::glGenRenderbuffers(1, &context.depth_stencil);
    ::glBindRenderbuffer(GL_RENDERBUFFER, context.depth_stencil);
    ::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    ::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                context.depth_stencil);
    if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Port default framebuffer is incomplete");
    ::glBindRenderbuffer(GL_RENDERBUFFER, 0);
    ::glViewport(0, 0, width, height);
    ::glClearColor(0, 0, 0, 1);
    ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    ::glClearColor(0, 0, 0, 0);
    current = &context;
    std::printf("GLES target=%dx%d RGBA8 D24S8 ETC1=%s renderer=%s version=%s\n", width, height,
                context.native_etc1 ? "ANGLE" : "CPU fallback", ::glGetString(GL_RENDERER),
                ::glGetString(GL_VERSION));
}
void destroy(Context &context) {
    if (current == &context)
        current = nullptr;
    ::glDeleteFramebuffers(1, &context.framebuffer);
    ::glDeleteRenderbuffers(1, &context.color);
    ::glDeleteRenderbuffers(1, &context.depth_stencil);
    context = {};
}
void present(Context &context, EGLDisplay display, EGLSurface surface,
             PresentationOverlay overlay,void *user,int output_width,int output_height) {
    if(output_width<=0) output_width=context.width;
    if(output_height<=0) output_height=context.height;
    const auto rect=platform::fit_presentation(output_width,output_height,context.width,context.height);
    GLint draw = 0, read = 0;
    ::glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    ::glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    GLboolean scissor = ::glIsEnabled(GL_SCISSOR_TEST);
    ::glDisable(GL_SCISSOR_TEST);
    ::glBindFramebuffer(GL_READ_FRAMEBUFFER, context.framebuffer);
    ::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // Clear only the host backbuffer. Preserve inherited masks/clear values and
    // never resize, clear or replace the guest's fixed virtual framebuffer.
    GLboolean color_mask[4]{};
    ::glGetBooleanv(GL_COLOR_WRITEMASK,color_mask);
    ::glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const GLboolean discard=::glIsEnabled(GL_RASTERIZER_DISCARD);
    ::glDisable(GL_RASTERIZER_DISCARD);
    const GLfloat black[4]={0,0,0,1};
    ::glClearBufferfv(GL_COLOR,0,black);
    if(discard) ::glEnable(GL_RASTERIZER_DISCARD);
    ::glColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]);
    const int bottom=output_height-rect.y-rect.h;
    // The single-sample normalized-color path permits linear color blits.
    // Resampling smooths resized output without changing guest resolution.
    ::glBlitFramebuffer(0, 0, context.width, context.height, rect.x, bottom, rect.x+rect.w, bottom+rect.h,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR);
    try {
        if(overlay) overlay(user,output_width,output_height);
    } catch(...) {
        ::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        ::glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        if(scissor) ::glEnable(GL_SCISSOR_TEST);
        throw;
    }
    ::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    ::glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    if (scissor)
        ::glEnable(GL_SCISSOR_TEST);
    if (!eglSwapBuffers(display, surface))
        throw std::runtime_error("eglSwapBuffers failed: " + std::to_string(eglGetError()));
}
void screenshot(Context &context, const std::filesystem::path &path) {
    GLint read = 0, pack_buffer = 0;
    ::glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    ::glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    std::array<GLenum, 4> names{GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS,
                                GL_PACK_SKIP_PIXELS};
    std::array<GLint, 4> saved{};
    for (size_t i = 0; i < names.size(); ++i) {
        ::glGetIntegerv(names[i], &saved[i]);
        ::glPixelStorei(names[i], i == 0 ? 1 : 0);
    }
    ::glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    ::glBindFramebuffer(GL_READ_FRAMEBUFFER, context.framebuffer);
    size_t stride = static_cast<size_t>(context.width) * 4;
    std::vector<uint8_t> pixels(stride * context.height), topdown(pixels.size());
    ::glReadPixels(0, 0, context.width, context.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    ::glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    ::glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
    for (size_t i = 0; i < names.size(); ++i)
        ::glPixelStorei(names[i], saved[i]);
    for (int y = 0; y < context.height; ++y)
        std::memcpy(topdown.data() + static_cast<size_t>(y) * stride,
                    pixels.data() + static_cast<size_t>(context.height - 1 - y) * stride, stride);
    write_png(path, context.width, context.height, topdown);
    std::printf("screenshot=%s\n", path.string().c_str());
}
} // namespace nova::gles
