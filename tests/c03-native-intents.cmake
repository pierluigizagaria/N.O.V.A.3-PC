# Pure host intent policy; no guest code or real-library input is executed.
add_executable(native-intents-tests native_intents_tests.cpp)
target_include_directories(native-intents-tests PRIVATE ../src)
target_compile_options(native-intents-tests PRIVATE /W4 /WX /EHsc)
target_link_options(native-intents-tests PRIVATE "/STACK:1048576,1048576")
add_test(NAME input-native-intents COMMAND native-intents-tests)
set_tests_properties(input-native-intents PROPERTIES TIMEOUT 20)

set(AIM_INTENT_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/aim_intent.so")
add_custom_command(OUTPUT "${AIM_INTENT_FIXTURE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC -fno-stack-protector -fno-builtin -fno-omit-frame-pointer -O1
    "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe" -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aim_intent_contract.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/aim_intent_boundary.S"
    -o "${AIM_INTENT_FIXTURE}"
  DEPENDS fixtures/aim_intent_contract.c fixtures/aim_intent_contract.h fixtures/aim_intent_boundary.S
  VERBATIM)
add_custom_target(aim_intent_fixture DEPENDS "${AIM_INTENT_FIXTURE}")
add_executable(native-aim-installed-tests native_aim_installed_tests.cpp)
target_link_libraries(native-aim-installed-tests PRIVATE nova_media)
target_compile_options(native-aim-installed-tests PRIVATE /W4 /WX /EHsc)
target_link_options(native-aim-installed-tests PRIVATE "/STACK:1048576,1048576")
add_dependencies(native-aim-installed-tests aim_intent_fixture)
add_test(NAME input-native-aim-installed COMMAND native-aim-installed-tests "${AIM_INTENT_FIXTURE}")
set_tests_properties(input-native-aim-installed PROPERTIES TIMEOUT 20)
