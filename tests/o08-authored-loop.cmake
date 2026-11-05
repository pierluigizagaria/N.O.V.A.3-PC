# O08g integrator proposal only. Author did not configure, compile or run.
# Explicit opt-in target, excluded from default builds. Release only.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(movement-authored-loop-tests EXCLUDE_FROM_ALL movement_authored_loop_tests.cpp)
  target_link_libraries(movement-authored-loop-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(movement-authored-loop-tests PRIVATE $<$<CONFIG:Release>:NOVA_O08G_RELEASE=1>)
  target_compile_options(movement-authored-loop-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME movement-authored-loop-original
    COMMAND movement-authored-loop-tests
      "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
      "${PROJECT_SOURCE_DIR}/local/work/o08-wobble/locomotion-pose/resource"
      "${PROJECT_SOURCE_DIR}/local/work/o08-wobble/authored-loop-reduction/resource"
    CONFIGURATIONS Release)
  set_tests_properties(movement-authored-loop-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;O08g" TIMEOUT 20)
endif()

option(NOVA_O08G_CONFIG_PROPOSAL "Enable reviewed inactive O08g config/UI proposal checks" OFF)
if(NOVA_REAL_TESTS AND NOVA_O08G_CONFIG_PROPOSAL AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(movement-wobble-policy-tests EXCLUDE_FROM_ALL movement_wobble_policy_tests.cpp)
  target_link_libraries(movement-wobble-policy-tests PRIVATE nova_media)
  target_compile_definitions(movement-wobble-policy-tests PRIVATE $<$<CONFIG:Release>:NOVA_O08G_RELEASE=1>)
  target_compile_options(movement-wobble-policy-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME movement-wobble-inactive-policy COMMAND movement-wobble-policy-tests CONFIGURATIONS Release)
  set_tests_properties(movement-wobble-inactive-policy PROPERTIES LABELS "source-proposal;O08g" TIMEOUT 20)
endif()
