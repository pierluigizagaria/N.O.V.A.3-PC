# Integrator proposal only. Not configured or executed by V01d.
# Explicit target: excluded from default builds; only Release test registration.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(weapon-presentation-interval-tests EXCLUDE_FROM_ALL weapon_presentation_interval_tests.cpp)
  target_link_libraries(weapon-presentation-interval-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(weapon-presentation-interval-tests PRIVATE $<$<CONFIG:Release>:NOVA_V01D_RELEASE=1>)
  target_compile_options(weapon-presentation-interval-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME weapon-presentation-interval-original
    COMMAND weapon-presentation-interval-tests
      "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
    CONFIGURATIONS Release)
  set_tests_properties(weapon-presentation-interval-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;V01d" TIMEOUT 20)
endif()
