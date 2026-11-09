#pragma once
#include <string_view>

namespace nova::build_interface {
#if defined(NOVA_DEVELOPER_INTERFACE)
inline constexpr bool developer = true;
#else
inline constexpr bool developer = false;
#endif
inline bool client_option(std::string_view option) {
    if (option == "--profile" || option == "--profile-root" || option == "--obb-dir" ||
        option == "--server-config" || option == "--backend" || option == "--audio") return true;
#if defined(NOVA_DEVELOPER_INTERFACE)
    if (option == "--frames" || option == "--screenshot-after" || option == "--audio-dump" ||
        option == "--input-script" || option == "--allow-physical-input" ||
        option == "--input-diagnostics" || option == "--initial-menu-observer" ||
        option == "--diagnostic-menu-ack" || option == "--session-log" ||
        option == "--net-log" || option == "--file-log" || option == "--local-ca") return true;
#endif
    return false;
}
inline bool launcher_option(std::wstring_view option) {
    if (option.empty() || option == L"--setup" || option == L"--help") return true;
#if defined(NOVA_DEVELOPER_INTERFACE)
    if (option == L"--preview" || option == L"--preview-progress" ||
        option == L"--render-preview" || option == L"--verify-appearance") return true;
#endif
    return false;
}
inline bool client_requires_value(std::string_view option) {
#if defined(NOVA_DEVELOPER_INTERFACE)
    if (option == "--allow-physical-input" || option == "--input-diagnostics" ||
        option == "--initial-menu-observer" || option == "--diagnostic-menu-ack" ||
        option == "--session-log" || option == "--net-log" || option == "--file-log") return false;
#else
    (void)option;
#endif
    return true;
}
inline constexpr const char *client_help =
    "N.O.V.A. 3\nLaunch without arguments to open the game launcher.\n"
    "Connection: server.conf beside the game; mode, address, optional profile_root.\n"
    "Direct launch: nova3pc LIBRARY [options]\n"
    "  --server-config FILE  --profile NAME  --profile-root DIR  --obb-dir DIR\n"
    "  --backend d3d11|vulkan  --audio sdl|null  --help\n"
#if defined(NOVA_DEVELOPER_INTERFACE)
    "Debug only:\n  --frames N  --screenshot-after N  --audio-dump FILE  --input-script FILE\n"
    "  --allow-physical-input  --input-diagnostics  --initial-menu-observer\n"
    "  --diagnostic-menu-ack  --session-log  --net-log  --file-log  --local-ca FILE\n"
#endif
    ;
inline constexpr const wchar_t *launcher_help =
    L"Launch without arguments to play.\n--setup: prepare game files.\n--help: show this help."
#if defined(NOVA_DEVELOPER_INTERFACE)
    L"\nDebug: --preview, --preview-progress, --render-preview, --verify-appearance."
#endif
    ;
}
