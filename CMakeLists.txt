cmake_minimum_required(VERSION 3.25)

project(kr16 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KR16_BUILD_TESTS "Build the KR16 test suite" ON)
option(KR16_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus /Brepro)
  add_link_options(/Brepro)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
  if(KR16_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

add_library(kr16_core STATIC
  src/core/version.cpp
  src/cli/commands.cpp
  src/container/container.cpp
  src/container/crc32.cpp
  src/container/debug_objects.cpp
  src/instruction/instruction.cpp
  src/cpu/cpu.cpp
  src/asm/assembler.cpp
  src/dis/disassembler.cpp
)
target_include_directories(kr16_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(kr16_core PUBLIC KR16_VERSION="${PROJECT_VERSION}")

add_executable(kr16
  src/cli/main.cpp
)
target_link_libraries(kr16 PRIVATE kr16_core)

if(KR16_BUILD_TESTS)
  include(CTest)
  if(BUILD_TESTING)
    add_subdirectory(tests)
    add_test(NAME kr16.platform-boundary
      COMMAND "${CMAKE_COMMAND}"
        -DKR16_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake"
    )
    set_tests_properties(kr16.platform-boundary PROPERTIES LABELS "unit;policy")
  endif()
endif()
