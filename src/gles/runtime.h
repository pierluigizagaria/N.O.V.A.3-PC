#pragma once
#include "loader/imports.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace nova::platform { class NativeDeviceProfile; }
namespace nova::gles {
struct Entry {
    const char *name;
    const void *address;
    unsigned arguments;
};
std::span<const Entry> entries();
void register_imports(ImportRegistry &registry);
struct Context {
    GLuint framebuffer = 0, color = 0, depth_stencil = 0;
    int width = 0, height = 0;
    bool native_etc1 = false;
    float max_anisotropy = 0; // Zero means the extension is absent in this GL context.
    GLenum pending_error = GL_NO_ERROR;
    std::string extensions;
};
void initialize(Context &context, int width, int height, bool force_etc1_fallback = false,
                const platform::NativeDeviceProfile *profile=nullptr);
void set_context(Context *context);
Context *current_context(); // Host transaction: restore this thread's existing owner.
void destroy(Context &context);
using PresentationOverlay = void (*)(void *user,int width,int height);
void present(Context &context, EGLDisplay display, EGLSurface surface,
             PresentationOverlay overlay=nullptr,void *user=nullptr,
             int output_width=0,int output_height=0);
void screenshot(Context &context, const std::filesystem::path &path);
std::vector<uint8_t> decode_etc1(int width, int height, std::span<const uint8_t> blocks);
void write_png(const std::filesystem::path &path, unsigned width, unsigned height,
               std::span<const uint8_t> rgba);
} // namespace nova::gles
