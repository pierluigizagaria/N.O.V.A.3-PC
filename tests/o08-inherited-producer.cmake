# O08e integrator proposal only; not configured, built or run by the author.
# Explicit opt-in target, excluded from default builds. Release-only execution.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(movement-inherited-producer-tests EXCLUDE_FROM_ALL movement_inherited_producer_tests.cpp)
  target_link_libraries(movement-inherited-producer-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(movement-inherited-producer-tests PRIVATE $<$<CONFIG:Release>:NOVA_O08E_RELEASE=1>)
  target_compile_options(movement-inherited-producer-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME movement-inherited-producer-original
    COMMAND movement-inherited-producer-tests
      "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
    CONFIGURATIONS Release)
  set_tests_properties(movement-inherited-producer-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;O08e" TIMEOUT 20)
endif()
