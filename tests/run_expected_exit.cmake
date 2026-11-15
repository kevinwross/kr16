if(NOT DEFINED KR16 OR NOT DEFINED IMAGE OR NOT DEFINED EXPECTED)
  message(FATAL_ERROR "KR16, IMAGE, and EXPECTED are required")
endif()
execute_process(
  COMMAND "${KR16}" vm "${IMAGE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL EXPECTED)
  message(FATAL_ERROR "expected exit ${EXPECTED}, got ${result}: ${output}${error}")
endif()
