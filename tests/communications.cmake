set(COMMUNICATIONS_FIXTURE "${FIXTURE_DIR}/communications.so")
add_custom_command(OUTPUT "${COMMUNICATIONS_FIXTURE}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe"
    -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/communications.S" -o "${COMMUNICATIONS_FIXTURE}"
  DEPENDS fixtures/communications.S VERBATIM)
add_custom_target(communications_fixture DEPENDS "${COMMUNICATIONS_FIXTURE}")
add_executable(communications-tests communications_tests.cpp panel_movement_tests.cpp)
target_link_libraries(communications-tests PRIVATE nova_media)
target_compile_options(communications-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(communications-tests communications_fixture)
add_test(NAME input-communications COMMAND communications-tests "${COMMUNICATIONS_FIXTURE}" --synthetic)
set_tests_properties(input-communications PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME input-communications-real COMMAND communications-tests
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" --real)
  set_tests_properties(input-communications-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
