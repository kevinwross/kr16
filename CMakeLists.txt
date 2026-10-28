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
endif()

add_library(kr16_core STATIC
  src/core/version.cpp
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
  endif()
endif()
