if(NOT DEFINED KR16 OR NOT DEFINED CFORU_KEX OR NOT DEFINED GENDIR)
  message(FATAL_ERROR "cforu_check requires KR16, CFORU_KEX, and GENDIR")
endif()
if(NOT DEFINED CFORU_ROOT OR NOT DEFINED CFORU_INPUT)
  message(FATAL_ERROR "cforu_check requires CFORU_ROOT and CFORU_INPUT")
endif()

execute_process(
  COMMAND "${KR16}" vm "${CFORU_KEX}" --fs-root "${CFORU_ROOT}"
          "${CFORU_INPUT}"
  OUTPUT_VARIABLE generated
  ERROR_VARIABLE generated_error
  RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "C-for-U failed: ${generated_error}")
endif()

set(generated_s "${GENDIR}/C-for-U.generated.s")
set(generated_kex "${GENDIR}/C-for-U.generated.kex")
file(WRITE "${generated_s}" "${generated}")

execute_process(
  COMMAND "${KR16}" build --libc "${generated_s}" -o "${generated_kex}"
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error
  RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "generated assembly failed to build: ${build_output}${build_error}")
endif()

execute_process(
  COMMAND "${KR16}" vm "${generated_kex}"
  OUTPUT_VARIABLE generated_run
  ERROR_VARIABLE generated_run_error
  RESULT_VARIABLE generated_run_result)
if(NOT generated_run_result EQUAL 0)
  message(FATAL_ERROR "generated program failed: ${generated_run_error}")
endif()
if(NOT generated_run MATCHES "sum=30 fact=120")
  message(FATAL_ERROR "unexpected generated output: ${generated_run}")
endif()
