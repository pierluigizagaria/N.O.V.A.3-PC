set(NATIVE_DETAIL_FIXTURE "${FIXTURE_DIR}/native_detail.so")
add_custom_command(OUTPUT "${NATIVE_DETAIL_FIXTURE}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC -x assembler "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe"
    -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/native_detail.S" -o "${NATIVE_DETAIL_FIXTURE}"
  DEPENDS fixtures/native_detail.S VERBATIM)
add_custom_target(native_detail_fixture DEPENDS "${NATIVE_DETAIL_FIXTURE}")
add_executable(native-detail-tests native_detail_tests.cpp)
target_link_libraries(native-detail-tests PRIVATE nova_media)
target_compile_options(native-detail-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(native-detail-tests native_detail_fixture)
add_test(NAME native-detail COMMAND native-detail-tests "${NATIVE_DETAIL_FIXTURE}" --synthetic)
set_tests_properties(native-detail PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  add_test(NAME native-detail-real COMMAND native-detail-tests
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" --real)
  set_tests_properties(native-detail-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()
