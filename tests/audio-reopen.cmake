# Device-free production-accounting tests. Link headers only: every SDL symbol is mocked.
add_executable(audio-reopen-tests audio_reopen_tests.cpp ../src/audio/pcm.cpp)
target_link_libraries(audio-reopen-tests PRIVATE nova_runtime SDL3::Headers)
target_compile_options(audio-reopen-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME audio-reopen-accounting COMMAND audio-reopen-tests
  "${CMAKE_CURRENT_BINARY_DIR}/audio-reopen/$<CONFIG>/positive")
set_tests_properties(audio-reopen-accounting PROPERTIES TIMEOUT 20)

# Discriminating control: generate a private copy with only the pristine boundary removed.
# The real adapter is never edited. The checker requires the specific invariant failure.
set(_audio_reopen_negative "${CMAKE_CURRENT_BINARY_DIR}/audio_reopen_missing_boundary.cpp")
add_custom_command(OUTPUT "${_audio_reopen_negative}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/make_audio_reopen_negative.py"
    "${PROJECT_SOURCE_DIR}/src/audio/opensl.cpp" "${_audio_reopen_negative}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/audio/opensl.cpp" make_audio_reopen_negative.py
  VERBATIM)
add_custom_target(audio-reopen-negative-source DEPENDS "${_audio_reopen_negative}")
add_executable(audio-reopen-negative-tests audio_reopen_tests.cpp ../src/audio/pcm.cpp)
add_dependencies(audio-reopen-negative-tests audio-reopen-negative-source)
target_compile_definitions(audio-reopen-negative-tests PRIVATE
  NOVA_AUDIO_REOPEN_SOURCE="${_audio_reopen_negative}")
target_link_libraries(audio-reopen-negative-tests PRIVATE nova_runtime SDL3::Headers)
target_compile_options(audio-reopen-negative-tests PRIVATE /W4 /WX /EHsc)
add_test(NAME audio-reopen-negative COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/check_audio_reopen_negative.py"
  "$<TARGET_FILE:audio-reopen-negative-tests>"
  "${CMAKE_CURRENT_BINARY_DIR}/audio-reopen/$<CONFIG>/negative")
set_tests_properties(audio-reopen-negative PROPERTIES TIMEOUT 20)
