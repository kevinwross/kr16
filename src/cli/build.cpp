#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "kr16/build.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

#include "kr16/cli.hpp"
#include "platform/terminal.hpp"
#include "resources/embedded_libc.hpp"

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
        result.executable.replace_extension(".kex");
    }

    // Artifacts stay beside the input, mirroring a hand-run toolchain:
    // name.c -> name.s, name.o, name.libc.o, name.lst, name.kex.
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
