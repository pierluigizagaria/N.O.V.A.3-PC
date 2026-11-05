# Owned virtual SDL component harness. The production target owns gamepad.cpp.
add_executable(gamepad-tests gamepad_tests.cpp)
target_link_libraries(gamepad-tests PRIVATE nova_media)
target_compile_options(gamepad-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME input-gamepad-virtual COMMAND gamepad-tests)
set_tests_properties(input-gamepad-virtual PROPERTIES TIMEOUT 20)
