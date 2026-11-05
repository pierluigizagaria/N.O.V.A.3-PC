# Integrate/run only when integrate_verify_play schedules this component.
option(NOVA_INTERFACE_IOCTL_TESTS "Build synthetic Android32 interface ioctl tests" OFF)
if(NOT NOVA_INTERFACE_IOCTL_TESTS)
  return()
endif()
set(INTERFACE_IOCTL_FIXTURE "${FIXTURE_DIR}/interface_ioctl.so")
add_custom_command(OUTPUT "${INTERFACE_IOCTL_FIXTURE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${FIXTURE_DIR}"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC -fno-stack-protector -fno-builtin
    -fno-omit-frame-pointer -O1 -g
    "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe" -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/interface_ioctl.c" -o "${INTERFACE_IOCTL_FIXTURE}"
  DEPENDS fixtures/interface_ioctl.c VERBATIM)
add_custom_target(interface_ioctl_fixture DEPENDS "${INTERFACE_IOCTL_FIXTURE}")
add_executable(interface-ioctl-tests interface_ioctl_tests.cpp)
target_link_libraries(interface-ioctl-tests PRIVATE nova_runtime)
target_compile_options(interface-ioctl-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(interface-ioctl-tests interface_ioctl_fixture)
foreach(case baseline omit-dispatch host-layout oper-up)
  add_test(NAME network-interface-ioctl-${case} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_interface_ioctl.py"
    --exe "$<TARGET_FILE:interface-ioctl-tests>" --fixture "${INTERFACE_IOCTL_FIXTURE}" --case "${case}"
    CONFIGURATIONS Release)
  set_tests_properties(network-interface-ioctl-${case} PROPERTIES TIMEOUT 20
    LABELS "synthetic;opt-in;interface-ioctl")
endforeach()
