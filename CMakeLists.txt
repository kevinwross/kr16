cmake_minimum_required(VERSION 3.25)

project(kr16 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KR16_BUILD_TESTS "Build the KR16 test suite" ON)
option(KR16_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
set(KR16_UI_DEFAULT OFF)
if(WIN32 OR UNIX)
  set(KR16_UI_DEFAULT ON)
endif()
option(KR16_BUILD_UI "Build the windowed debugger UI" ${KR16_UI_DEFAULT})
set(KR16_SDL3_SOURCE_DIR "" CACHE PATH
    "Optional SDL3 source tree; otherwise a pinned release is fetched")

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
  src/cli/build.cpp
  src/cli/shell.cpp
  src/container/container.cpp
  src/container/crc32.cpp
  src/container/debug_objects.cpp
  src/instruction/instruction.cpp
  src/cpu/cpu.cpp
  src/asm/assembler.cpp
  src/dis/disassembler.cpp
  src/vm/machine.cpp
  src/vm/runner.cpp
  src/vm/host.cpp
  src/link/linker.cpp
  src/debug/debug_info.cpp
  src/debug/debug_session.cpp
  src/cc/compiler.cpp
  src/resources/embedded.cpp
  src/resources/embedded_libc.cpp
  src/platform/socket.cpp
  src/platform/terminal.cpp
  src/dap/dap_json.cpp
  src/dap/dap_session.cpp
  src/dap/dap_server.cpp
)
target_include_directories(kr16_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(kr16_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(kr16_core PUBLIC KR16_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_link_libraries(kr16_core PRIVATE ws2_32)
endif()

add_executable(kr16
  src/cli/main.cpp
)
target_link_libraries(kr16 PRIVATE kr16_core)

if(KR16_BUILD_UI)
  set(KR16_UI_SOURCES
    src/ui/app.cpp
    src/ui/help.cpp
    src/resources/embedded_fonts.cpp
  )

  if(WIN32)
    list(APPEND KR16_UI_SOURCES
      src/ui/run_win32.cpp
      src/platform/ui/win32/win32_launcher.cpp
      src/platform/ui/win32/win32_canvas.cpp
      src/platform/ui/win32/win32_window.cpp
    )
    add_library(kr16_ui STATIC ${KR16_UI_SOURCES})
    target_compile_definitions(kr16_ui PRIVATE
      UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(kr16_ui PRIVATE
      user32 gdi32 comdlg32 shell32
    )
  elseif(UNIX)
    list(APPEND KR16_UI_SOURCES
      src/ui/run_sdl.cpp
      src/platform/ui/sdl/sdl_canvas.cpp
      src/platform/ui/sdl/sdl_window.cpp
    )
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
    set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
    if(KR16_SDL3_SOURCE_DIR)
      add_subdirectory("${KR16_SDL3_SOURCE_DIR}"
                       "${CMAKE_BINARY_DIR}/sdl3")
    else()
      FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 535d80badefc83c5c527ec5748f2a20d6a9310fe
        GIT_SHALLOW FALSE
      )
      FetchContent_MakeAvailable(SDL3)
    endif()
    # KR16 builds with -Werror; keep third-party SDL warnings non-fatal.
    foreach(sdl_target SDL_uclibc SDL3-static SDL3_test)
      if(TARGET ${sdl_target})
        target_compile_options(${sdl_target} PRIVATE -Wno-error)
      endif()
    endforeach()
    add_library(kr16_ui STATIC ${KR16_UI_SOURCES})
    target_include_directories(kr16_ui PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
    )
    target_compile_definitions(kr16_ui PRIVATE SDL_MAIN_HANDLED)
    if(TARGET SDL3::SDL3-static)
      target_link_libraries(kr16_ui PRIVATE SDL3::SDL3-static)
    else()
      target_link_libraries(kr16_ui PRIVATE SDL3::SDL3)
    endif()
    target_link_libraries(kr16_ui PRIVATE dl pthread m)
  else()
    message(STATUS "KR16 UI: unsupported platform; skipping the UI")
    add_library(kr16_ui INTERFACE)
  endif()

  target_include_directories(kr16_ui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_include_directories(kr16_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  target_link_libraries(kr16_ui PUBLIC kr16_core)

  add_executable(kr16_ui_smoke src/ui/main.cpp)
  target_include_directories(kr16_ui_smoke PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  if(WIN32)
    target_compile_definitions(kr16_ui_smoke PRIVATE
      UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    )
  endif()
  target_link_libraries(kr16_ui_smoke PRIVATE kr16_ui)

  target_include_directories(kr16 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_compile_definitions(kr16 PRIVATE KR16_HAS_UI=1)
  target_link_libraries(kr16 PRIVATE kr16_ui)
endif()

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




