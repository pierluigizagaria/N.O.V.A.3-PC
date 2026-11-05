# O08f integrator proposal only; author did not configure, compile or run.
# Explicit opt-in target, excluded from default builds. Release only.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(movement-weapon-locomotion-tests EXCLUDE_FROM_ALL movement_weapon_locomotion_tests.cpp)
  target_link_libraries(movement-weapon-locomotion-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(movement-weapon-locomotion-tests PRIVATE $<$<CONFIG:Release>:NOVA_O08F_RELEASE=1>)
  target_compile_options(movement-weapon-locomotion-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME movement-weapon-locomotion-original
    COMMAND movement-weapon-locomotion-tests
      "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
      "${PROJECT_SOURCE_DIR}/local/work/o08-wobble/locomotion-pose/resource"
    CONFIGURATIONS Release)
  set_tests_properties(movement-weapon-locomotion-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;O08f" TIMEOUT 20)
endif()
