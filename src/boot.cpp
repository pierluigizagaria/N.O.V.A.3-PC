#include "java/boot.h"
#include "abi/guest_call.h"
#include "bionic/runtime.h"
#include "jni/runtime.h"
#include "loader/diagnostics.h"
#include "loader/report.h"
#include "platform/profile.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "Usage: nova3pc-boot <library> [--profile name] [--obb-dir directory] "
                             "[--profile-root directory] [--net-log]\n");
        return 2;
    }
    try {
        nova::bionic::PathConfig config;
        wchar_t executable[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, executable, MAX_PATH))
            throw std::runtime_error("Cannot locate executable");
        auto executable_directory = std::filesystem::path(executable).parent_path();
        auto settings = nova::profile::read_config(executable_directory / "nova3pc.conf", config.obb);
        for (int i = 2; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--net-log") {
                settings.net_log = true;
                continue;
            }
            if (i + 1 >= argc)
                throw std::runtime_error("Missing option argument");
            if (option == "--profile")
                config.profile = argv[++i];
            else if (option == "--obb-dir")
                config.obb = argv[++i];
            else if (option == "--profile-root")
                config.profile_root = argv[++i];
            else
                throw std::runtime_error("Unknown option: " + option);
        }
        nova::bionic::configure_paths(config);
        nova::profile::configure(settings);
        std::printf("profile=%s root=%s\n", config.profile.c_str(),
                    nova::bionic::profile_directory().string().c_str());
        auto start = std::chrono::steady_clock::now();
        nova::ImportRegistry imports;
        nova::bionic::register_boot(imports);
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(argv[1], imports);
        nova::Diagnostics diagnostics(image);
        nova::report_imports(image);
        image.run_initializers();
        nova::jni::Runtime runtime(image);
        nova::java::register_boot(runtime);
        uint32_t args[] = {reinterpret_cast<uint32_t>(runtime.vm()), 0};
        uint32_t version = nova::abi::call_guest(image.lookup("JNI_OnLoad"), args, 2);
        std::printf("JNI_OnLoad returned=0x%08x\n", version);
        if (version != 0x10001 && version != 0x10002 && version != 0x10004 && version != 0x10006)
            throw std::runtime_error("JNI_OnLoad returned an invalid JNI version");
        std::printf(
            "onload_ms=%.3f __cxa_atexit=%zu\n",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
            cxa.size());
        nova::java::startup(runtime);
        double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("boot_ms=%.3f __cxa_atexit=%zu live_jni_refs=%zu\n", ms, cxa.size(),
                    runtime.live_references());
        nova::bionic::stream_flush(nullptr);
        std::fflush(nullptr);
        // One image per process: no guest destructors or image unload at shutdown.
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "nova3pc-boot: %s\n", e.what());
        return 1;
    }
}
