# Include once from tests/CMakeLists.txt after NOVA_LLVM_BIN is resolved.
# Authored for the sole integrator; this worker did not configure/build/run it.
set(MENU_OBSERVER_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/menu_observer.so")
add_custom_command(OUTPUT "${MENU_OBSERVER_FIXTURE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
  COMMAND "${NOVA_LLVM_BIN}/clang.exe" --target=i686-linux-android
    -shared -nostdlib -fPIC -fno-stack-protector -fno-builtin -fno-omit-frame-pointer -O1 -g
    "--ld-path=${NOVA_LLVM_BIN}/ld.lld.exe" -Wl,--hash-style=sysv -Wl,-Bsymbolic
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/menu_observer_contract.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/menu_observer_boundary.S"
    -o "${MENU_OBSERVER_FIXTURE}"
  DEPENDS fixtures/menu_observer_contract.c fixtures/menu_observer_contract.h
    fixtures/menu_observer_boundary.S VERBATIM)
add_custom_target(menu_observer_fixture DEPENDS "${MENU_OBSERVER_FIXTURE}")
add_executable(native-menu-observer-tests native_menu_observer_tests.cpp
  "${PROJECT_SOURCE_DIR}/src/menu/native_menu_observer.cpp"
  "${PROJECT_SOURCE_DIR}/src/menu/initial_target_scaling.cpp")
target_link_libraries(native-menu-observer-tests PRIVATE nova_runtime)
target_compile_definitions(native-menu-observer-tests PRIVATE NOVA_MENU_OBSERVER_FIXTURE=1)
target_compile_options(native-menu-observer-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(native-menu-observer-tests menu_observer_fixture)
add_test(NAME menu-observer-synthetic COMMAND native-menu-observer-tests "${MENU_OBSERVER_FIXTURE}")
set_tests_properties(menu-observer-synthetic PROPERTIES TIMEOUT 30)
