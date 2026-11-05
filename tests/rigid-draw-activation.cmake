# V01i production binding/native authored fixture gate. Sole integrator, Release only.
if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE STREQUAL "Release")
  add_test(NAME camera-rigid-draw-binding-native
    COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so" --rigid-draw
    CONFIGURATIONS Release)
  set_tests_properties(camera-rigid-draw-binding-native PROPERTIES TIMEOUT 30 LABELS "V01i;production-seam")
endif()
