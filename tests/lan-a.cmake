# Opt in only when integrate_verify_play schedules LAN-A after O09 priority.
option(NOVA_LAN_A_TESTS "Build and register synthetic LAN-A loopback tests" OFF)
if(NOT NOVA_LAN_A_TESTS)
  return()
endif()
set(LAN_A_RUN_ID "" CACHE STRING "Unique integrator-owned LAN-A log run identifier")
if(NOT LAN_A_RUN_ID MATCHES "^[A-Za-z0-9_-]+$")
  message(FATAL_ERROR "Set a fresh LAN_A_RUN_ID using letters, digits, underscore or hyphen")
endif()
set(LAN_A_FIXTURE "${FIXTURE_DIR}/lan_a.so")
add_custom_command(OUTPUT "${LAN_A_FIXTURE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIXTURE_DIR}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC -fno-stack-protector -fno-builtin
    -fno-omit-frame-pointer -O1 -g
    "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe" -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/lan_a.c" -o "${LAN_A_FIXTURE}"
  DEPENDS fixtures/lan_a.c VERBATIM)
add_custom_target(lan_a_fixture DEPENDS "${LAN_A_FIXTURE}")
add_executable(lan-a-tests lan_a_tests.cpp)
target_link_libraries(lan-a-tests PRIVATE nova_runtime)
target_compile_options(lan-a-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(lan-a-tests lan_a_fixture)
foreach(case baseline client-bind reply-endpoint omit-reply dirty-address
             short-address lost-source-port errno98 omit-close)
  add_test(NAME network-lan-a-${case} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_lan_a.py"
    --exe "$<TARGET_FILE:lan-a-tests>" --fixture "${LAN_A_FIXTURE}"
    --case "${case}" --logs "${PROJECT_SOURCE_DIR}/local/logs/lan-a/runtime-${LAN_A_RUN_ID}/${case}"
    CONFIGURATIONS Release)
  set_tests_properties(network-lan-a-${case} PROPERTIES TIMEOUT 30
    RUN_SERIAL TRUE RESOURCE_LOCK lan_udp7891 LABELS "synthetic;opt-in;lan-a"
    SKIP_RETURN_CODE 77)
endforeach()
