# V01g integrator proposal only; author did not configure/build/run.
if(NOVA_REAL_TESTS AND (CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release"))
  add_executable(weapon-consumed-ads-provenance-tests EXCLUDE_FROM_ALL weapon_consumed_ads_provenance_tests.cpp)
  target_link_libraries(weapon-consumed-ads-provenance-tests PRIVATE nova_runtime bcrypt)
  target_compile_definitions(weapon-consumed-ads-provenance-tests PRIVATE $<$<CONFIG:Release>:NOVA_V01G_RELEASE=1>)
  target_compile_options(weapon-consumed-ads-provenance-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME weapon-consumed-ads-provenance-original
    COMMAND weapon-consumed-ads-provenance-tests "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
      "${PROJECT_SOURCE_DIR}/local/work/o08-wobble/locomotion-pose/resource"
      "${PROJECT_SOURCE_DIR}/local/work/v01-weapon-position/consumed-ads-provenance/resource"
    CONFIGURATIONS Release)
  set_tests_properties(weapon-consumed-ads-provenance-original PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "real;opt-in;V01g" TIMEOUT 20)
endif()
