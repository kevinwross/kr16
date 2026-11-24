# Generates src/resources/embedded_libc.cpp from the resources it embeds.
#
#   cmake -DROOT=<kr16 root> -P resources/embed_libc.cmake
#
# This replaces resources/embed_libc.py (P3/B4). The build already requires CMake
# on every host, and it did not require Python on every host, so the generator
# uses the tool that is guaranteed to be there. The output is byte-for-byte what
# the script produced: same header line, same 16 bytes per line, upper-case hex,
# LF endings, one trailing newline.
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "pass -DROOT=<kr16 root>")
endif()

if(NOT DEFINED OUTPUT)
    set(OUTPUT "${ROOT}/src/resources/embedded_libc.cpp")
endif()
set(output "${OUTPUT}")
if(NOT DEFINED HEADER)
    set(HEADER "${ROOT}/src/resources/embedded_libc.hpp")
endif()
set(header "${HEADER}")

file(WRITE "${header}" [=[#pragma once

#include <cstdint>
#include <span>

namespace kr16 {

std::span<const std::uint8_t> embedded_libc_source();
std::span<const std::uint8_t> embedded_fileio_source();
std::span<const std::uint8_t> embedded_systemservices_header();

}  // namespace kr16
]=])

set(array_names kLibc kFileio kSystemServices)
set(function_names embedded_libc_source embedded_fileio_source embedded_systemservices_header)
set(source_paths
    "${ROOT}/resources/libc.s"
    "${ROOT}/resources/fileio.c"
    "${ROOT}/include/kr16/systemservices.h")
list(LENGTH array_names source_count)
math(EXPR last_index "${source_count} - 1")

set(lines "// Generated from resources/libc.s, fileio.c and systemservices.h; do not edit by hand.")
string(APPEND lines "\n#include \"resources/embedded_libc.hpp\"\n")
string(APPEND lines "\n#include <array>\n")
string(APPEND lines "\nnamespace kr16 {\nnamespace {\n")

foreach(index RANGE ${last_index})
    list(GET array_names ${index} array_name)
    list(GET source_paths ${index} source)
    # Normalise line endings before embedding: the repository pins LF, and a
    # checkout made with core.autocrlf=true hands this generator CRLF bytes.
    file(READ "${source}" hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR byte_count "${hex_length} / 2")
    string(APPEND lines "constexpr std::array<std::uint8_t, ${byte_count}> ${array_name} = {\n")
    set(offset 0)
    set(column 0)
    set(line "")
    while(offset LESS hex_length)
        string(SUBSTRING "${hex}" ${offset} 2 pair)
        string(TOUPPER "${pair}" pair)
        if(line STREQUAL "")
            set(line "    ")
        else()
            string(APPEND line ", ")
        endif()
        string(APPEND line "0x${pair}")
        math(EXPR offset "${offset} + 2")
        math(EXPR column "${column} + 1")
        if(column EQUAL 16)
            string(APPEND lines "${line},\n")
            set(line "")
            set(column 0)
        endif()
    endwhile()
    if(NOT line STREQUAL "")
        string(APPEND lines "${line},\n")
    endif()
    string(APPEND lines "};\n")
endforeach()

string(APPEND lines "}  // namespace\n\n")
foreach(index RANGE ${last_index})
    list(GET array_names ${index} array_name)
    list(GET function_names ${index} function_name)
    string(APPEND lines "std::span<const std::uint8_t> ${function_name}() {\n")
    string(APPEND lines "    return std::span<const std::uint8_t>(${array_name});\n")
    string(APPEND lines "}\n\n")
endforeach()
string(APPEND lines "}\n")

if(DEFINED CHECK)
    # Drift check: the committed file must be exactly what this generator
    # produces, so a hand edit or a resource change cannot go unnoticed.
    if(NOT EXISTS "${CHECK}")
        message(FATAL_ERROR "no committed file at ${CHECK}")
    endif()
    file(READ "${CHECK}" committed)
    if(NOT committed STREQUAL lines)
        message(FATAL_ERROR "${CHECK} does not match a regeneration; run cmake -DROOT=${ROOT} -P resources/embed_libc.cmake")
    endif()
    message(STATUS "${CHECK} matches a regeneration")
else()
    file(WRITE "${output}" "${lines}")
    message(STATUS "wrote ${output}")
endif()
