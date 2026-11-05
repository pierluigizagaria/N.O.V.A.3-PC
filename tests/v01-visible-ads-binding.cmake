# V01e integrator proposal only; author did not configure/build/run.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(weapon-visible-ads-binding-tests EXCLUDE_FROM_ALL weapon_visible_ads_binding_tests.cpp)
  target_link_libraries(weapon-visible-ads-binding-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(weapon-visible-ads-binding-tests PRIVATE $<$<CONFIG:Release>:NOVA_V01E_RELEASE=1>)
  target_compile_options(weapon-visible-ads-binding-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME weapon-visible-ads-binding-original
    COMMAND weapon-visible-ads-binding-tests "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
    CONFIGURATIONS Release)
  set_tests_properties(weapon-visible-ads-binding-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;V01e" TIMEOUT 20)
endif()
