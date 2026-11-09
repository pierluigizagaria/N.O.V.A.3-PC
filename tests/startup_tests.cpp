#include "platform/native_logo.h"
#include "platform/startup_presentation.h"
#include "bionic/runtime.h"
#include "audio/opensl.h"
#include "gles/runtime.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <thread>
namespace {
void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
nova::platform::NativeLogo::Clock::time_point now_value{};
auto now() { return now_value; }
void lifecycle() {
    using Gate=nova::platform::StartupPresentation;
    Gate gate;
    check(!gate.visible() && !gate.waiting() && !gate.needs_reveal(),"No reveal before a presented frame");
    gate.revealed(); check(!gate.visible(),"Premature UI acknowledgement rejected");
    std::thread render([&] { gate.presented(); }); render.join();
    check(gate.waiting() && gate.needs_reveal(),"First present parks renderer for UI");
    gate.revealed(); gate.presented();
    check(gate.visible() && !gate.waiting() && !gate.needs_reveal(),"Exactly one reveal");
    gate.cancel(); gate.revealed(); gate.presented();
    check(!gate.visible() && !gate.waiting(),"Cancellation never reopens window");
    Gate failure; failure.presented(); failure.cancel();
    check(!failure.waiting() && !failure.needs_reveal(),"Failed reveal releases renderer");
}
}
int main(int argc,char **argv) {
    try {
        lifecycle();
        if(argc!=3) throw std::runtime_error("Expected library and --real/--synthetic");
        const bool real=std::string(argv[2])=="--real";
        if(real && !std::filesystem::exists(argv[1])) return 77;
        nova::ImportRegistry imports;
        nova::bionic::register_boot(imports);
        nova::audio::register_opensl(imports);
        nova::gles::register_imports(imports);
        nova::ElfImage image(argv[1],imports);
        const auto render=nova::abi::require_function(image,"_ZN7GS_Logo6RenderEv");
        std::array<uint8_t,5> before{};
        std::memcpy(before.data(),reinterpret_cast<void *>(render+0xc6),5);
        std::array<uint32_t,7> observed{};
        auto draw=[&](uint32_t alpha) {
            check(nova::abi::invoke(render,{reinterpret_cast<uint32_t>(observed.data())})==0xabcdef01,
                  "Preserve native return and aligned re-entry");
            check(observed==std::array<uint32_t,7>{17,0x41200000,0xc1200000,31,0x3f800000,0x40000000,alpha},
                  "Only logo opacity changes; float bits/geometry/flags remain exact");
        };
        if(!real) draw(200);
        {
            nova::platform::NativeLogo logo(image,&now);
            bool duplicate=false;
            try { nova::platform::NativeLogo another(image); } catch(const std::logic_error &) { duplicate=true; }
            check(duplicate,"Duplicate logo adapter refused");
            if(!real) {
                now_value+=std::chrono::seconds(20); draw(0); // Preparation never consumes the fade.
                logo.visible(); draw(0);
                now_value+=std::chrono::milliseconds(325); draw(100);
                logo.visible(); draw(100); // Repeated present does not restart it.
                now_value+=std::chrono::milliseconds(325); draw(200);
                now_value+=std::chrono::seconds(10); draw(200);
            }
        }
        check(!std::memcmp(before.data(),reinterpret_cast<void *>(render+0xc6),5),"Exact native call restoration");
        if(!real) draw(200);
        std::puts("PASS startup presentation lifecycle and logo call contract");
        return 0;
    } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
