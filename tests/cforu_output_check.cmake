if(NOT DEFINED KR16 OR NOT DEFINED CFORU_KEX OR NOT DEFINED CFORU_ROOT)
  message(FATAL_ERROR "cforu_output_check requires KR16, CFORU_KEX, and CFORU_ROOT")
endif()
if(NOT DEFINED CFORU_INPUT OR NOT DEFINED CFORU_OUTPUT)
  message(FATAL_ERROR "cforu_output_check requires CFORU_INPUT and CFORU_OUTPUT")
endif()

set(output_path "${CFORU_ROOT}/${CFORU_OUTPUT}")
file(REMOVE "${output_path}")

execute_process(
  COMMAND "${KR16}" vm "${CFORU_KEX}" --fs-root "${CFORU_ROOT}"
          -- "${CFORU_INPUT}" -o "${CFORU_OUTPUT}"
  OUTPUT_VARIABLE run_output
  ERROR_VARIABLE run_error
  RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "C-for-U failed: ${run_output}${run_error}")
endif()
if(NOT EXISTS "${output_path}")
  message(FATAL_ERROR "C-for-U did not create ${output_path}")
endif()

file(READ "${output_path}" generated)
if(NOT generated MATCHES "; argv\\[")
  message(FATAL_ERROR "C-for-U output is missing argv comments")
endif()
if(NOT generated MATCHES "\\.global fact")
  message(FATAL_ERROR "C-for-U output is missing generated assembly")
endif()
