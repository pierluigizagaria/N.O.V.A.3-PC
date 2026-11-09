#include "platform/build_interface.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>
int main() {
    try {
        auto check=[](bool value){if(!value) throw std::runtime_error("build interface mismatch");};
        for(auto option:{"--server-config","--profile","--profile-root","--obb-dir","--backend","--audio"})
            check(nova::build_interface::client_option(option));
        for(auto option:{"--frames","--screenshot-after","--audio-dump","--input-script","--allow-physical-input",
            "--input-diagnostics","--initial-menu-observer","--diagnostic-menu-ack","--session-log","--net-log","--file-log","--local-ca"}) {
            check(nova::build_interface::client_option(option)==nova::build_interface::developer);
            check((std::string_view(nova::build_interface::client_help).find(option)!=std::string_view::npos)==nova::build_interface::developer);
        }
        for(auto option:{L"",L"--setup",L"--help"}) check(nova::build_interface::launcher_option(option));
        for(auto option:{L"--preview",L"--preview-progress",L"--render-preview",L"--verify-appearance"})
            check(nova::build_interface::launcher_option(option)==nova::build_interface::developer);
        check(!nova::build_interface::client_option("--unknown"));
        check(!nova::build_interface::launcher_option(L"--unknown"));
        check(!nova::build_interface::launcher_option(L"--setup --preview"));
        std::puts(nova::build_interface::developer?"PASS Debug interface":"PASS Release interface");
        return 0;
    } catch(const std::exception &e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
