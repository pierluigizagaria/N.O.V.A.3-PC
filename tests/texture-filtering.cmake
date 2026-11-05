set(TEXTURE_FIXTURE "${FIXTURE_DIR}/texture_filtering.so")
add_custom_command(OUTPUT "${TEXTURE_FIXTURE}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android -shared -nostdlib -fPIC -fno-stack-protector
    "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe" -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/texture_filtering.c" -x assembler
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/texture_filtering.S" -o "${TEXTURE_FIXTURE}"
  DEPENDS fixtures/texture_filtering.c fixtures/texture_filtering.S VERBATIM)
add_custom_target(texture_filtering_fixture DEPENDS "${TEXTURE_FIXTURE}")
add_executable(texture-filtering-tests texture_filtering_tests.cpp)
target_link_libraries(texture-filtering-tests PRIVATE nova_media)
target_compile_options(texture-filtering-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(texture-filtering-tests texture_filtering_fixture)
add_test(NAME texture-filtering COMMAND texture-filtering-tests "${TEXTURE_FIXTURE}" --synthetic)
set_tests_properties(texture-filtering PROPERTIES TIMEOUT 30)
if(NOVA_REAL_TESTS)
  add_test(NAME texture-filtering-real COMMAND texture-filtering-tests
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so" --real)
  set_tests_properties(texture-filtering-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()
