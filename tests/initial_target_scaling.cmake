# Source-author artifact. Sole integrator configures/builds/runs Release only.
add_executable(initial-target-scaling-tests initial_target_scaling_tests.cpp
  "${PROJECT_SOURCE_DIR}/src/menu/initial_target_scaling.cpp"
  "${PROJECT_SOURCE_DIR}/src/menu/native_menu_observer.cpp")
target_link_libraries(initial-target-scaling-tests PRIVATE nova_runtime)
target_compile_definitions(initial-target-scaling-tests PRIVATE NOVA_INITIAL_TARGET_SCALING_FIXTURE=1 NOVA_MENU_OBSERVER_FIXTURE=1)
target_compile_options(initial-target-scaling-tests PRIVATE /W4 /WX /EHsc)
add_dependencies(initial-target-scaling-tests menu_observer_fixture)
add_test(NAME initial-target-scaling-mocked COMMAND initial-target-scaling-tests "${MENU_OBSERVER_FIXTURE}")
add_test(NAME initial-target-scaling-null-color COMMAND "${CMAKE_COMMAND}"
  "-DEXE=$<TARGET_FILE:initial-target-scaling-tests>" "-DFIXTURE=${MENU_OBSERVER_FIXTURE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/initial_target_scaling_null.cmake")
set_tests_properties(initial-target-scaling-mocked initial-target-scaling-null-color PROPERTIES TIMEOUT 20)

foreach(case IN ITEMS wrong-app wrong-scene wrong-reference early-push repeat-push bad-viewport bad-uv)
  add_test(NAME initial-target-scaling-push-${case} COMMAND "${CMAKE_COMMAND}"
    "-DEXE=$<TARGET_FILE:initial-target-scaling-tests>" "-DFIXTURE=${MENU_OBSERVER_FIXTURE}"
    "-DCASE=${case}" -P "${CMAKE_CURRENT_SOURCE_DIR}/initial_target_scaling_push.cmake")
  set_tests_properties(initial-target-scaling-push-${case} PROPERTIES TIMEOUT 20)
endforeach()
if(NOVA_REAL_TESTS)
  add_test(NAME initial-target-scaling-hooks-real COMMAND initial-target-scaling-tests --real-hooks
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(initial-target-scaling-hooks-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 20)
endif()
