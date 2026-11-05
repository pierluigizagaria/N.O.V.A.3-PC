# T13c: source-only activation gate; execution belongs to the sole integrator.
add_test(NAME input-gamepad-play-routing COMMAND post-drain-tests "${POST_DRAIN_FIXTURE}" --play-routing)
set_tests_properties(input-gamepad-play-routing PROPERTIES TIMEOUT 30)
foreach(mode poll gate)
  add_test(NAME input-gamepad-play-routing-negative-${mode} COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_play_gamepad_negative.py" --omit-play-${mode}
    $<TARGET_FILE:post-drain-tests> "${POST_DRAIN_FIXTURE}")
  set_tests_properties(input-gamepad-play-routing-negative-${mode} PROPERTIES TIMEOUT 30)
endforeach()
if(NOVA_REAL_TESTS)
  add_test(NAME input-gamepad-play-routing-original COMMAND post-drain-tests
    "${POST_DRAIN_FIXTURE}" --play-routing --original "${POST_DRAIN_ORIGINAL}")
  set_tests_properties(input-gamepad-play-routing-original PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
  foreach(mode poll gate)
    add_test(NAME input-gamepad-play-routing-original-negative-${mode} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check_play_gamepad_negative.py" --omit-play-${mode}
      $<TARGET_FILE:post-drain-tests> "${POST_DRAIN_FIXTURE}" --original "${POST_DRAIN_ORIGINAL}")
    set_tests_properties(input-gamepad-play-routing-original-negative-${mode}
      PROPERTIES SKIP_RETURN_CODE 77 LABELS "real;opt-in" TIMEOUT 30)
  endforeach()
endif()
