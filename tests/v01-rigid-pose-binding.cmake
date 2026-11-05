# V01h integrator proposal only; author did not configure/build/run.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(weapon-rigid-pose-binding-tests EXCLUDE_FROM_ALL weapon_rigid_pose_binding_tests.cpp)
  target_link_libraries(weapon-rigid-pose-binding-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(weapon-rigid-pose-binding-tests PRIVATE $<$<CONFIG:Release>:NOVA_V01H_RELEASE=1>)
  target_compile_options(weapon-rigid-pose-binding-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME weapon-rigid-pose-binding-original
    COMMAND weapon-rigid-pose-binding-tests "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
      "${PROJECT_SOURCE_DIR}/local/work/v01-weapon-position/rigid-pose-binding/resource"
    CONFIGURATIONS Release)
  set_tests_properties(weapon-rigid-pose-binding-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;V01h" TIMEOUT 20)
endif()
