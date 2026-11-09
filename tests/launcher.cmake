add_executable(launcher-probe launcher_probe.cpp)
target_link_libraries(launcher-probe PRIVATE nova_setup nova_connection shell32)
target_compile_options(launcher-probe PRIVATE /W4 /WX /EHsc)
add_executable(launcher-child launcher_child.cpp)
target_compile_options(launcher-child PRIVATE /W4 /WX /EHsc)
add_test(NAME launcher-setup COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_launcher.py" "$<TARGET_FILE:launcher-probe>"
  "$<TARGET_FILE:launcher-child>" "${CMAKE_CURRENT_BINARY_DIR}/runtime/launcher-$<CONFIG>")
set_tests_properties(launcher-setup PROPERTIES TIMEOUT 90)
add_test(NAME launcher-window CONFIGURATIONS Debug COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/launcher_window_probe.py" "$<TARGET_FILE:nova3pc-launcher>")
set_tests_properties(launcher-window PROPERTIES TIMEOUT 30)
add_test(NAME launcher-apk-picker COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/launcher_window_probe.py" "$<TARGET_FILE:nova3pc-launcher>" --setup)
set_tests_properties(launcher-apk-picker PROPERTIES TIMEOUT 40)
if(NOVA_REAL_TESTS)
  add_test(NAME launcher-appearance-original CONFIGURATIONS Debug COMMAND nova3pc-launcher --verify-appearance)
  set_tests_properties(launcher-appearance-original PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()
add_test(NAME launcher-package COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_package.py")
set_tests_properties(launcher-package PROPERTIES TIMEOUT 30)

add_executable(launcher-paint-tests launcher_paint_tests.cpp)
target_link_libraries(launcher-paint-tests PRIVATE nova_setup)
target_compile_options(launcher-paint-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME launcher-paint COMMAND launcher-paint-tests)
set_tests_properties(launcher-paint PROPERTIES TIMEOUT 30)

add_executable(server-selection-tests server_selection_tests.cpp)
target_link_libraries(server-selection-tests PRIVATE nova_connection)
target_compile_options(server-selection-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME server-selection COMMAND server-selection-tests
  "${CMAKE_CURRENT_BINARY_DIR}/runtime/server-selection-$<CONFIG>")
set_tests_properties(server-selection PROPERTIES TIMEOUT 25)

add_executable(build-interface-tests build_interface_tests.cpp)
target_include_directories(build-interface-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_options(build-interface-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME build-interface COMMAND build-interface-tests)
add_test(NAME product-cli COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/product_cli.py"
  "$<TARGET_FILE:nova3pc>" "$<CONFIG>")
set_tests_properties(build-interface product-cli PROPERTIES TIMEOUT 30)
