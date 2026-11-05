# V01s inactive candidate. This target has no guest/SDL/GPU execution path.
add_executable(weapon-position-policy-tests weapon_position_policy_tests.cpp
  ../src/camera/viewmodel_eligibility.cpp)
target_include_directories(weapon-position-policy-tests PRIVATE ../src)
target_compile_definitions(weapon-position-policy-tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(weapon-position-policy-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME weapon-position-policy COMMAND weapon-position-policy-tests)
set_tests_properties(weapon-position-policy PROPERTIES TIMEOUT 20)
# When the peer config/UI patch is integrated, make coexistence assertions mandatory.
if(EXISTS "${PROJECT_SOURCE_DIR}/src/movement_wobble/integration_gate.h")
  target_compile_definitions(fov-config-tests PRIVATE NOVA_V01S_O08G_COEXIST=1)
endif()
