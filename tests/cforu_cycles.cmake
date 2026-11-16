if(NOT DEFINED KR16 OR NOT DEFINED CFORU_KEX OR NOT DEFINED CFORU_ROOT OR
   NOT DEFINED CFORU_INPUT)
  message(FATAL_ERROR "cforu_cycles requires KR16, CFORU_KEX, CFORU_ROOT, and CFORU_INPUT")
endif()
if(NOT DEFINED CFORU_MAX_CYCLES)
  set(CFORU_MAX_CYCLES 1200000)
endif()

execute_process(
  COMMAND "${KR16}" vm "${CFORU_KEX}" --fs-root "${CFORU_ROOT}"
          --stats -- "${CFORU_INPUT}"
  OUTPUT_VARIABLE run_output
  ERROR_VARIABLE run_error
  RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "C-for-U cycle run failed: ${run_output}${run_error}")
endif()

set(stats "${run_output}${run_error}")
if(NOT stats MATCHES "cycles=([0-9]+)")
  message(FATAL_ERROR "C-for-U cycle run produced no cycle count: ${stats}")
endif()
set(cycles "${CMAKE_MATCH_1}")
if(cycles GREATER CFORU_MAX_CYCLES)
  message(FATAL_ERROR "C-for-U used ${cycles} cycles, limit ${CFORU_MAX_CYCLES}")
endif()
message(STATUS "C-for-U cycles=${cycles} (limit ${CFORU_MAX_CYCLES})")

