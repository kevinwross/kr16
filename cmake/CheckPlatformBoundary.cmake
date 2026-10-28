# Enforce the platform boundary: host OS headers belong only under src/platform.
#
# Invoked by CTest:
#   cmake -DKR16_SOURCE_DIR=<root> -P cmake/CheckPlatformBoundary.cmake

if(NOT DEFINED KR16_SOURCE_DIR)
  message(FATAL_ERROR "KR16_SOURCE_DIR is required")
endif()

set(platform_headers
  "windows.h"
  "unistd.h"
  "sys/socket.h"
  "netinet/in.h"
  "arpa/inet.h"
  "termios.h"
  "conio.h"
  "io.h"
  "direct.h"
  "pthread.h"
)

file(GLOB_RECURSE source_files
  "${KR16_SOURCE_DIR}/src/*.c"
  "${KR16_SOURCE_DIR}/src/*.cc"
  "${KR16_SOURCE_DIR}/src/*.cpp"
  "${KR16_SOURCE_DIR}/src/*.h"
  "${KR16_SOURCE_DIR}/src/*.hpp"
  "${KR16_SOURCE_DIR}/include/*.h"
  "${KR16_SOURCE_DIR}/include/*.hpp"
)

set(violations "")
foreach(file IN LISTS source_files)
  if(file MATCHES "/src/platform/")
    continue()
  endif()
  file(READ "${file}" contents)
  foreach(header IN LISTS platform_headers)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"]${header}[>\"]")
      list(APPEND violations "${file}: ${header}")
    endif()
  endforeach()
endforeach()

if(violations)
  string(REPLACE ";" "\n  " formatted "${violations}")
  message(FATAL_ERROR
    "Platform headers used outside src/platform:\n  ${formatted}\n"
    "Move host-specific code under src/platform and expose a portable API.")
endif()
