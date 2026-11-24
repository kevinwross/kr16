#pragma once

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace kr16 {

/// Options for the one-step compile, assemble, and link pipeline that backs
/// `kr16 build` and the interactive shell's `load`.
///
/// Artifacts land next to the input, like a hand-run `cc`/`as`/`ld` sequence:
/// `name.c` produces `name.s`, `name.o`, and `name.libc.o`; `-l` adds
/// `name.lst`; the image is `name.kr16` unless `output` says otherwise.
struct BuildOptions {
    std::filesystem::path input;    ///< .c, .s, .asm, or .o
    std::filesystem::path output;   ///< image; empty means "<input>.kr16"
    std::filesystem::path listing;  ///< listing; empty means "do not write one"
    std::filesystem::path libc_source;  ///< explicit libc.s; empty means search
    std::vector<std::filesystem::path> include_paths;
    bool debug = true;              ///< record .debug_* sections
    bool optimize = true;           ///< compile C with -O1 when true
    bool link_libc = false;         ///< link libc alongside the input
    std::string command = "build";  ///< prefix used in diagnostics
};

struct BuildResult {
    bool ok = false;
    std::filesystem::path executable;
    std::filesystem::path listing;       ///< empty when none was requested
    std::filesystem::path assembly;      ///< empty unless a .s took part
    std::filesystem::path object;        ///< object that was linked
    std::filesystem::path libc_object;   ///< empty unless libc was linked
};

/// Compiles, assembles, and links `options.input` into `options.output`.
/// C input also links `resources/libc.s` (or `options.libc_source` when set).
/// Diagnostics are written to `error`; the individual sub-commands print their
/// own compiler/assembler errors.
BuildResult build_program(const BuildOptions& options, std::ostream& error);

/// True when `path` has an extension the pipeline understands.
bool is_build_input(const std::filesystem::path& path);

/// Finds `libc.s` next to the executable or under `KR16_RESOURCE_DIR`.
/// When no file is present, writes the embedded copy to the temp directory and
/// returns that path. Returns an empty path if extraction fails.
std::filesystem::path find_libc_source();

}  // namespace kr16
