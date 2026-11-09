find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(launcher.cmake)
include(o08-authored-loop.cmake)
include(v01-consumed-ads-provenance.cmake)
include(o08-weapon-locomotion.cmake)
include(v01-consumed-pose.cmake)
include(v01-visible-ads-binding.cmake)
include(o08-inherited-producer.cmake)
include(v01-presentation-interval.cmake)
include(native-device-profile.cmake)
add_executable(window-modes-tests window_modes_tests.cpp ../src/platform/window_modes.cpp)
target_include_directories(window-modes-tests PRIVATE ../src)
target_link_libraries(window-modes-tests PRIVATE SDL3::SDL3)
target_link_options(window-modes-tests PRIVATE "/STACK:1048576,1048576")
target_compile_options(window-modes-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME platform-window-modes COMMAND window-modes-tests)
set_tests_properties(platform-window-modes PROPERTIES TIMEOUT 20)
add_executable(window-present-tests window_present_tests.cpp)
target_link_libraries(window-present-tests PRIVATE nova_media)
target_compile_options(window-present-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME platform-window-present COMMAND window-present-tests)
set_tests_properties(platform-window-present PROPERTIES TIMEOUT 20)

if(NOT NOVA_LLVM_BIN)
  get_filename_component(_nova_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
  find_program(NOVA_CLANG NAMES clang.exe HINTS
    "${_nova_compiler_dir}"
    "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/bin"
    "${CMAKE_GENERATOR_INSTANCE}/VC/Tools/Llvm/x64/bin" REQUIRED)
  get_filename_component(NOVA_LLVM_BIN "${NOVA_CLANG}" DIRECTORY)
  if(NOT EXISTS "${NOVA_LLVM_BIN}/ld.lld.exe")
    message(FATAL_ERROR "Install the Visual Studio clang/LLD component or set NOVA_LLVM_BIN")
  endif()
endif()
set(FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
include(startup.cmake)
include(dialog-escape.cmake)
include(communications.cmake)
include(native-detail.cmake)
include(texture-filtering.cmake)
include(lan-a.cmake)
include(local-shop.cmake)
include(interface-ioctl.cmake)
set(FIXTURES "${FIXTURE_DIR}/loader.so" "${FIXTURE_DIR}/bionic.so" "${FIXTURE_DIR}/platform.so" "${FIXTURE_DIR}/media.so" "${FIXTURE_DIR}/input.so" "${FIXTURE_DIR}/world_fov.so")
foreach(name bad_relocation bad_target bad_symbol wx arm truncated stripped)
  list(APPEND FIXTURES "${FIXTURE_DIR}/${name}.so")
endforeach()
add_custom_command(OUTPUT ${FIXTURES}
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_fixtures.py"
    --llvm-bin "${NOVA_LLVM_BIN}" --source "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    --output "${FIXTURE_DIR}"
  DEPENDS build_fixtures.py fixtures/loader.c fixtures/boundary.S fixtures/host.c fixtures/bionic.c fixtures/bionic_boundary.S fixtures/platform.c fixtures/media.c fixtures/audio.c fixtures/media_boundary.S fixtures/input.c fixtures/input_boundary.S fixtures/look_contract.c fixtures/look_contract.h fixtures/draw_contract.c fixtures/draw_contract.h fixtures/mech_contract.c fixtures/mech_contract.h fixtures/machinegun_contract.c fixtures/machinegun_contract.h fixtures/mounted_machinegun_contract.c fixtures/mounted_machinegun_contract.h fixtures/bike_contract.c fixtures/bike_contract.h fixtures/bike_boundary.S fixtures/driver_contract.c fixtures/driver_contract.h fixtures/jeep_contract.c fixtures/jeep_contract.h
    fixtures/world_fov_contract.c fixtures/world_fov_contract.h fixtures/world_fov_boundary.S
  VERBATIM)
add_custom_target(elf_fixtures ALL DEPENDS ${FIXTURES})
include(c03-native-intents.cmake)
set(STACK_FIXTURE "${FIXTURE_DIR}/stack.so")
add_custom_command(OUTPUT "${STACK_FIXTURE}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe"
    -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/stack.S" -o "${STACK_FIXTURE}"
  DEPENDS fixtures/stack.S VERBATIM)
add_custom_target(stack_fixture ALL DEPENDS "${STACK_FIXTURE}")
add_executable(stack-tests stack_tests.cpp)
target_link_libraries(stack-tests PRIVATE nova_media)
target_compile_options(stack-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(stack-tests stack_fixture)
add_test(NAME runtime-stack-commit COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/check_stack.py" "$<TARGET_FILE:stack-tests>"
  "${STACK_FIXTURE}" "${CMAKE_CURRENT_BINARY_DIR}/runtime/b03-stack-$<CONFIG>")
set_tests_properties(runtime-stack-commit PROPERTIES TIMEOUT 60)
add_executable(loader-tests loader_tests.cpp)
target_link_libraries(loader-tests PRIVATE nova_runtime)
target_compile_options(loader-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(loader-tests elf_fixtures)
add_test(NAME loader-synthetic COMMAND loader-tests "${FIXTURE_DIR}")
foreach(mode trap-first trap-second crash)
  add_test(NAME loader-${mode}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_failure.py"
      $<TARGET_FILE:loader-tests> "${FIXTURE_DIR}" ${mode})
endforeach()
add_test(NAME input-setup COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_setup_inputs.py")
if(NOVA_REAL_TESTS)
  add_test(NAME loader-real COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_real.py" $<TARGET_FILE:nova3pc-inspect>
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(loader-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 60)
endif()

add_executable(mech-controls-tests mech_controls_tests.cpp)
target_link_libraries(mech-controls-tests PRIVATE nova_media)
target_compile_options(mech-controls-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(mech-controls-tests elf_fixtures)
add_test(NAME input-mech-controls COMMAND mech-controls-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-mech-controls PROPERTIES TIMEOUT 20)
add_executable(driver-controls-tests driver_controls_tests.cpp)
target_link_libraries(driver-controls-tests PRIVATE nova_media)
target_compile_options(driver-controls-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(driver-controls-tests elf_fixtures)
add_test(NAME input-driver-controls COMMAND driver-controls-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-driver-controls PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-driver-hooks-real COMMAND driver-controls-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(input-driver-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
add_executable(mounted-machinegun-controls-tests mounted_machinegun_controls_tests.cpp)
target_link_libraries(mounted-machinegun-controls-tests PRIVATE nova_media)
target_compile_options(mounted-machinegun-controls-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(mounted-machinegun-controls-tests elf_fixtures)
add_test(NAME input-mounted-machinegun-controls COMMAND mounted-machinegun-controls-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-mounted-machinegun-controls PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-mounted-machinegun-hooks-real COMMAND mounted-machinegun-controls-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(input-mounted-machinegun-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
add_executable(machinegun-controls-tests machinegun_controls_tests.cpp)
target_link_libraries(machinegun-controls-tests PRIVATE nova_media)
target_compile_options(machinegun-controls-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(machinegun-controls-tests elf_fixtures)
add_test(NAME input-machinegun-controls COMMAND machinegun-controls-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-machinegun-controls PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-machinegun-hooks-real COMMAND machinegun-controls-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(input-machinegun-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
if(NOVA_REAL_TESTS)
  add_test(NAME input-mech-hooks-real COMMAND mech-controls-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(input-mech-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
set_tests_properties(loader-synthetic loader-trap-first loader-trap-second loader-crash input-setup
  PROPERTIES TIMEOUT 30)

add_executable(runtime-tests runtime_tests.cpp)
target_link_libraries(runtime-tests PRIVATE nova_runtime)
target_compile_options(runtime-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(runtime-tests elf_fixtures)
foreach(area allocator tmpnam stdio format files threads misc jni jni_threads)
  add_test(NAME runtime-${area} COMMAND runtime-tests "${FIXTURE_DIR}/bionic.so" ${area})
  set_tests_properties(runtime-${area} PROPERTIES TIMEOUT 30)
endforeach()

if(NOVA_REAL_TESTS)
  add_test(NAME boot-real COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_boot.py" $<TARGET_FILE:nova3pc-boot> "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" "${CMAKE_CURRENT_BINARY_DIR}/runtime/$<CONFIG>")
  set_tests_properties(boot-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 60)
  add_test(NAME boot-policy-cache-real COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_boot_policy.py" $<TARGET_FILE:nova3pc-boot>
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
    "${CMAKE_CURRENT_BINARY_DIR}/runtime/l02-$<CONFIG>")
  set_tests_properties(boot-policy-cache-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 60)
endif()

add_test(NAME runtime-jni-trap COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_failure.py" $<TARGET_FILE:runtime-tests> "${FIXTURE_DIR}/bionic.so" jni_trap)
set_tests_properties(runtime-jni-trap PROPERTIES TIMEOUT 30)

add_executable(platform-tests platform_tests.cpp)
target_link_libraries(platform-tests PRIVATE nova_runtime)
target_compile_definitions(platform-tests PRIVATE NOVA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(platform-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(platform-tests elf_fixtures)
add_test(NAME platform-guest-extent COMMAND platform-tests "${FIXTURE_DIR}/platform.so"
  guest-extent "${CMAKE_CURRENT_BINARY_DIR}/profiles/$<CONFIG>/guest-extent")
set_tests_properties(platform-guest-extent PROPERTIES TIMEOUT 20)
add_test(NAME platform-policy-cache COMMAND platform-tests "${FIXTURE_DIR}/platform.so"
  policy-cache "${CMAKE_CURRENT_BINARY_DIR}/profiles/$<CONFIG>/policy-cache")
set_tests_properties(platform-policy-cache PROPERTIES TIMEOUT 20)
foreach(area tcp udp waits errors resolver broadcast)
  add_test(NAME network-${area} COMMAND platform-tests "${FIXTURE_DIR}/platform.so" ${area} "${CMAKE_CURRENT_BINARY_DIR}/profiles/$<CONFIG>/${area}")
  set_tests_properties(network-${area} PROPERTIES TIMEOUT 30)
endforeach()
set_tests_properties(network-broadcast PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME profile-processes COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_profiles.py" $<TARGET_FILE:platform-tests> "${FIXTURE_DIR}/platform.so" "${CMAKE_CURRENT_BINARY_DIR}/profiles/$<CONFIG>")
add_test(NAME network-processes COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_network.py" $<TARGET_FILE:platform-tests> "${FIXTURE_DIR}/platform.so" "${CMAKE_CURRENT_BINARY_DIR}/profiles/$<CONFIG>")
set_tests_properties(profile-processes network-processes PROPERTIES TIMEOUT 45)

add_executable(media-tests media_tests.cpp)
target_link_libraries(media-tests PRIVATE nova_media)
target_compile_definitions(media-tests PRIVATE NOVA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(media-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(media-tests elf_fixtures)
foreach(area wrappers framebuffer etc1 contexts audio shaders)
  add_test(NAME media-${area} COMMAND media-tests "${FIXTURE_DIR}/media.so" ${area})
  set_tests_properties(media-${area} PROPERTIES TIMEOUT 30)
endforeach()

add_test(NAME quality-profiles COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_quality_profiles.py")
set_tests_properties(quality-profiles PROPERTIES TIMEOUT 30)

include(audio-reopen.cmake)

add_executable(audio-tests audio_tests.cpp)
target_link_libraries(audio-tests PRIVATE nova_media)
target_compile_definitions(audio-tests PRIVATE NOVA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(audio-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(audio-tests elf_fixtures)
foreach(area t04 formats lifecycle removed physical-removed volume fallback slow null-stream preflight)
  add_test(NAME audio-${area} COMMAND audio-tests "${FIXTURE_DIR}/media.so" ${area}
    "${CMAKE_CURRENT_BINARY_DIR}/audio-profiles/$<CONFIG>/${area}")
  set_tests_properties(audio-${area} PROPERTIES TIMEOUT 40 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endforeach()

add_test(NAME audio-trap COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_audio.py"
  trap $<TARGET_FILE:audio-tests> "${FIXTURE_DIR}/media.so" "${CMAKE_CURRENT_BINARY_DIR}/runtime/$<CONFIG>/audio-trap")
add_test(NAME audio-preflight-cli CONFIGURATIONS Debug COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/check_audio.py"
  preflight $<TARGET_FILE:nova3pc> unused "${CMAKE_CURRENT_BINARY_DIR}/runtime/$<CONFIG>/audio-preflight")
set_tests_properties(audio-trap audio-preflight-cli PROPERTIES TIMEOUT 20 WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")

add_executable(input-tests input_tests.cpp)
target_link_libraries(input-tests PRIVATE nova_media)
target_compile_options(input-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(input-tests elf_fixtures)
foreach(area touch letterbox guest-extent keys text script ownership communication-mouse)
  add_test(NAME input-${area} COMMAND input-tests "${FIXTURE_DIR}/input.so" ${area})
  set_tests_properties(input-${area} PROPERTIES TIMEOUT 20)
endforeach()

add_executable(fov-config-tests fov_config_tests.cpp)
target_link_libraries(fov-config-tests PRIVATE nova_media)
target_compile_options(fov-config-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME fov-config-synthetic COMMAND fov-config-tests
  "${CMAKE_CURRENT_BINARY_DIR}/fov-config-$<CONFIG>")
add_executable(fov-panel-tests fov_panel_tests.cpp)
target_link_libraries(fov-panel-tests PRIVATE nova_media)
target_compile_options(fov-panel-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(fov-panel-tests elf_fixtures)
add_test(NAME fov-panel-policy COMMAND fov-panel-tests policy)
add_test(NAME fov-panel-render COMMAND fov-panel-tests render
  "${CMAKE_CURRENT_BINARY_DIR}/fov-panel-$<CONFIG>.png" "${FIXTURE_DIR}/world_fov.so")
set_tests_properties(fov-config-synthetic fov-panel-policy fov-panel-render PROPERTIES TIMEOUT 60)

add_executable(controls-tests controls_tests.cpp)
add_executable(power-selection-tests power_selection_tests.cpp)
target_link_libraries(power-selection-tests PRIVATE nova_media)
target_compile_options(power-selection-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(power-selection-tests elf_fixtures)
add_test(NAME input-power-selection COMMAND power-selection-tests "${FIXTURE_DIR}/input.so" --synthetic)
set_tests_properties(input-power-selection PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-power-selection-real COMMAND power-selection-tests "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" --real)
  set_tests_properties(input-power-selection-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
target_link_libraries(controls-tests PRIVATE nova_media)
target_compile_options(controls-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(controls-tests elf_fixtures)
add_test(NAME input-native-controls COMMAND controls-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-native-controls PROPERTIES TIMEOUT 20)

add_executable(mouse-motion-tests mouse_motion_tests.cpp)
target_link_libraries(mouse-motion-tests PRIVATE nova_media)
target_compile_options(mouse-motion-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(mouse-motion-tests elf_fixtures)
add_test(NAME input-mouse-motion COMMAND mouse-motion-tests "${FIXTURE_DIR}/input.so"
  "${CMAKE_CURRENT_BINARY_DIR}/mouse-motion-$<CONFIG>.log")
set_tests_properties(input-mouse-motion PROPERTIES TIMEOUT 20)

add_executable(look-contract-tests look_contract_tests.cpp)
target_link_libraries(look-contract-tests PRIVATE nova_media)
target_compile_options(look-contract-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(look-contract-tests elf_fixtures)
add_test(NAME input-look-contract COMMAND look-contract-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-look-contract PROPERTIES TIMEOUT 20)

add_executable(native-diagnostics-tests native_diagnostics_tests.cpp)
target_link_libraries(native-diagnostics-tests PRIVATE nova_media)
target_compile_options(native-diagnostics-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(native-diagnostics-tests elf_fixtures)
add_test(NAME input-native-diagnostics COMMAND native-diagnostics-tests "${FIXTURE_DIR}/input.so"
  "${CMAKE_CURRENT_BINARY_DIR}/native-diagnostics-$<CONFIG>.log")
set_tests_properties(input-native-diagnostics PROPERTIES TIMEOUT 20)

add_executable(sprint-draw-tests sprint_draw_tests.cpp)
target_link_libraries(sprint-draw-tests PRIVATE nova_media)
target_compile_options(sprint-draw-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(sprint-draw-tests elf_fixtures)
add_test(NAME input-sprint-draw COMMAND sprint-draw-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-sprint-draw PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-sprint-callsite-real COMMAND sprint-draw-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(input-sprint-callsite-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()

add_executable(jeep-controls-tests jeep_controls_tests.cpp)
target_link_libraries(jeep-controls-tests PRIVATE nova_media)
target_compile_options(jeep-controls-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(jeep-controls-tests elf_fixtures)
add_test(NAME input-jeep-controls COMMAND jeep-controls-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-jeep-controls PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-jeep-hooks-real COMMAND jeep-controls-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(input-jeep-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()

add_executable(bike-controls-tests bike_controls_tests.cpp)
target_link_libraries(bike-controls-tests PRIVATE nova_media)
target_compile_options(bike-controls-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(bike-controls-tests elf_fixtures)
add_test(NAME input-bike-controls COMMAND bike-controls-tests "${FIXTURE_DIR}/input.so")
set_tests_properties(input-bike-controls PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-bike-hooks-real COMMAND bike-controls-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(input-bike-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()

add_executable(world-fov-tests world_fov_tests.cpp)
target_link_libraries(world-fov-tests PRIVATE nova_runtime)
target_compile_options(world-fov-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME camera-world-fov-config COMMAND world-fov-tests config
  "${CMAKE_CURRENT_BINARY_DIR}/world-fov-config/$<CONFIG>")
add_test(NAME camera-world-fov-optics COMMAND world-fov-tests optics)
set_tests_properties(camera-world-fov-config camera-world-fov-optics PROPERTIES TIMEOUT 20)

add_executable(native-world-fov-tests native_world_fov_tests.cpp)
target_link_libraries(native-world-fov-tests PRIVATE nova_media)
target_compile_options(native-world-fov-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(native-world-fov-tests elf_fixtures)
add_test(NAME camera-world-fov-native COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so")
add_test(NAME camera-live-fov-native COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so" --live)
add_test(NAME camera-foreign-pseudo-native COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so" --foreign-known)
add_test(NAME camera-additive-diagnostic-native COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so" --additive-probe)
add_test(NAME camera-animation-fov-native COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so" --animation)
add_test(NAME camera-world-fov-event-failure COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/check_world_fov_failure.py" $<TARGET_FILE:native-world-fov-tests> "${FIXTURE_DIR}/world_fov.so")
set_tests_properties(camera-world-fov-native camera-live-fov-native camera-foreign-pseudo-native camera-additive-diagnostic-native camera-animation-fov-native camera-world-fov-event-failure PROPERTIES TIMEOUT 30)
if(NOVA_REAL_TESTS)
  add_test(NAME camera-world-fov-hooks-real COMMAND native-world-fov-tests --real
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(camera-world-fov-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
  add_test(NAME camera-weapon-fov-hooks-real COMMAND native-world-fov-tests --real-weapon
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(camera-weapon-fov-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
  add_test(NAME camera-live-fov-hooks-real COMMAND native-world-fov-tests --real-live
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(camera-live-fov-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()

if(NOVA_REAL_TESTS)
  add_executable(native-render-extent-tests native_render_extent_tests.cpp)
  target_link_libraries(native-render-extent-tests PRIVATE nova_media)
  target_compile_options(native-render-extent-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME native-render-extent-real COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_native_render_extent.py" "$<TARGET_FILE:native-render-extent-tests>"
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(native-render-extent-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
  add_test(NAME native-render-extent-negative COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_native_render_extent.py" "$<TARGET_FILE:native-render-extent-tests>"
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" --negative)
  set_tests_properties(native-render-extent-negative PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()

include(native-render-apply.cmake)

include(o07-controls.cmake)

include(t13-gamepad.cmake)

# O09 isolated source candidate: integrate only after independent source review.
include(viewmodel_eligibility.cmake)
add_test(NAME camera-viewmodel-eligibility-native COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so" --eligibility)
set_tests_properties(camera-viewmodel-eligibility-native PROPERTIES TIMEOUT 30)

# Synthetic observer target; fixture-only bindings never enter nova_media.
include(menu_observer_fixture.cmake)

include(t13b3-post-drain.cmake)

include(weapon-position-policy.cmake)

include(t13c-play-gamepad.cmake)

include(v01-rigid-pose-binding.cmake)
include(rigid-draw-activation.cmake)

include(menu_ack.cmake)




include(initial_target_scaling.cmake)

add_executable(movement-camera-binding-tests movement_camera_binding_tests.cpp)
target_link_libraries(movement-camera-binding-tests PRIVATE nova_media)
target_compile_options(movement-camera-binding-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME movement-camera-binding COMMAND movement-camera-binding-tests)

add_executable(camera-view-tests camera_view_tests.cpp)
target_link_libraries(camera-view-tests PRIVATE nova_media)
target_compile_options(camera-view-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME camera-view-bob COMMAND camera-view-tests)
include(ui-scale.cmake)
include(menu-fonts.cmake)
include(sprite-selection.cmake)

add_executable(pc-snapshot-tests pc_snapshot_tests.cpp)
target_include_directories(pc-snapshot-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_options(pc-snapshot-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME pc-snapshot-contract COMMAND pc-snapshot-tests)

add_test(NAME technical-world-data COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_world_data.py")
add_test(NAME technical-authority-data COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_authority_data.py")
add_test(NAME technical-animation-timing COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_animation_metadata.py")
add_test(NAME technical-hitbox-animation COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_hitbox_metadata.py")
set_tests_properties(technical-authority-data technical-animation-timing technical-hitbox-animation PROPERTIES TIMEOUT 90)
add_executable(native-world-reader-tests native_world_reader_tests.cpp)
target_link_libraries(native-world-reader-tests PRIVATE nova_runtime)
target_compile_options(native-world-reader-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME native-world-readers-original COMMAND native-world-reader-tests
  "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
set_tests_properties(native-world-readers-original PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

add_test(NAME local-layout-guard COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check_local_layout.py")
add_test(NAME local-layout-unit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_local_layout.py")
