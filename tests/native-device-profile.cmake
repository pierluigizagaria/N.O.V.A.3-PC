# T11d component only. No production activation or native1920 rendering follows.
if(NOVA_REAL_TESTS)
  add_executable(native-device-profile-tests native_device_profile_tests.cpp)
  target_link_libraries(native-device-profile-tests PRIVATE nova_media)
  target_compile_options(native-device-profile-tests PRIVATE /W4 /WX /EHsc)
  add_test(NAME native-device-profile-real COMMAND native-device-profile-tests
    "${PROJECT_SOURCE_DIR}/local/input/lib/x86/libNOVA3_neon.so"
    "${CMAKE_CURRENT_BINARY_DIR}/runtime/$<CONFIG>/device-profile")
  set_tests_properties(native-device-profile-real PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
endif()
