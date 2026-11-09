add_executable(ui-scale-tests ui_scale_tests.cpp ../src/ui/native_scale.cpp ../src/ui/native_hud_scale.cpp ../src/ui/native_hud_nodes.cpp)
target_compile_definitions(ui-scale-tests PRIVATE NOVA_UI_SCALE_FIXTURE=1 NativeScale=FixtureNativeScale NativeHudScale=FixtureNativeHudScale NativeHudNodes=FixtureNativeHudNodes)
target_compile_options(ui-scale-tests PRIVATE /W4 /WX /EHsc)
target_link_libraries(ui-scale-tests PRIVATE nova_media)
add_test(NAME ui-scale-connected COMMAND ui-scale-tests)
set(UI_CHECKPOINT_FIXTURE "${FIXTURE_DIR}/ui_checkpoint.so")
add_custom_command(OUTPUT "${UI_CHECKPOINT_FIXTURE}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe"
    -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ui_checkpoint.S" -o "${UI_CHECKPOINT_FIXTURE}"
  DEPENDS fixtures/ui_checkpoint.S VERBATIM)
add_custom_target(ui_checkpoint_fixture DEPENDS "${UI_CHECKPOINT_FIXTURE}")
add_dependencies(ui-scale-tests ui_checkpoint_fixture)
add_test(NAME ui-scale-checkpoint COMMAND ui-scale-tests "${UI_CHECKPOINT_FIXTURE}" --checkpoint)
set_tests_properties(ui-scale-connected PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME ui-scale-hooks-real COMMAND ui-scale-tests "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(ui-scale-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
