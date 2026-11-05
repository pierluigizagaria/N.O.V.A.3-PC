add_executable(launcher-probe launcher_probe.cpp)
target_link_libraries(launcher-probe PRIVATE nova_setup shell32)
target_compile_options(launcher-probe PRIVATE /W4 /WX /EHsc)
add_executable(launcher-child launcher_child.cpp)
target_compile_options(launcher-child PRIVATE /W4 /WX /EHsc)
add_test(NAME launcher-setup COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_launcher.py" "$<TARGET_FILE:launcher-probe>"
  "$<TARGET_FILE:launcher-child>" "${PROJECT_SOURCE_DIR}/local/test-profiles/launcher-$<CONFIG>")
set_tests_properties(launcher-setup PROPERTIES TIMEOUT 90)
add_test(NAME launcher-window COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/launcher_window_probe.py" "$<TARGET_FILE:nova3pc-launcher>")
set_tests_properties(launcher-window PROPERTIES TIMEOUT 30)
add_test(NAME launcher-apk-picker COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/launcher_window_probe.py" "$<TARGET_FILE:nova3pc-launcher>" --setup)
set_tests_properties(launcher-apk-picker PROPERTIES TIMEOUT 40)
if(NOVA_REAL_TESTS)
  add_test(NAME launcher-appearance-original COMMAND nova3pc-launcher --verify-appearance)
  set_tests_properties(launcher-appearance-original PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
endif()
add_test(NAME launcher-package COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_package.py")
set_tests_properties(launcher-package PROPERTIES TIMEOUT 30)
