set(STARTUP_FIXTURE "${FIXTURE_DIR}/startup_logo.so")
add_custom_command(OUTPUT "${STARTUP_FIXTURE}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe"
    -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/startup_logo.S" -o "${STARTUP_FIXTURE}"
  DEPENDS fixtures/startup_logo.S VERBATIM)
add_custom_target(startup_fixture DEPENDS "${STARTUP_FIXTURE}")
add_executable(startup-tests startup_tests.cpp)
target_link_libraries(startup-tests PRIVATE nova_media)
target_compile_options(startup-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(startup-tests startup_fixture)
add_test(NAME startup-contract COMMAND startup-tests "${STARTUP_FIXTURE}" --synthetic)
set_tests_properties(startup-contract PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME startup-original COMMAND startup-tests
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" --real)
  set_tests_properties(startup-original PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()
add_executable(gui-entry-child WIN32 gui_entry_child.cpp ../src/platform/game_entry.cpp)
target_compile_options(gui-entry-child PRIVATE /W4 /WX /EHsc)
add_test(NAME startup-gui-entry COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_gui_entry.py" "$<TARGET_FILE:gui-entry-child>" "$<TARGET_FILE:nova3pc>")
set_tests_properties(startup-gui-entry PROPERTIES TIMEOUT 20)
add_executable(startup-window-tests startup_window_tests.cpp)
target_link_libraries(startup-window-tests PRIVATE nova_media)
target_compile_options(startup-window-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME startup-window COMMAND startup-window-tests)
set_tests_properties(startup-window PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
