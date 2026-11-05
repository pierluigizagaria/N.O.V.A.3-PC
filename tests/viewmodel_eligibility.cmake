# Optional contract-only target for Root's future Release verification.
# Do not include in the current C05 candidate. The connected native producer is covered by the separate native target.
add_executable(viewmodel-eligibility-tests viewmodel_eligibility_tests.cpp
  "${PROJECT_SOURCE_DIR}/src/camera/viewmodel_eligibility.cpp"
  "${PROJECT_SOURCE_DIR}/src/camera/world_fov.cpp")
target_include_directories(viewmodel-eligibility-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(viewmodel-eligibility-tests PRIVATE cxx_std_20)
target_compile_options(viewmodel-eligibility-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME camera-viewmodel-eligibility-contract COMMAND viewmodel-eligibility-tests)
set_tests_properties(camera-viewmodel-eligibility-contract PROPERTIES TIMEOUT 20)
