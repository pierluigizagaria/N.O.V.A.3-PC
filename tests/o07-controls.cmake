# Integrator: include(o07-controls) from tests/CMakeLists.txt after nova_media exists.
# No production CMake source entry is needed: implementation is in controls.cpp.
add_executable(controls-rebinding-tests controls_rebinding_tests.cpp)
target_link_libraries(controls-rebinding-tests PRIVATE nova_media)
target_compile_options(controls-rebinding-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME input-controls-rebinding COMMAND controls-rebinding-tests)
set_tests_properties(input-controls-rebinding PROPERTIES TIMEOUT 20)
