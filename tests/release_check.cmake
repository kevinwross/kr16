# The release policy row: what a release is made of, and where it is made.
#
# This used to read `.github/workflows/release.yml` and assert the jobs and the
# artefacts it published. The workflow was removed on 2026-11-23 because the gates
# run on machines this project owns, so the row now asserts the policy that
# replaced it: nothing is built on a hosted runner, and packaging is done by the
# two scripts that ship the same artefacts.
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

# No hosted workflow may come back without this row being changed deliberately.
if(EXISTS "${SOURCE_DIR}/.github/workflows")
  file(GLOB leftovers "${SOURCE_DIR}/.github/workflows/*")
  if(leftovers)
    message(FATAL_ERROR "a hosted workflow is present again: ${leftovers}")
  endif()
endif()

# Packaging is local, one script per platform, and each ships the debugger VSIX.
foreach(script "scripts/package.ps1" "scripts/package.sh")
  if(NOT EXISTS "${SOURCE_DIR}/${script}")
    message(FATAL_ERROR "the release path is missing ${script}")
  endif()
  file(READ "${SOURCE_DIR}/${script}" contents)
  string(FIND "${contents}" "kr16-debug-" vsix)
  if(vsix EQUAL -1)
    message(FATAL_ERROR "${script} does not ship the debugger VSIX")
  endif()
endforeach()

message(STATUS "release is packaged locally and ships the VSIX")
