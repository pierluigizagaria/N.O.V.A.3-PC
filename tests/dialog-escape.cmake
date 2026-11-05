set(DIALOG_ESCAPE_FIXTURE "${FIXTURE_DIR}/dialog_escape.so")
add_custom_command(OUTPUT "${DIALOG_ESCAPE_FIXTURE}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe"
    -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dialog_escape.S" -o "${DIALOG_ESCAPE_FIXTURE}"
  DEPENDS fixtures/dialog_escape.S VERBATIM)
add_custom_target(dialog_escape_fixture DEPENDS "${DIALOG_ESCAPE_FIXTURE}")
add_executable(dialog-escape-tests dialog_escape_tests.cpp)
target_link_libraries(dialog-escape-tests PRIVATE nova_media)
target_compile_options(dialog-escape-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(dialog-escape-tests dialog_escape_fixture)
add_test(NAME menu-dialog-escape COMMAND dialog-escape-tests "${DIALOG_ESCAPE_FIXTURE}" --synthetic)
set_tests_properties(menu-dialog-escape PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME menu-dialog-escape-real COMMAND dialog-escape-tests
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" --real)
  set_tests_properties(menu-dialog-escape-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()
