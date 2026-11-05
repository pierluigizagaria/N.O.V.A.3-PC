# T09h source-only diagnostic seam checks; sole integrator runs Release.
add_executable(menu-ack-gate-tests menu_ack_gate_tests.cpp)
target_include_directories(menu-ack-gate-tests PRIVATE ../src)
target_compile_options(menu-ack-gate-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME menu-ack-gate COMMAND menu-ack-gate-tests)
add_executable(menu-ack-snapshot-tests menu_ack_snapshot_tests.cpp)
target_link_libraries(menu-ack-snapshot-tests PRIVATE nova_media)
target_compile_options(menu-ack-snapshot-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME menu-ack-snapshot-script COMMAND menu-ack-snapshot-tests)
set_tests_properties(menu-ack-gate menu-ack-snapshot-script PROPERTIES TIMEOUT 20)

add_test(NAME input-menu-ack COMMAND input-tests "${FIXTURE_DIR}/input.so" menu-ack)
set_tests_properties(input-menu-ack PROPERTIES TIMEOUT 20)
