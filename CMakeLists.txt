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

# Which producer builds a program in `build_program`: the built-in compiler in
# `src/cc`, or KRCompiler's facade through `krc::toolchain`.
#
# The option is `KRG_TOOLCHAIN` like every other tree; `KR16_TOOLCHAIN` remains
# as this repository's legacy spelling. The value set is the union of what the
# family supports -- krc, builtin, bundled -- and this tree accepts the two that
# exist here, naming the third in the error rather than pretending it is a typo.
set(KR16_TOOLCHAIN "" CACHE STRING "Program build path (legacy alias for KRG_TOOLCHAIN)")
if(NOT KRG_TOOLCHAIN AND KR16_TOOLCHAIN)
  set(KRG_TOOLCHAIN "${KR16_TOOLCHAIN}")
endif()
if(NOT KRG_TOOLCHAIN)
  # `builtin` stays the default here until stage B moves the tree onto krc; the
  # family default is krc, so a tree that has not made the move says so.
  set(KRG_TOOLCHAIN "builtin")
endif()
set(KRG_TOOLCHAIN "${KRG_TOOLCHAIN}" CACHE STRING "Program build path: krc or builtin")
set_property(CACHE KRG_TOOLCHAIN PROPERTY STRINGS krc builtin)
if(NOT KRG_TOOLCHAIN STREQUAL "krc" AND NOT KRG_TOOLCHAIN STREQUAL "builtin")
  message(FATAL_ERROR
    "KRG_TOOLCHAIN must be one of krc, builtin (or bundled, which this tree does "
    "not have), not '${KRG_TOOLCHAIN}'")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KrGuest.cmake")

# `.kdbg` is the debug payload a krc-built `.kr16` carries instead of `.debug_*`.
# It needs `krc::debug`, so it follows the toolchain. Deliberately not a cached
# option: a build directory reconfigured for the other toolchain must follow it,
# and a cached value would keep whatever the first configure wrote. Pass
# `-DKR16_KDBG=ON|OFF` to override.
if(KRG_TOOLCHAIN STREQUAL "krc")
  set(KR16_KDBG_DEFAULT ON)
else()
  set(KR16_KDBG_DEFAULT OFF)
endif()
if(NOT DEFINED KR16_KDBG OR KR16_KDBG STREQUAL "")
  set(KR16_KDBG ${KR16_KDBG_DEFAULT})
endif()

# The KRCompiler revision this tree is pinned to. Recorded here because the
# consumer knows what it was built and gated against; the package itself does
# not export a revision.
set(KR16_KRC_PIN "d2be672" CACHE STRING
    "KRCompiler adopt-krc revision the krc toolchain package is pinned to")
# Install prefix of the pinned package, passed on to find_package.
set(KR16_KRC_PREFIX "" CACHE PATH
    "Install prefix of the pinned krc-toolchain package")
# Target sysroot of the pin. From 4.12 the package installs one under
# share/krc/sysroot; this variable is how the krc path finds it until the
# follow-up that reads KRC_SYSROOT instead.
set(KR16_KRC_SYSROOT "" CACHE PATH
    "KRCompiler target sysroot directory for krc-built programs")

if(KR16_KRC_PREFIX)
  list(PREPEND CMAKE_PREFIX_PATH "${KR16_KRC_PREFIX}")
endif()

# The package is required in both modes, for different things: the krc path uses its
# facade and archive, and the builtin path includes krc/listing.h so its `-l` is
# rendered by the same code as the compiler's. Only the facade is linked -- the
# renderer is inline, which is what keeps the builtin binary free of the package's
# dynamic CRT.
find_package(krc-toolchain CONFIG REQUIRED)
if(KR16_KDBG AND NOT TARGET krc::debug)
  find_package(krc-debug CONFIG REQUIRED)
endif()
message(STATUS "kr16 toolchain: ${KRG_TOOLCHAIN} (krc pin ${KR16_KRC_PIN}); "
               "kdbg reader: ${KR16_KDBG}")

if(MSVC)
  # The installed krc package is built against the dynamic CRT, so a binary that
  # links krc::toolchain must use it too: a static-CRT object meets an /MD archive
  # with LNK2038. The builtin toolchain keeps the static CRT, which is why the
  # shipped kr16.exe needs no redistributable; the choice follows the toolchain.
  if(KRG_TOOLCHAIN STREQUAL "krc")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
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
  src/vm/command_line.cpp
  src/vm/process.cpp
  src/vm/runner.cpp
  src/vm/host.cpp
  src/link/linker.cpp
  src/debug/debug_info.cpp
  src/debug/debug_session.cpp
  src/debug/kdbg_model.cpp
  src/cc/compiler.cpp
  src/resources/embedded.cpp
  src/resources/embedded_libc.cpp
  src/platform/socket.cpp
  src/platform/terminal.cpp
  src/dap/dap_json.cpp
  src/dap/dap_session.cpp
  src/dap/dap_server.cpp
)
include(GNUInstallDirs)
target_include_directories(kr16_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(kr16_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(kr16_core PUBLIC KR16_VERSION="${PROJECT_VERSION}")
# Headers in both modes: the builtin assembler renders its listing with the inline
# renderer from krc/listing.h, so a `-l` prints the same bytes from either toolchain
# path. Nothing from the package is linked in builtin mode, which is what keeps that
# binary free of the package's dynamic CRT.
get_target_property(KR16_KRC_INCLUDE_DIRS krc::toolchain INTERFACE_INCLUDE_DIRECTORIES)
if(NOT KR16_KRC_INCLUDE_DIRS)
  message(FATAL_ERROR "krc::toolchain exports no include directories")
endif()
target_include_directories(kr16_core PRIVATE ${KR16_KRC_INCLUDE_DIRS})
if(KRG_TOOLCHAIN STREQUAL "krc")
  target_compile_definitions(kr16_core PRIVATE KR16_TOOLCHAIN_KRC=1)
  target_link_libraries(kr16_core PRIVATE krc::toolchain)
endif()
if(KR16_KDBG)
  target_compile_definitions(kr16_core PRIVATE KR16_KDBG=1)
  target_link_libraries(kr16_core PRIVATE krc::debug)
endif()
# The krc build path compiles resources/fileio.c through the in-process facade,
# and the facade searches no sysroot unless it is told one: without this, a krc
# build fails with "header not found: 'stdarg.h'" even though the installed
# sysroot carries it. Deriving it from the package config means a caller does not
# have to name a path the package already knows.
if(KRG_TOOLCHAIN STREQUAL "krc" AND NOT KR16_KRC_SYSROOT AND krc-toolchain_DIR)
  get_filename_component(_krc_prefix "${krc-toolchain_DIR}/../../.." ABSOLUTE)
  if(EXISTS "${_krc_prefix}/share/krc/sysroot")
    set(KR16_KRC_SYSROOT "${_krc_prefix}/share/krc/sysroot")
    message(STATUS "kr16 krc sysroot: ${KR16_KRC_SYSROOT}")
  endif()
endif()
if(KR16_KRC_SYSROOT)
  target_compile_definitions(kr16_core PRIVATE
    KR16_KRC_SYSROOT_DIR="${KR16_KRC_SYSROOT}")
endif()
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
    src/ui/text_field.cpp
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
    # Exporting the core lets another tree link this machine instead of compiling its
# sources (D4 of the build cleanup plan); the other three cores export the same
# way. The include directory above is split so the export is legal.
add_library(kr16::core ALIAS kr16_core)
set_target_properties(kr16_core PROPERTIES EXPORT_NAME core)
target_compile_features(kr16_core PUBLIC cxx_std_20)
option(KR16_INSTALL "Install kr16::core and its headers for other trees" OFF)
if(KR16_INSTALL)
    install(TARGETS kr16_core EXPORT kr16Targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT kr16Targets
        FILE kr16Targets.cmake
        NAMESPACE kr16::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr16-core")
    include(CMakePackageConfigHelpers)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake.in"
        "@PACKAGE_INIT@\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/kr16Targets.cmake\")\n")
    configure_package_config_file(
        "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr16-core")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr16-core")
endif()
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
    # Exporting the core lets another tree link this machine instead of compiling its
# sources (D4 of the build cleanup plan); the other three cores export the same
# way. The include directory above is split so the export is legal.
add_library(kr16::core ALIAS kr16_core)
set_target_properties(kr16_core PROPERTIES EXPORT_NAME core)
target_compile_features(kr16_core PUBLIC cxx_std_20)
option(KR16_INSTALL "Install kr16::core and its headers for other trees" OFF)
if(KR16_INSTALL)
    install(TARGETS kr16_core EXPORT kr16Targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(EXPORT kr16Targets
        FILE kr16Targets.cmake
        NAMESPACE kr16::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr16-core")
    include(CMakePackageConfigHelpers)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake.in"
        "@PACKAGE_INIT@\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/kr16Targets.cmake\")\n")
    configure_package_config_file(
        "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr16-core")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/kr16-core-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/kr16-core")
endif()
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




