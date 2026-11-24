#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "kr16/build.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "kr16/cli.hpp"
#include "platform/terminal.hpp"
#include "resources/embedded_libc.hpp"

#if defined(KR16_TOOLCHAIN_KRC)
#include "krc/listing.h"
#include "krc/toolchain.h"
#endif

namespace kr16 {
namespace {

namespace fs = std::filesystem;

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return text;
}

std::string extension_of(const fs::path& path) {
    return lowercase(path.extension().string());
}

int run_command(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    std::string program = "kr16";
    argv.push_back(program.data());
    for (const auto& argument : args) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    return cli::run(static_cast<int>(argv.size()), argv.data());
}

bool run_step(const std::string& command, std::ostream& error,
              const std::vector<std::string>& args) {
    if (run_command(args) == 0) {
        return true;
    }
    error << command << ": " << args.front() << " failed\n";
    return false;
}

std::optional<fs::path> search_libc_source() {
    std::vector<fs::path> roots;
    if (const char* resource_dir = std::getenv("KR16_RESOURCE_DIR")) {
        roots.emplace_back(resource_dir);
    }
    std::error_code ec;
    roots.push_back(fs::current_path(ec));
    const std::string executable = platform::executable_path();
    if (!executable.empty()) {
        auto directory = fs::path(executable).parent_path();
        for (int level = 0; level < 4; ++level) {
            roots.push_back(directory);
            directory = directory.parent_path();
        }
    }
    for (const auto& root : roots) {
        if (root.empty()) {
            continue;
        }
        for (const auto& candidate :
             {root / "resources" / "libc.s", root / "libc.s"}) {
            if (fs::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::string> include_flags(
    const std::vector<fs::path>& include_paths) {
    std::vector<std::string> flags;
    for (const auto& path : include_paths) {
        flags.push_back("-I");
        flags.push_back(path.string());
    }
    return flags;
}

}  // namespace

fs::path find_libc_source() {
    static fs::path cached;
    static bool searched = false;
    if (searched) {
        return cached;
    }
    searched = true;

    if (const auto existing = search_libc_source()) {
        cached = *existing;
        return cached;
    }

    const auto bytes = embedded_libc_source();
    const auto fileio_bytes = embedded_fileio_source();
    const auto header_bytes = embedded_systemservices_header();
    if (bytes.empty()) {
        return {};
    }
    std::error_code ec;
    fs::path directory = fs::temp_directory_path(ec);
    if (ec) {
        return {};
    }
    directory /= "kr16";
    fs::create_directories(directory, ec);
    if (ec) {
        return {};
    }
    fs::path path = directory / "libc.s";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return {};
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return {};
    }
    if (!fileio_bytes.empty()) {
        fs::path fileio_path = directory / "fileio.c";
        std::ofstream fileio_output(fileio_path,
                                   std::ios::binary | std::ios::trunc);
        if (!fileio_output) {
            return {};
        }
        fileio_output.write(
            reinterpret_cast<const char*>(fileio_bytes.data()),
            static_cast<std::streamsize>(fileio_bytes.size()));
        if (!fileio_output) {
            return {};
        }
    }
    if (!header_bytes.empty()) {
        fs::path include_dir = directory / "include" / "kr16";
        fs::create_directories(include_dir, ec);
        if (ec) {
            return {};
        }
        std::ofstream header_output(include_dir / "systemservices.h",
                                    std::ios::binary | std::ios::trunc);
        if (!header_output) {
            return {};
        }
        header_output.write(
            reinterpret_cast<const char*>(header_bytes.data()),
            static_cast<std::streamsize>(header_bytes.size()));
        if (!header_output) {
            return {};
        }
    }
    cached = path;
    return cached;
}

#if defined(KR16_TOOLCHAIN_KRC)
namespace {

// The entry the built-in compiler emits into `.crt0`. krc's assembler has no
// `.crt0` directive, so the same three instructions go into `.text` and reach
// `main` through a register, the way krc's own startup object does.
constexpr std::string_view kKrcEntryStub =
    ".text\n.extern main\n.global _start\n_start:\n  LEA R0, [main]\n"
    "  CALL R0\n  HLT\n";

bool read_file_text(const fs::path& path, std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    return true;
}

bool write_file_text(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}

bool write_file_bytes(const fs::path& path,
                      const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

void write_diagnostics(
    const std::vector<krc::toolchain::Diagnostic>& diagnostics,
    std::ostream& error) {
    if (!diagnostics.empty()) {
        error << krc::toolchain::format_diagnostics(diagnostics);
    }
}

/// Formats kr16's `.lst` through the shared renderer, so the host that builds a
/// program and the compiler that lists one produce the same bytes for the same
/// statements. The rows carry no labels, so the link symbols whose value falls on a
/// row boundary become the symbol appendix the renderer prints.
std::string format_listing(const std::vector<krc::toolchain::ListingRow>& rows,
                           const krc::toolchain::LinkResult& linked,
                           const std::string& source_name) {
    std::vector<std::uint64_t> row_starts;
    row_starts.reserve(rows.size());
    const auto base_of = [&](const std::string& name) {
        for (const auto& section : linked.sections) {
            if (section.name == name) {
                return section.address;
            }
        }
        return std::uint64_t{0};
    };
    for (const auto& row : rows) {
        row_starts.push_back(base_of(row.section) + row.address);
    }
    std::sort(row_starts.begin(), row_starts.end());
    const auto starts_a_row = [&](std::uint64_t address) {
        return std::binary_search(row_starts.begin(), row_starts.end(), address);
    };

    std::vector<krc::toolchain::SymbolInfo> symbols;
    for (const auto& symbol : linked.symbols) {
        // A function's label belongs exactly where its first statement starts; a
        // symbol anywhere else (libc included) is not part of this listing.
        if (!symbol.global || symbol.name.empty() || symbol.name.front() == '.' ||
            !starts_a_row(symbol.value)) {
            continue;
        }
        symbols.push_back(symbol);
    }

    krc::listing::Document document;
    document.title = source_name;
    document.target = "kr16";
    document.linked = true;
    document.image_base = linked.base;
    document.word_bytes = 2;
    return krc::listing::render(rows, symbols, linked.sections, document);
}
/// The krc branch of `build_program`: compile, assemble and link one `.c` input
/// through `krc::toolchain`, using the shipped libc for the runtime.
BuildResult build_c_with_krc(const BuildOptions& options,
                             const fs::path& executable,
                             const fs::path& assembly_path,
                             const fs::path& object_path,
                             const fs::path& libc_object_path,
                             std::ostream& error) {
    BuildResult result;
    result.executable = executable;
    result.listing = options.listing;
    result.assembly = assembly_path;
    result.object = object_path;

    krc::toolchain::Options settings;
    settings.target = krc::toolchain::Target::Kr16;
    settings.debug = options.debug;
    settings.optimize = options.optimize ? 1 : 0;
    settings.entry = "_start";
    // `resources/libc.s` puts each function in its own `.text.<name>` section,
    // so collecting the unreferenced ones costs nothing the image still needs.
    // The entry section is the walk's root and relocations carry it onward.
    settings.gc_sections = true;
#if defined(KR16_KRC_SYSROOT_DIR)
    settings.include_paths.push_back(std::string(KR16_KRC_SYSROOT_DIR) +
                                     "/include");
    settings.include_paths.push_back(std::string(KR16_KRC_SYSROOT_DIR) +
                                     "/kr16/include");
#endif
    for (const auto& path : options.include_paths) {
        settings.include_paths.push_back(path.string());
    }

    // The runtime is the shipped libc; its `include/` carries the host-service
    // header a program may name.
    const fs::path libc_source =
        options.libc_source.empty() ? find_libc_source() : options.libc_source;
    if (libc_source.empty()) {
        error << options.command
              << ": cannot find resources/libc.s; set KR16_RESOURCE_DIR or "
                 "pass a libc source\n";
        return result;
    }
    const fs::path runtime_include = libc_source.parent_path().parent_path() / "include";
    if (fs::exists(runtime_include)) {
        settings.include_paths.push_back(runtime_include.string());
    }

    std::string source_text;
    if (!read_file_text(options.input, source_text)) {
        error << options.command << ": cannot read " << options.input.string()
              << '\n';
        return result;
    }
    auto compiled = krc::toolchain::compile(
        {options.input.string(), source_text}, settings);
    write_diagnostics(compiled.diagnostics, error);
    if (!compiled.ok) {
        error << options.command << ": cc failed\n";
        return result;
    }
    if (!compiled.assembly.empty() &&
        !write_file_text(assembly_path, compiled.assembly)) {
        error << options.command << ": cannot write " << assembly_path.string()
              << '\n';
        return result;
    }
    auto object = krc::toolchain::assemble(compiled.assembly,
                                           compiled.debug_info, settings);
    write_diagnostics(object.diagnostics, error);
    if (!object.ok) {
        error << options.command << ": as failed: " << object.error << '\n';
        return result;
    }
    if (!write_file_bytes(object_path, object.object)) {
        error << options.command << ": cannot write " << object_path.string()
              << '\n';
        return result;
    }

    auto entry = krc::toolchain::assemble(kKrcEntryStub, settings);
    if (!entry.ok) {
        error << options.command << ": entry stub failed: " << entry.error
              << '\n';
        return result;
    }

    std::string libc_text;
    if (!read_file_text(libc_source, libc_text)) {
        error << options.command << ": cannot read " << libc_source.string()
              << '\n';
        return result;
    }
    auto libc = krc::toolchain::assemble(libc_text, settings);
    write_diagnostics(libc.diagnostics, error);
    if (!libc.ok) {
        error << options.command << ": libc failed: " << libc.error << '\n';
        return result;
    }
    result.libc_object = libc_object_path;
    if (!write_file_bytes(libc_object_path, libc.object)) {
        error << options.command << ": cannot write "
              << libc_object_path.string() << '\n';
        return result;
    }

    std::optional<krc::toolchain::Object> fileio_object;
    const fs::path fileio_source = libc_source.parent_path() / "fileio.c";
    if (fs::exists(fileio_source)) {
        std::string fileio_text;
        if (!read_file_text(fileio_source, fileio_text)) {
            error << options.command << ": cannot read "
                  << fileio_source.string() << '\n';
            return result;
        }
        auto fileio = krc::toolchain::compile(
            {fileio_source.string(), fileio_text}, settings);
        write_diagnostics(fileio.diagnostics, error);
        if (!fileio.ok) {
            error << options.command << ": fileio failed\n";
            return result;
        }
        auto assembled = krc::toolchain::assemble(fileio.assembly,
                                                  fileio.debug_info, settings);
        write_diagnostics(assembled.diagnostics, error);
        if (!assembled.ok) {
            error << options.command << ": fileio as failed: "
                  << assembled.error << '\n';
            return result;
        }
        krc::toolchain::Object object_bytes;
        object_bytes.name = fileio_source.string();
        object_bytes.bytes = std::move(assembled.object);
        fileio_object = std::move(object_bytes);
    }

    // The input goes first so its section offsets are the section's own, which
    // lets the listing place labels by address. The entry symbol sets the entry
    // point, so the stub does not have to be at the start of `.text`.
    std::vector<krc::toolchain::Object> objects;
    objects.push_back(krc::toolchain::Object{
        options.input.string(), std::move(object.object)});
    objects.push_back(
        krc::toolchain::Object{"crt0.s", std::move(entry.object)});
    objects.push_back(
        krc::toolchain::Object{libc_source.string(), std::move(libc.object)});
    if (fileio_object.has_value()) {
        objects.push_back(std::move(*fileio_object));
    }
    auto linked = krc::toolchain::link(objects, settings);
    write_diagnostics(linked.diagnostics, error);
    if (!linked.ok) {
        error << options.command << ": ld failed: " << linked.error << '\n';
        return result;
    }
    if (!write_file_bytes(executable, linked.image)) {
        error << options.command << ": cannot write " << executable.string()
              << '\n';
        return result;
    }

    if (!options.listing.empty()) {
        if (!write_file_text(options.listing,
                             format_listing(object.listing, linked,
                                            options.input.filename().string()))) {
            error << options.command << ": cannot write "
                  << options.listing.string() << '\n';
            return result;
        }
    }

    result.ok = true;
    return result;
}

}  // namespace
#endif  // KR16_TOOLCHAIN_KRC

bool is_build_input(const fs::path& path) {
    const std::string extension = extension_of(path);
    return extension == ".c" || extension == ".s" || extension == ".asm" ||
           extension == ".o";
}

BuildResult build_program(const BuildOptions& options, std::ostream& error) {
    BuildResult result;
    if (options.input.empty()) {
        error << options.command << ": missing input file\n";
        return result;
    }

    std::error_code ec;
    if (!fs::exists(options.input, ec)) {
        error << options.command << ": cannot find " << options.input.string()
              << '\n';
        return result;
    }

    const std::string extension = extension_of(options.input);
    if (!is_build_input(options.input)) {
        error << options.command << ": unsupported file type '" << extension
              << "' (expected .c, .s, .asm, or .o)\n";
        return result;
    }

    const bool with_libc = options.link_libc;

    result.executable = options.output.empty() ? options.input : options.output;
    if (options.output.empty()) {
        result.executable.replace_extension(".kr16");
    }

    // Artifacts stay beside the input, mirroring a hand-run toolchain:
    // name.c -> name.s, name.o, name.libc.o, name.lst, name.kr16.
    const fs::path directory = options.input.parent_path();
    const std::string stem = options.input.stem().string();
    const fs::path assembly = directory / (stem + ".s");
    const fs::path object = directory / (stem + ".o");
    const fs::path libc_object = directory / (stem + ".libc.o");
    std::vector<std::string> includes = include_flags(options.include_paths);
    if (with_libc) {
        const fs::path runtime_libc = find_libc_source();
        if (!runtime_libc.empty()) {
            const fs::path runtime_include =
                runtime_libc.parent_path().parent_path() / "include";
            if (fs::exists(runtime_include, ec)) {
                includes.push_back("-I");
                includes.push_back(runtime_include.string());
            }
        }
    }

#if defined(KR16_TOOLCHAIN_KRC)
    // C input is produced by KRCompiler's facade. Assembly and object input
    // stay on the built-in stages for stage A: a krc-built image mixes their
    // object format and addresses with the built-in linker's.
    if (extension == ".c") {
        return build_c_with_krc(options, result.executable, assembly, object,
                                libc_object, error);
    }
#endif

    bool ok = true;
    if (extension == ".o") {
        if (!options.listing.empty()) {
            error << options.command << ": ignoring -l, "
                  << options.input.string() << " has no assembly source\n";
        }
        result.object = options.input;
    } else {
        result.listing = options.listing;
        result.object = object;
        if (extension == ".c") {
            result.assembly = assembly;
            std::vector<std::string> compile{
                "cc", options.input.string(), "-S", "-o", assembly.string()};
            if (options.debug) {
                compile.push_back("-g");
            }
            compile.push_back(options.optimize ? "-O1" : "-O0");
            compile.insert(compile.end(), includes.begin(), includes.end());
            ok = run_step(options.command, error, compile);
        }
        if (ok) {
            std::vector<std::string> assemble{
                "as", (extension == ".c" ? assembly : options.input).string(),
                "-o", object.string()};
            if (options.debug) {
                assemble.push_back("-g");
            }
            if (!result.listing.empty()) {
                assemble.push_back("-l");
                assemble.push_back(result.listing.string());
            }
            assemble.insert(assemble.end(), includes.begin(), includes.end());
            ok = run_step(options.command, error, assemble);
        }
    }

    fs::path fileio_object;
    if (ok && with_libc) {
        fs::path libc;
        if (!options.libc_source.empty()) {
            libc = options.libc_source;
        } else {
            libc = find_libc_source();
        }
        if (libc.empty()) {
            error << options.command
                  << ": cannot find resources/libc.s; set KR16_RESOURCE_DIR or "
                     "pass a libc source\n";
            ok = false;
        } else {
            result.libc_object = libc_object;
            ok = run_step(options.command, error,
                          {"as", libc.string(), "-o", libc_object.string()});
        }
        if (ok) {
            const fs::path fileio_source = libc.parent_path() / "fileio.c";
            if (fs::exists(fileio_source)) {
                fileio_object = directory / (stem + ".fileio.o");
                std::vector<std::string> fileio_compile{
                    "cc", fileio_source.string(), "-c", "-o",
                    fileio_object.string(), "-O1"};
                const fs::path include_dir =
                    libc.parent_path().parent_path() / "include";
                if (fs::exists(include_dir)) {
                    fileio_compile.push_back("-I");
                    fileio_compile.push_back(include_dir.string());
                }
                ok = run_step(options.command, error, fileio_compile);
            }
        }
    }

    if (ok) {
        std::vector<std::string> link{"ld"};
        if (extension == ".c") {
            // libc first: the compiler emits .crt0, which sets the entry point.
            link.push_back(libc_object.string());
            if (!fileio_object.empty()) {
                link.push_back(fileio_object.string());
            }
            link.push_back(object.string());
        } else {
            link.push_back(object.string());
            if (with_libc) {
                link.push_back(libc_object.string());
                if (!fileio_object.empty()) {
                    link.push_back(fileio_object.string());
                }
            }
        }
        link.push_back("-o");
        link.push_back(result.executable.string());
        ok = run_step(options.command, error, link);
        if (!ok && !with_libc) {
            error << options.command
                  << ": if the program calls the runtime, add --libc\n";
        }
    }

    result.ok = ok;
    return result;
}

}  // namespace kr16
