# Include after FIXTURE_DIR/native-world-fov-tests registration.
add_test(NAME camera-render-extent-native COMMAND native-world-fov-tests "${FIXTURE_DIR}/world_fov.so" --render-extent)
set_tests_properties(camera-render-extent-native PROPERTIES TIMEOUT 30)
if(NOVA_REAL_TESTS)
  add_executable(native-render-apply-tests native_render_apply_tests.cpp)
  target_link_libraries(native-render-apply-tests PRIVATE nova_media)
  target_compile_options(native-render-apply-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME native-render-apply-real COMMAND native-render-apply-tests
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so")
  set_tests_properties(native-render-apply-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()
