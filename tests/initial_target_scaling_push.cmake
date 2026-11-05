# Each subprocess must fail at the exact unchanged guard boundary.
set(boundary viewport_scope_contract)
if(CASE STREQUAL "bad-viewport" OR CASE STREQUAL "bad-uv")
  set(boundary viewport_or_uv_contract)
endif()
execute_process(COMMAND "${EXE}" "push-${CASE}" "${FIXTURE}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 86 OR NOT err MATCHES "boundary=${boundary}")
  message(FATAL_ERROR "Expected unsuccessful86/${boundary}, got ${code}: ${err}")
endif()
