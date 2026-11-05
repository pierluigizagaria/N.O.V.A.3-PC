# Source-only candidate; no play.cpp activation. Later sole integrator owns execution.
set(POST_DRAIN_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/post_drain.so")
add_custom_command(OUTPUT "${POST_DRAIN_FIXTURE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC -fno-stack-protector -fno-builtin -fno-omit-frame-pointer -O1
    "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe" -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/post_drain_contract.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/post_drain_boundary.S" -o "${POST_DRAIN_FIXTURE}"
  DEPENDS fixtures/post_drain_contract.c fixtures/post_drain_contract.h fixtures/post_drain_boundary.S VERBATIM)
add_custom_target(post_drain_fixture DEPENDS "${POST_DRAIN_FIXTURE}")
add_executable(post-drain-tests post_drain_tests.cpp)
target_link_libraries(post-drain-tests PRIVATE nova_media)
target_compile_options(post-drain-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(post-drain-tests post_drain_fixture)
add_test(NAME input-gamepad-post-drain COMMAND post-drain-tests "${POST_DRAIN_FIXTURE}")
foreach(mode queue-host-packets retain-old-caller omit-role-revocation)
  add_test(NAME input-gamepad-post-drain-negative-${mode} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_post_drain_negative.py" --${mode}
    $<TARGET_FILE:post-drain-tests> "${POST_DRAIN_FIXTURE}")
  set_tests_properties(input-gamepad-post-drain-negative-${mode} PROPERTIES TIMEOUT 20)
endforeach()
set_tests_properties(input-gamepad-post-drain PROPERTIES TIMEOUT 20)
if(NOVA_REAL_TESTS)
  set(POST_DRAIN_ORIGINAL "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  add_test(NAME input-gamepad-post-drain-original-consumer COMMAND post-drain-tests
    "${POST_DRAIN_FIXTURE}" --original "${POST_DRAIN_ORIGINAL}")
  add_test(NAME input-gamepad-post-drain-original-stale-negative COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_post_drain_negative.py" --queue-host-packets
    $<TARGET_FILE:post-drain-tests> "${POST_DRAIN_FIXTURE}" --original "${POST_DRAIN_ORIGINAL}")
  add_test(NAME input-gamepad-post-drain-original-caller-negative COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_post_drain_negative.py" --retain-old-caller
    $<TARGET_FILE:post-drain-tests> "${POST_DRAIN_FIXTURE}" --original "${POST_DRAIN_ORIGINAL}")
  set_tests_properties(input-gamepad-post-drain-original-consumer input-gamepad-post-drain-original-stale-negative input-gamepad-post-drain-original-caller-negative
    PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()
