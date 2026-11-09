cmake_minimum_required(VERSION 4.2)
project(nova3pc VERSION 0.1.0 LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Use cmake --preset win32: clang-cl and 32-bit Windows are required")
endif()
find_package(SDL3 CONFIG REQUIRED)
find_package(unofficial-angle CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
set(CMAKE_CXX_STANDARD 20)
# Preserve host symbols for failures inside adapters in optimized real-game probes.
add_compile_options($<$<CONFIG:Release>:/Z7>)
add_link_options($<$<CONFIG:Release>:/DEBUG>)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions($<$<CONFIG:Debug>:NOVA_DEVELOPER_INTERFACE=1>)
add_library(nova_connection STATIC src/net/server_config.cpp)
target_include_directories(nova_connection PUBLIC src)
target_compile_options(nova_connection PRIVATE /W4 /WX /EHsc)
target_link_libraries(nova_connection PUBLIC ws2_32)
target_compile_definitions(nova_connection PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
add_library(nova_runtime STATIC
  src/jni/runtime.cpp
  src/java/boot.cpp
  src/platform/profile.cpp
  src/camera/world_fov.cpp
  src/camera/native_death_camera.cpp
  src/net/sockets.cpp
  src/net/interfaces.cpp
  src/net/interfaces_windows.cpp
  src/net/resolver.cpp
  src/net/local_shop.cpp
  src/net/native_trust.cpp
  src/net/native_server_tls.cpp
  src/net/community_lobby.cpp
  src/net/social_invitations.cpp
  src/net/native_friends_refresh.cpp
  src/net/community_match.cpp
  src/net/native_snapshot_presentation.cpp
  src/net/native_consumables.cpp
  src/net/server_certificate.cpp
  src/net/tls_stream.cpp
  src/net/secure_datagram.cpp
  src/net/native_session_trace.cpp
  src/net/native_save_trace.cpp
  src/loader/report.cpp
  src/loader/sha256.cpp
  src/abi/guest_call.cpp
  src/abi/guest_hook.cpp
  src/abi/guest_jump.cpp
  src/loader/elf_image.cpp
  src/loader/imports.cpp
  src/loader/diagnostics.cpp
  src/bionic/data.cpp
  src/bionic/errors.cpp
  src/bionic/paths.cpp
  src/bionic/files.cpp
  src/bionic/stdio.cpp
  src/bionic/format.cpp
  src/bionic/misc.cpp
  src/bionic/math.cpp
  src/bionic/stdlib.cpp
  src/bionic/string.cpp
  src/bionic/cxx.cpp
  src/bionic/pthread.cpp
  src/bionic/wchar.cpp
  src/bionic/time.cpp)
target_link_libraries(nova_runtime PUBLIC nova_connection synchronization bcrypt ws2_32 iphlpapi winhttp crypt32 secur32)
# Android x86 ELF frames do not probe Windows guard pages. Keep the existing
# nominal reserve and commit its budget for every executable entering guest code.
target_link_options(nova_runtime INTERFACE "/STACK:1048576,1048576")
target_include_directories(nova_runtime PUBLIC src)
target_compile_definitions(nova_runtime PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(nova_runtime PRIVATE /W4 /WX /EHsc)
add_executable(nova3pc-inspect src/inspect.cpp)
target_link_libraries(nova3pc-inspect PRIVATE nova_runtime)
target_compile_options(nova3pc-inspect PRIVATE /W4 /WX /EHsc)
add_executable(nova3pc-boot src/boot.cpp)
target_link_libraries(nova3pc-boot PRIVATE nova_runtime)
target_compile_options(nova3pc-boot PRIVATE /W4 /WX /EHsc)

# Every guest GLES import enters a cdecl adapter before ANGLE's stdcall entry.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_path(NOVA_GLES_INCLUDE_DIR GLES3/gl3.h REQUIRED)
set(NOVA_GL_TABLE "${CMAKE_CURRENT_BINARY_DIR}/generated/gles_entries.inc")
add_custom_command(OUTPUT "${NOVA_GL_TABLE}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/generate_gles.py"
    --include "${NOVA_GLES_INCLUDE_DIR}"
    --inventory "${PROJECT_SOURCE_DIR}/assets/manifests/x86_imports.txt"
    --output "${NOVA_GL_TABLE}"
  DEPENDS tools/generate_gles.py assets/manifests/x86_imports.txt
    "${NOVA_GLES_INCLUDE_DIR}/GLES2/gl2.h"
    "${NOVA_GLES_INCLUDE_DIR}/GLES2/gl2ext.h"
    "${NOVA_GLES_INCLUDE_DIR}/GLES3/gl3.h"
  VERBATIM)
file(STRINGS "${PROJECT_SOURCE_DIR}/assets/manifests/bootstrap_files.txt" NOVA_BOOTSTRAP_FILES)
set(NOVA_BOOTSTRAP_TABLE "")
foreach(name IN LISTS NOVA_BOOTSTRAP_FILES)
  string(APPEND NOVA_BOOTSTRAP_TABLE "\"${name}\",\n")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/bootstrap_files.inc" "${NOVA_BOOTSTRAP_TABLE}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS assets/manifests/bootstrap_files.txt)
add_library(nova_media STATIC src/gles/runtime.cpp src/gles/etc1.cpp src/gles/png.cpp
  src/platform/fov_panel.cpp src/platform/fov_config.cpp
  src/camera/native_world_fov.cpp
  src/movement_wobble/native_premix_locomotion.cpp
  src/movement_wobble/native_camera_bob.cpp
  src/weapon_placement/native_rigid_draw_binding.cpp
  src/camera/viewmodel_eligibility.cpp src/camera/viewmodel_eligibility_native.cpp
  src/input/input.cpp src/input/script.cpp src/input/controls.cpp src/input/native_controls.cpp src/input/native_communications.cpp src/input/native_panel_movement.cpp src/input/native_gamepad_post_drain.cpp src/input/trace.cpp src/input/native_diagnostics.cpp
  src/platform/angle.cpp src/platform/window_modes.cpp src/platform/preflight.cpp src/audio/opensl.cpp src/audio/pcm.cpp "${NOVA_GL_TABLE}")
target_sources(nova_media PRIVATE src/input/gamepad.cpp)
target_include_directories(nova_media PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(nova_media PUBLIC nova_runtime SDL3::SDL3
  imgui::imgui
  unofficial::angle::libEGL unofficial::angle::libGLESv2 windowscodecs ole32)
target_compile_options(nova_media PRIVATE /W4 /WX /EHsc)
add_executable(nova3pc WIN32 src/play.cpp src/platform/game_entry.cpp)
target_sources(nova_media PRIVATE src/platform/native_logo.cpp)

# First-run UI and importer are host-only; no guest library is loaded here.
find_package(ZLIB REQUIRED)
add_library(nova_setup STATIC src/launcher/setup.cpp src/launcher/process.cpp src/launcher/language.cpp src/launcher/shortcut.cpp src/launcher/skin.cpp)
target_include_directories(nova_setup PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(nova_setup PUBLIC ZLIB::ZLIB bcrypt gdiplus ole32 shell32 uuid)
target_compile_definitions(nova_setup PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(nova_setup PRIVATE /W4 /WX /EHsc /utf-8)
add_executable(nova3pc-launcher WIN32 src/launcher/main.cpp)
target_link_libraries(nova3pc-launcher PRIVATE nova_setup comctl32 comdlg32 dwmapi uxtheme gdiplus ole32 shell32)
target_compile_options(nova3pc-launcher PRIVATE /W4 /WX /EHsc /utf-8)

# Executable metadata and icon. The icon comes from the user's local original
# artwork (local/launcher/icon.png, gitignored) and is skipped when absent.
set(NOVA_ICON_SOURCE "${PROJECT_SOURCE_DIR}/local/launcher/icon.png" CACHE FILEPATH
  "PNG for the executables' icon resource (optional; CI may point elsewhere)")
set(NOVA_RC_ICON "")
if(EXISTS "${NOVA_ICON_SOURCE}")
  set(NOVA_ICON "${CMAKE_CURRENT_BINARY_DIR}/generated/nova3.ico")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/make_icon.py"
    "${NOVA_ICON_SOURCE}" "${NOVA_ICON}" RESULT_VARIABLE NOVA_ICON_RESULT)
  if(NOVA_ICON_RESULT EQUAL 0)
    set(NOVA_RC_ICON "1 ICON \"${NOVA_ICON}\"")
  else()
    message(WARNING "Executable icon skipped: tools/make_icon.py failed")
  endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/app.rc.in")
string(REPLACE "." "," NOVA_VERSION_COMMA "${PROJECT_VERSION}.0")
foreach(pair "nova3pc|NOVA3-Game|N.O.V.A. 3 PC" "nova3pc-launcher|NOVA3|N.O.V.A. 3 PC launcher")
  string(REPLACE "|" ";" pair "${pair}")
  list(GET pair 0 NOVA_RC_TARGET)
  list(GET pair 1 NOVA_RC_NAME)
  list(GET pair 2 NOVA_RC_DESCRIPTION)
  configure_file(cmake/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/${NOVA_RC_TARGET}.rc" @ONLY)
  target_sources(${NOVA_RC_TARGET} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/${NOVA_RC_TARGET}.rc")
endforeach()
target_sources(nova_media PRIVATE src/platform/native_device_profile.cpp)
target_sources(nova_media PRIVATE src/platform/native_detail.cpp)
target_sources(nova_media PRIVATE src/gles/native_texture_filtering.cpp)
target_sources(nova_media PRIVATE src/platform/native_render_extent.cpp)
target_sources(nova_media PRIVATE src/menu/native_menu_observer.cpp src/menu/initial_target_scaling.cpp src/menu/dialog_escape.cpp)
target_sources(nova_media PRIVATE src/ui/native_scale.cpp src/ui/native_hud_scale.cpp src/ui/native_hud_nodes.cpp src/ui/native_menu_fonts.cpp src/ui/menu_text_trace.cpp)
target_sources(nova_media PRIVATE src/ui/native_pixel_projection.cpp)
target_sources(nova_media PRIVATE src/ui/native_pixel_resources.cpp)
target_sources(nova_media PRIVATE src/ui/native_artwork.cpp src/ui/sprite_density.cpp)
target_link_libraries(nova3pc PRIVATE nova_media)
target_compile_options(nova3pc PRIVATE /W4 /WX /EHsc)

# Independent diagnostic process: no game/runtime libraries or guest callbacks.
add_executable(nova3pc-first-fault tools/diagnostics/first_fault.cpp)
target_link_libraries(nova3pc-first-fault PRIVATE bcrypt)
target_compile_definitions(nova3pc-first-fault PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(nova3pc-first-fault PRIVATE /W4 /WX /EHsc)

include(CTest)
option(NOVA_REAL_TESTS "Opt in to running the locally supplied game library" OFF)
if(BUILD_TESTING)
  add_subdirectory(tests)
  add_executable(debug-observer-child tests/debug_observer_child.cpp)
  add_library(debug-observer-module SHARED tests/debug_observer_module.cpp)
  target_compile_definitions(debug-observer-child PRIVATE _CRT_SECURE_NO_WARNINGS)
  add_test(NAME debug-observer COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tests/check_debug_observer.py"
    "$<TARGET_FILE:nova3pc-first-fault>" "$<TARGET_FILE:debug-observer-child>"
    "$<TARGET_FILE:debug-observer-module>" "${PROJECT_SOURCE_DIR}")
  set_tests_properties(debug-observer PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 90)
endif()
