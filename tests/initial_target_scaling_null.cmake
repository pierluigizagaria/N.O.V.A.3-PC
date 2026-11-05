execute_process(COMMAND "${EXE}" null-color "${FIXTURE}" RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT code EQUAL 86 OR NOT err MATCHES "native_color_factory_null" OR NOT err MATCHES "original-null-after-fallback")
  message(FATAL_ERROR "Expected named unsuccessful86, got ${code}: ${err}")
endif()
