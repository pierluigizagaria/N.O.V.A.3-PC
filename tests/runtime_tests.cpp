#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "jni/runtime.h"
#include "loader/diagnostics.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    try {
        std::filesystem::path fixture = argv[1];
        std::string mode = argv[2];
        nova::bionic::PathConfig config;
        config.profile = mode;
        config.profile_root = fixture.parent_path() / "profiles" / mode;
        config.obb = fixture.parent_path() / "synthetic-obb";
        std::filesystem::create_directories(config.obb);
        std::ofstream(config.obb / "synthetic.txt") << "synthetic fixture";
        nova::bionic::configure_paths(config);
        nova::ImportRegistry registry;
        nova::bionic::register_boot(registry);
        nova::bionic::CxaRegistry cxa(registry);
        nova::ElfImage image(fixture, registry);
        nova::Diagnostics diagnostics(image);
        uint32_t result = 0;
        if (mode == "jni" || mode == "jni_threads" || mode == "jni_trap") {
            nova::jni::Runtime vm(image);
            vm.register_method("synthetic/Example", "mixed", "(IJFDLjava/lang/String;)J",
                               [](auto &r, auto, const auto &a) {
                                   nova::jni::Value v;
                                   if (r.object(a[4].l)->text.size() != 3)
                                       throw std::runtime_error("JNI string argument mismatch");
                                   v.j = a[0].i + a[1].j + static_cast<int64_t>(a[2].f + a[3].d);
                                   return v;
                               });
            vm.register_method("synthetic/Example", "floatValue", "()F", [](auto &, auto, const auto &) {
                nova::jni::Value v;
                v.f = 1.25f;
                return v;
            });
            vm.register_method("synthetic/Example", "doubleValue", "()D", [](auto &, auto, const auto &) {
                nova::jni::Value v;
                v.d = 3.5;
                return v;
            });
            uint32_t arg = reinterpret_cast<uint32_t>(vm.env());
            result = nova::abi::call_guest(image.lookup("fixture_" + mode), &arg, 1);
            if (mode == "jni_threads" && vm.live_references() != 0)
                throw std::runtime_error("Guest thread leaked JNI references");
            if (mode == "jni") {
                // A deleted local handle must never alias a later reference in another frame.
                auto first = vm.string("first");
                vm.release(first, 1);
                auto second = vm.string("second");
                if (first == second)
                    throw std::runtime_error("JNI reference token reused");
            }
        } else
            result = nova::abi::call0(image.lookup("fixture_" + mode));
        if (result) {
            std::fprintf(stderr, "FAIL %s at bionic.c:%u\n", mode.c_str(), result);
            return 1;
        }
        std::printf("PASS %s synthetic ELF32 fixture\n", mode.c_str());
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "runtime test: %s\n", e.what());
        return 1;
    }
}
