# V01f integrator proposal only; author did not configure/build/run.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(weapon-consumed-pose-tests EXCLUDE_FROM_ALL weapon_consumed_pose_tests.cpp)
  target_link_libraries(weapon-consumed-pose-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(weapon-consumed-pose-tests PRIVATE $<$<CONFIG:Release>:NOVA_V01F_RELEASE=1>)
  target_compile_options(weapon-consumed-pose-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME weapon-consumed-pose-original
    COMMAND weapon-consumed-pose-tests "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
      "${PROJECT_SOURCE_DIR}/local/fixtures/movement/consumed-pose"
    CONFIGURATIONS Release)
  set_tests_properties(weapon-consumed-pose-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;V01f" TIMEOUT 20)
endif()
