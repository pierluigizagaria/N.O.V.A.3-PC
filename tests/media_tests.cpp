#include "abi/guest_call.h"
#include "audio/null_opensl.h"
#include "bionic/runtime.h"
#include "gles/runtime.h"
#include "loader/diagnostics.h"
#include "loader/elf_image.h"
#include "platform/angle.h"
#include <GLES2/gl2ext.h>
#include <array>
#include <cstdio>
#include <fstream>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class T> T function(nova::ImportRegistry &registry, const char *name) {
    auto entry = registry.find(name);
    check(entry && entry->kind == nova::ImportKind::function, name);
    return reinterpret_cast<T>(entry->address);
}
GLuint program(bool mixed_precision = false) {
    auto shader = [](GLenum type, const char *source) {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &source, nullptr);
        glCompileShader(id);
        GLint compiled = 0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
        check(compiled != 0, "Synthetic shader compilation");
        return id;
    };
    auto v = shader(GL_VERTEX_SHADER,
                    "#version 300 es\n"
                    "out vec2 uv; void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                    "uv=p; gl_Position=vec4(p*2.0-1.0,0,1); }");
    auto f = shader(GL_FRAGMENT_SHADER,
                    mixed_precision
                        ? "#version 300 es\n"
                          "precision mediump float; uniform lowp vec4 lowColor;"
                          "uniform mediump vec4 mediumColor; uniform highp vec4 highColor;"
                          "out vec4 color; void main(){color=lowColor+mediumColor+highColor;}"
                        : "#version 300 es\n"
                          "precision mediump float; uniform sampler2D tex; in vec2 uv; out vec4 color;"
                          "void main(){color=texture(tex,uv);}");
    auto p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint linked = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &linked);
    check(linked != 0, "Synthetic shader link");
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}
void etc1(nova::ImportRegistry &registry, nova::platform::Angle &angle) {
    const std::array<uint8_t, 8> red{255, 0, 0, 0, 0, 0, 0, 0};
    const std::array<uint8_t, 8> blue{0, 0, 255, 0, 0, 0, 0, 0};
    const std::array<uint8_t, 8> differential{0x80, 0x80, 0x80, 2, 0, 0, 0, 0};
    auto decoded = nova::gles::decode_etc1(3, 2, red);
    check(decoded.size() == 18 && decoded[0] == 255 && decoded[1] == 2 && decoded[17] == 2,
          "ETC1 individual/clipped block");
    decoded = nova::gles::decode_etc1(4, 4, differential);
    check(decoded[0] == 134 && decoded[47] == 134, "ETC1 differential block");
    auto negatives = red;
    negatives[4] = negatives[5] = negatives[6] = negatives[7] = 255;
    decoded = nova::gles::decode_etc1(4, 4, negatives);
    check(decoded[0] == 247 && decoded[1] == 0, "ETC1 selector sign and magnitude");
    bool rejected = false;
    try {
        nova::gles::decode_etc1(5, 5, red);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "ETC1 truncated data rejected");
    using Image = void(__cdecl *)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
    using Sub =
        void(__cdecl *)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *);
    auto upload = function<Image>(registry, "glCompressedTexImage2D");
    auto update = function<Sub>(registry, "glCompressedTexSubImage2D");
    for (bool force : {false, true}) {
        auto &target = angle.main_target();
        nova::gles::destroy(target);
        nova::gles::initialize(target, 64, 64, force);
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        auto p = program();
        glUseProgram(p);
        upload(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 4, 4, 0, 8, red.data());
        check(glGetError() == GL_NO_ERROR, "ETC1 upload error");
        glDrawArrays(GL_TRIANGLES, 0, 3);
        uint8_t pixel[4]{};
        glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        check(pixel[0] >= 254 && pixel[1] >= 1 && pixel[1] <= 3 && pixel[2] <= 3,
              "ETC1 sampled red pixel");
        if (force) {
            update(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_ETC1_RGB8_OES, 8, blue.data());
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            check(pixel[0] <= 3 && pixel[2] >= 254, "ETC1 fallback sub-image update");
        }
        check(glGetError() == GL_NO_ERROR, "ETC1 rendering error");
        glDeleteTextures(1, &tex);
        glDeleteProgram(p);
        glUseProgram(0);
    }
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    try {
        std::string mode = argv[2];
        nova::bionic::PathConfig config;
        config.profile_root = std::filesystem::path(argv[1]).parent_path() / "media-profiles" / mode;
        nova::bionic::configure_paths(config);
        nova::ImportRegistry registry;
        nova::bionic::register_boot(registry);
        nova::audio::register_null_opensl(registry);
        nova::gles::register_imports(registry);
        nova::bionic::CxaRegistry cxa(registry);
        nova::ElfImage image(argv[1], registry);
        nova::Diagnostics diagnostics(image);
        uint32_t result = 0;
        if (mode == "wrappers") {
            std::ifstream inventory(std::filesystem::path(NOVA_SOURCE_DIR) /
                                    "assets/manifests/x86_imports.txt");
            std::set<std::string> names;
            std::string line;
            while (std::getline(inventory, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.starts_with("gl"))
                    names.insert(line);
            }
            check(names.size() == 100 && nova::gles::entries().size() == names.size(),
                  "GL inventory size");
            for (auto &entry : nova::gles::entries()) {
                check(names.erase(entry.name) == 1, "Missing/duplicate GL wrapper");
                check(registry.find(entry.name)->address == reinterpret_cast<uint32_t>(entry.address),
                      "Wrapper address binding");
                check(entry.arguments <= 11, "Invalid GL argument count");
            }
            check(names.empty(), "Incomplete GL wrapper table");
        } else if (mode == "audio") {
            result = nova::abi::call0(image.lookup("fixture_audio"));
        } else {
            nova::platform::Angle angle("d3d11");
            check(angle.create(nullptr, 64, 64, mode == "contexts" ? 2 : 0) ==
                      (mode == "contexts" ? 2 : 0),
                  "Context count");
            if (mode == "framebuffer") {
                result = nova::abi::call0(image.lookup("fixture_framebuffer"));
                glEnable(GL_SCISSOR_TEST);
                glScissor(0, 0, 1, 1);
                angle.present();
                using Query = void(__cdecl *)(GLenum, GLint *);
                auto query = function<Query>(registry, "glGetIntegerv");
                GLint binding = -1, viewport[4]{};
                query(GL_FRAMEBUFFER_BINDING, &binding);
                glGetIntegerv(GL_VIEWPORT, viewport);
                check(binding == 0 && viewport[0] == -2 && viewport[1] == 3 && viewport[2] == 60 &&
                          viewport[3] == 50 && glIsEnabled(GL_SCISSOR_TEST),
                      "Presentation changed guest GL state");
                auto path =
                    std::filesystem::path(NOVA_SOURCE_DIR) / "local/screenshots/synthetic-fbo.png";
                angle.capture(path);
                std::ifstream png(path, std::ios::binary);
                std::array<unsigned char, 8> signature{};
                png.read(reinterpret_cast<char *>(signature.data()), signature.size());
                check(signature == std::array<unsigned char, 8>{137, 80, 78, 71, 13, 10, 26, 10},
                      "PNG signature");
            } else if (mode == "shaders") {
                // Independently written regression for ANGLE's mixed-precision uniform sort.
                auto p = program(true);
                glUseProgram(p);
                glUniform4f(glGetUniformLocation(p, "lowColor"), 0.25f, 0, 0, 0);
                glUniform4f(glGetUniformLocation(p, "mediumColor"), 0, 0.5f, 0, 0);
                glUniform4f(glGetUniformLocation(p, "highColor"), 0, 0, 0.75f, 1);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                uint8_t pixel[4]{};
                glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                check(pixel[0] >= 63 && pixel[0] <= 65 && pixel[1] >= 127 && pixel[1] <= 129 &&
                          pixel[2] >= 190 && pixel[2] <= 192 && pixel[3] == 255 &&
                          glGetError() == GL_NO_ERROR,
                      "Mixed-precision shader output");
                glUseProgram(0);
                glDeleteProgram(p);
            } else if (mode == "etc1") {
                etc1(registry, angle);
            } else if (mode == "contexts") {
                check(angle.make_current(1), "First loading context");
                GLuint texture = 0;
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                uint8_t pixels[4]{17, 29, 41, 255};
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                glFinish();
                check(angle.make_current(-1), "Release loading context");
                bool shared = false;
                std::thread worker([&] {
                    shared = angle.make_current(2) && glIsTexture(texture);
                    auto query = function<void(__cdecl *)(GLenum, GLint *)>(registry, "glGetIntegerv");
                    GLint binding = -1;
                    query(GL_FRAMEBUFFER_BINDING, &binding);
                    shared = shared && binding == 0;
                    angle.make_current(-1);
                });
                worker.join();
                check(shared, "Context resources not shared across loading threads");
                check(angle.make_current(0) && glIsTexture(texture), "Main context resource sharing");
                glDeleteTextures(1, &texture);
            } else {
                throw std::invalid_argument("Unknown media test");
            }
        }
        if (result) {
            std::fprintf(stderr, "FAIL %s media.c:%u\n", mode.c_str(), result);
            return 1;
        }
        nova::bionic::stream_flush(nullptr);
        std::printf("PASS media-%s\n", mode.c_str());
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "media test: %s\n", e.what());
        return 1;
    }
}
