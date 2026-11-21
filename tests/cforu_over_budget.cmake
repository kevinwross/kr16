# Negative test for the stage A address-space wall.
#
# Decision 6 (2026-11-21) accepts C-for-U as over budget for stage A. Under the
# krc toolchain its image needs 48,972 B of program code, 35,158 B of fileio and
# the startup stub -- 84,138 B of .text against a 65,536 B address space, over
# by 18,602 B before any libc section -- so the linker refuses the image. The
# wall is recorded as expected behaviour instead of a red suite. KRCompiler 4.17
# (16-bit codegen density) is the tracked work that would reopen it: when krc's
# output shrinks enough to fit, this test fails with the build's real output,
# which is the signal to restore the five positive C-for-U checks. `fileio.c` is
# deliberately not trimmed for this.
#
# Under the built-in compiler the program fits, so the build succeeds and this
# case skips; the positive checks cover C-for-U there.

if(NOT DEFINED KR16 OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "cforu_over_budget: KR16, SOURCE and OUTPUT are required")
endif()

execute_process(
  COMMAND "${KR16}" build --no-debug "${SOURCE}" -o "${OUTPUT}"
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_stdout
  ERROR_VARIABLE build_stderr
)
set(build_output "${build_stdout}${build_stderr}")

if(build_result EQUAL 0)
  # SKIP_REGULAR_EXPRESSION in tests/CMakeLists.txt turns this into a skip.
  message(FATAL_ERROR
    "skip: the build succeeded, so this toolchain fits C-for-U")
endif()

if(build_output MATCHES "ld failed: link relocation overflows its width")
  message(STATUS
    "C-for-U is over budget, as decision 6 records: the linker reported the overflow")
  return()
endif()

message(FATAL_ERROR
  "expected the address-space wall, got (exit ${build_result}):\n${build_output}")
