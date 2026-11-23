#include "kr16/cli.hpp"
#include "kr16/debug_instruments.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "kr16/assembler.hpp"
#include "kr16/build.hpp"
#include "kr16/compiler.hpp"
#include "kr16/container.hpp"
#include "kr16/dap.hpp"
#include "kr16/debug_info.hpp"
#include "kr16/diagnostic.hpp"
#include "kr16/disassembler.hpp"
#include "kr16/linker.hpp"
#include "kr16/shell.hpp"
#include "platform/terminal.hpp"
#include "kr16/version.hpp"
#include "kr16/vm.hpp"

namespace kr16::cli {
namespace {

constexpr std::array<std::string_view, 12> kCommands = {
    "as", "dis", "cc", "build", "ld", "size",
    "debug", "vm", "dap", "version", "help", "shell",
};

std::string command_help(std::string_view command) {
    if (command == "as") {
        return R"(usage: kr16 as <file.s> [options]

Assemble one source file into a relocatable object, or a raw binary.

Arguments:
  <file.s>             assembly source to assemble

Options:
  -o <path>            output path (default: a.o, or a.bin with --raw)
  -l <path>            write a listing file
  -I <dir>             add an include search directory (repeatable)
  -g                   include debug information
  --raw                write a flat binary instead of a relocatable object
  --base <addr>        raw load address (default: 0x0100)
  -h, --help           show this help

Examples:
  kr16 as hello.s -o hello.o
  kr16 as boot.s --raw --base 0x0100 -o boot.bin -l boot.lst

)";
    }
    if (command == "dis") {
        return R"(usage: kr16 dis <file.o|file.kex> [options]

Disassemble a KR16 object or executable image.

Arguments:
  <file.o|file.kex>    image to disassemble

Options:
  --raw                treat the input as a flat binary
  --base <addr>        base address for --raw (default: 0)
  --no-hex             omit the raw instruction bytes
  --source             accepted for compatibility; source interleaving is
                       not implemented in v1
  --symbols            accepted for compatibility; container symbols are
                       already used for labels
  -h, --help           show this help

)";
    }
    if (command == "cc") {
        return R"(usage: kr16 cc <file.c> [options]

Compile C to assembly, then assemble it to an object.

Arguments:
  <file.c>             C source file

Options:
  -S                   write assembly (.s); do not assemble
  -c                   write an object (.o); this is the default
  -o <path>            output path (default: input with .s or .o)
  -g                   include debug information
  -I <dir>             add an include search directory
  -I<dir>              attached form of -I
  -D <name[=value]>    accepted for compatibility; macro is not applied
  -U <name>            accepted for compatibility; macro is not undefined
  -O0                   disable constant folding and simple-expression codegen
  -O1                   enable optimization (default)
  -std=c99             accepted; the compiler is C99-like
  -std=c11             accepted; the compiler is C99-like
  -E                   unavailable in this subset compiler
  -h, --help           show this help

-S and -c are mutually exclusive.

)";
    }
    if (command == "build") {
        return R"(usage: kr16 build <file.c|file.s|file.asm|file.o> [options]

Compile, assemble and link one input into a KR16 image.

Arguments:
  <file.c>             compile with cc, assemble, and link libc
  <file.s|file.asm>    assemble, then link
  <file.o>             link

Options:
  -o <path>            image path (default: <input>.kex)
  -l, -L               write a listing beside the source
  -l=<path>            write the listing to the given path
  --listing=<path>     write the listing to the given path
  -I <dir>             add an include path for cc/as
  -I<dir>              attached form of -I
  -g                   record debug information (default)
  --no-debug           omit debug sections
  -O0                   disable C optimization
  -O1                   enable C optimization (default)
  --libc               link resources/libc.s for .s/.asm/.o input
  --run                run the image after a successful build
  -h, --help           show this help

)";
    }
    if (command == "ld") {
        return R"(usage: kr16 ld <object.o>... [options]

Link relocatable objects into a KR16 image.

Arguments:
  <object.o>...        input objects; at least one is required

Options:
  -o <path>            output image (default: a.kex)
  --map <path>         write a map of sections and symbols
  --strip-debug        remove debug sections
  --strip              remove debug sections and all but required symbols
  -r                   produce a relocatable object
  --gc-all             drop input objects unreachable from _start/start
  -h, --help           show this help

)";
    }
    if (command == "size") {
        return R"(usage: kr16 size <file.kex|file.o>

Report the allocatable .text, .data and .bss sizes of an image or object.

Arguments:
  <file>               container to inspect

Options:
  -h, --help           show this help

)";
    }
    if (command == "debug") {
        return R"(usage: kr16 debug <file.o|file.kex> [filters]

Dump decoded debug information.

Arguments:
  <file>               image or object to inspect

Filters (default: all):
  --lines              source line table
  --types              type records
  --vars               variable records
  --frames             frame records
  --objects            object records
  --json               unavailable in v1
  -h, --help           show this help

)";
    }
    if (command == "vm") {
        return R"(usage: kr16 vm <program.kex|program.bin> [options] [-- args...]

Run a KR16 program on the virtual machine.

Arguments:
  <program>            image to run
  [args...]            non-option words after the program are guest args
  -- <args...>         stop option parsing; pass the rest to the guest

Options:
  --raw                treat the program as a flat binary
  --base <addr>        raw load address (default: 0x0100)
  --bios <path>        load a 3.75 KiB BIOS ROM
  --no-bios            start directly at the image entry point
  --max-cycles <n>     stop after n cycles (default: unlimited)
  --realtime           pace execution to the requested MHz
  --mhz <n>            real-time clock rate (default: 1)
  --input <path>       feed scripted keyboard input
  --headless           run without a display
  --line-input         read guest input line by line
  --trace              print an instruction trace
  --trace-syscalls     print host syscall calls
  --stats              print cycle and instruction totals
  --fs-root <path>     host filesystem sandbox root (default: ./sandbox)
  --fs-quota <n>       quota, recursive over fs-root (default: 5M)
                       Counts every file under --fs-root; a build tree can
                       exhaust it. Use a dedicated directory or raise this.
  --fs-readonly        deny host filesystem writes
  --no-host-fs         disable host filesystem services
  -i                   unavailable in v1
  -h, --help           show this help

)";
    }
    if (command == "dap") {
        return R"(usage: kr16 dap <program.kex> [options]

Run the VM with the Debug Adapter Protocol server listening on TCP.

Arguments:
  <program.kex>        image to debug

Options:
  --port <n>           TCP port (default: 22152)
  --raw                treat the program as a flat binary
  --base <addr>        raw load address (default: 0x0100)
  --bios <path>        load a 3.75 KiB BIOS ROM
  --no-bios            start directly at the entry point
  --ready-file <path>  write a readiness marker after binding
  --stop-on-entry      report a stopped/entry event (default)
  --no-stop-on-entry   run silently until a breakpoint
  --force, -f          accepted for shell compatibility; no adapter effect
  -h, --help           show this help

)";
    }
    if (command == "version") {
        return R"(usage: kr16 version

Print the tool, ISA, container, host-service ABI and debug format versions.

Options:
  -h, --help           show this help

)";
    }
    if (command == "shell") {
        return R"(usage: kr16 shell

Start the interactive KR16 command shell.

The shell reads commands from stdin. Piped input runs without prompts.
Use `exit` or `quit` to leave.

Options:
  -h, --help           show this help

)";
    }
    if (command == "help") {
        return R"(usage: kr16 help [command]

Print general help, or detailed help for one command.

Arguments:
  [command]            command name (as, dis, cc, build, ld, size, debug,
                       vm, dap, version, shell)

Options:
  -h, --help           show this help

Examples:
  kr16 help
  kr16 help as
  kr16 as --help

)";
    }
    return {};
}


std::string lowercase_copy(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return result;
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void write_bytes(const std::string& path,
                 std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output.good()) {
        throw std::runtime_error("cannot create " + path);
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output.good()) {
        throw std::runtime_error("cannot write " + path);
    }
}

void write_text(const std::string& path, const std::string& text) {
    write_bytes(path, std::span<const std::uint8_t>(
                           reinterpret_cast<const std::uint8_t*>(text.data()),
                           text.size()));
}

std::uint16_t parse_u16(const std::string& text) {
    const auto value = std::stoul(text, nullptr, 0);
    if (value > 0xFFFF) {
        throw std::runtime_error("value out of 16-bit range: " + text);
    }
    return static_cast<std::uint16_t>(value);
}

int run_as(int argc, char** argv) {
    std::string input;
    std::string output;
    std::string listing_path;
    AsmOptions options;
    bool raw = false;

    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-o") {
            if (++i >= argc) {
                std::cerr << "kr16 as: -o needs a path\n";
                return 2;
            }
            output = argv[i];
        } else if (argument == "-l") {
            if (++i >= argc) {
                std::cerr << "kr16 as: -l needs a path\n";
                return 2;
            }
            listing_path = argv[i];
        } else if (argument == "-I") {
            if (++i >= argc) {
                std::cerr << "kr16 as: -I needs a path\n";
                return 2;
            }
            options.include_paths.push_back(argv[i]);
        } else if (argument == "--raw") {
            raw = true;
        } else if (argument == "--base") {
            if (++i >= argc) {
                std::cerr << "kr16 as: --base needs an address\n";
                return 2;
            }
            try {
                options.base = parse_u16(argv[i]);
            } catch (const std::exception& error) {
                std::cerr << "kr16 as: " << error.what() << '\n';
                return 2;
            }
        } else if (argument == "-g") {
            options.debug = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << command_help("as");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 as: unknown option " << argument << '\n';
            return 2;
        } else if (input.empty()) {
            input = argument;
        } else {
            std::cerr << "kr16 as: multiple input files\n";
            return 2;
        }
    }

    if (input.empty()) {
        std::cerr << "kr16 as: missing input file\n";
        return 2;
    }
    options.raw = raw;
    const std::string default_output = raw ? "a.bin" : "a.o";
    if (output.empty()) {
        output = default_output;
    }

    const auto result = assemble_file(input, options);
    if (!result.ok) {
        for (const auto& diagnostic : result.diagnostics) {
            std::cerr << diagnostic.file << ":" << diagnostic.line << ":"
                      << diagnostic.column << ": " << diagnostic.message << '\n';
        }
        return 1;
    }

    try {
        if (raw) {
            write_bytes(output, result.raw);
        } else {
            const auto bytes = result.object.write();
            write_bytes(output, bytes);
        }
        if (!listing_path.empty()) {
            write_text(listing_path, result.listing);
        }
    } catch (const std::exception& error) {
        std::cerr << "kr16 as: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

int run_dis(int argc, char** argv) {
    std::string input;
    bool raw = false;
    bool show_bytes = true;
    DisassemblyOptions options;

    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--raw") {
            raw = true;
        } else if (argument == "--base") {
            if (++i >= argc) {
                std::cerr << "kr16 dis: --base needs an address\n";
                return 2;
            }
            try {
                options.base = parse_u16(argv[i]);
            } catch (const std::exception& error) {
                std::cerr << "kr16 dis: " << error.what() << '\n';
                return 2;
            }
        } else if (argument == "--no-hex") {
            show_bytes = false;
        } else if (argument == "--source" || argument == "--symbols") {
            // Recognized; source interleaving arrives with debug info in M7.
        } else if (argument == "--help" || argument == "-h") {
            std::cout << command_help("dis");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 dis: unknown option " << argument << '\n';
            return 2;
        } else if (input.empty()) {
            input = argument;
        } else {
            std::cerr << "kr16 dis: multiple input files\n";
            return 2;
        }
    }

    if (input.empty()) {
        std::cerr << "kr16 dis: missing input file\n";
        return 2;
    }
    options.show_bytes = show_bytes;

    try {
        const auto bytes = read_bytes(input);
        if (raw) {
            std::cout << disassemble_raw(bytes, options);
        } else {
            const auto container = Container::read(bytes);
            std::cout << disassemble_container(container, options);
        }
    } catch (const std::exception& error) {
        std::cerr << "kr16 dis: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

int run_vm_command(int argc, char** argv) {
    std::string program;
    VmOptions options;
    bool after_separator = false;

    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (after_separator) {
            options.program_args.push_back(argument);
        } else if (argument == "--") {
            after_separator = true;
        } else if (argument == "--raw") {
            options.raw = true;
        } else if (argument == "--base") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --base needs an address\n";
                return 2;
            }
            try {
                options.base = parse_u16(argv[i]);
            } catch (const std::exception& error) {
                std::cerr << "kr16 vm: " << error.what() << '\n';
                return 2;
            }
        } else if (argument == "--bios") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --bios needs a path\n";
                return 2;
            }
            options.bios_path = argv[i];
        } else if (argument == "--no-bios") {
            options.no_bios = true;
        } else if (argument == "--watch") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --watch needs an address\n";
                return 2;
            }
            g_debug.watch_address = static_cast<std::uint16_t>(std::strtoul(argv[i], nullptr, 0));
            g_debug.watch = true;
        } else if (argument == "--break") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --break needs an address\n";
                return 2;
            }
            g_debug.break_address = static_cast<std::uint16_t>(std::strtoul(argv[i], nullptr, 0));
            g_debug.breakpoint = true;
        } else if (argument == "--break-max") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --break-max needs a count\n";
                return 2;
            }
            g_debug.break_limit = static_cast<unsigned>(std::strtoul(argv[i], nullptr, 0));
        } else if (argument == "--watch-page") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --watch-page needs an address\n";
                return 2;
            }
            g_debug.page_base = static_cast<std::uint16_t>(
                std::strtoul(argv[i], nullptr, 0) & 0xF000u);
            g_debug.page = true;
        } else if (argument == "--watch-fp") {
            g_debug.frame_pointer = true;
        } else if (argument == "--max-cycles") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --max-cycles needs a number\n";
                return 2;
            }
            try {
                options.max_cycles = std::stoull(argv[i], nullptr, 0);
            } catch (const std::exception&) {
                std::cerr << "kr16 vm: bad --max-cycles value\n";
                return 2;
            }
        } else if (argument == "--trace") {
            options.trace = true;
        } else if (argument == "--trace-syscalls") {
            options.trace_syscalls = true;
        } else if (argument == "--stats") {
            options.stats = true;
        } else if (argument == "--realtime") {
            options.realtime = true;
        } else if (argument == "--mhz") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --mhz needs a number\n";
                return 2;
            }
            try {
                options.mhz = static_cast<unsigned>(std::stoul(argv[i], nullptr, 0));
            } catch (const std::exception&) {
                std::cerr << "kr16 vm: bad --mhz value\n";
                return 2;
            }
        } else if (argument == "--input") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: --input needs a path\n";
                return 2;
            }
            options.input_path = argv[i];
        } else if (argument == "--fs-root" || argument == "--fs-quota") {
            if (++i >= argc) {
                std::cerr << "kr16 vm: " << argument << " needs a value\n";
                return 2;
            }
            if (argument == "--fs-root") {
                options.fs_root = argv[i];
                options.fs_root_explicit = true;
            } else {
                std::string value = argv[i];
                unsigned long long multiplier = 1;
                if (!value.empty()) {
                    const char suffix = static_cast<char>(
                        std::toupper(static_cast<unsigned char>(value.back())));
                    if (suffix == 'K' || suffix == 'M' || suffix == 'G') {
                        value.pop_back();
                        multiplier = suffix == 'K' ? 1024ull
                                     : suffix == 'M' ? 1024ull * 1024ull
                                                     : 1024ull * 1024ull * 1024ull;
                    }
                }
                try {
                    const auto quota =
                        std::stoull(value, nullptr, 0) * multiplier;
                    if (quota > 0xFFFFFFFFull) {
                        throw std::out_of_range("quota");
                    }
                    options.fs_quota = static_cast<std::uint32_t>(quota);
                } catch (const std::exception&) {
                    std::cerr << "kr16 vm: bad --fs-quota value\n";
                    return 2;
                }
            }
        } else if (argument == "--fs-readonly") {
            options.fs_readonly = true;
        } else if (argument == "--no-host-fs") {
            options.no_host_fs = true;
        } else if (argument == "--headless" || argument == "--line-input") {
            options.headless = true;
        } else if (argument == "-i") {
            std::cerr << "kr16 vm: interactive monitor is not available yet\n";
            return 2;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << command_help("vm");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 vm: unknown option " << argument << '\n';
            return 2;
        } else if (program.empty()) {
            program = argument;
        } else {
            options.program_args.push_back(argument);
        }
    }

    if (program.empty()) {
        std::cerr << "kr16 vm: missing program\n";
        return 2;
    }

    const auto result = run_vm(program, options);
    if (!result.ok) {
        std::cerr << "kr16 vm: " << result.error << '\n';
        return 1;
    }
    if (!result.output.empty()) {
        std::cout.write(result.output.data(),
                        static_cast<std::streamsize>(result.output.size()));
        std::cout.flush();
    }
    if (options.trace) {
        std::cerr << result.trace;
    }
    if (options.trace_syscalls) {
        std::cerr << result.syscall_trace;
    }
    if (options.stats) {
        std::cerr << "cycles=" << result.cycles
                  << " instructions=" << result.instructions << '\n';
    }
    if (result.timed_out) {
        std::cerr << "kr16 vm: maximum cycle count reached\n";
    }
    if (!result.error.empty()) {
        std::cerr << "kr16 vm: " << result.error << '\n';
    }
    return result.exit_code;
}

int run_ld_command(int argc, char** argv) {
    std::vector<std::string> inputs;
    std::string output = "a.kex";
    std::string map_path;
    LinkOptions options;
    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-o") {
            if (++i >= argc) {
                std::cerr << "kr16 ld: -o needs a path\n";
                return 2;
            }
            output = argv[i];
        } else if (argument == "--map") {
            if (++i >= argc) {
                std::cerr << "kr16 ld: --map needs a path\n";
                return 2;
            }
            map_path = argv[i];
        } else if (argument == "--strip-debug") {
            options.strip_debug = true;
        } else if (argument == "--strip") {
            options.strip_debug = true;
            options.strip_symbols = true;
        } else if (argument == "-r") {
            options.relocatable = true;
        } else if (argument == "--gc-all") {
            options.gc_all = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << command_help("ld");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 ld: unknown option " << argument << '\n';
            return 2;
        } else {
            inputs.push_back(argument);
        }
    }
    if (inputs.empty()) {
        std::cerr << "kr16 ld: no input objects\n";
        return 2;
    }

    const auto linked = link_files(inputs, options);
    if (!linked.ok) {
        for (const auto& diagnostic : linked.diagnostics) {
            std::cerr << "kr16 ld: " << diagnostic << '\n';
        }
        return 1;
    }
    try {
        write_bytes(output, linked.image.write());
        if (!map_path.empty()) {
            std::ofstream map(map_path);
            if (!map.good()) {
                throw std::runtime_error("cannot create " + map_path);
            }
            map << "KR16 map\nentry=0x" << std::hex << linked.image.entry
                << std::dec << "\n";
            for (const auto& section : linked.image.sections) {
                map << "section " << section.name << " addr=0x" << std::hex
                    << section.address << std::dec
                    << " size=" << section.byte_size() << "\n";
            }
            for (const auto& symbol : linked.image.symbols) {
                map << "symbol " << symbol.name << " value=0x" << std::hex
                    << symbol.value << std::dec << "\n";
            }
            for (const auto& symbol : linked.gc_dropped_symbols) {
                map << "dropped-symbol " << symbol << "\n";
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "kr16 ld: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

int run_size_command(int argc, char** argv) {    std::string input;
    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << command_help("size");
            return 0;
        }
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 size: unknown option " << argument << '\n';
            return 2;
        }
        if (!input.empty()) {
            std::cerr << "kr16 size: multiple input files\n";
            return 2;
        }
        input = argument;
    }
    if (input.empty()) {
        std::cerr << "kr16 size: missing input file\n";
        return 2;
    }

    try {
        const auto container = Container::read(read_bytes(input));
        std::uint64_t text = 0;
        std::uint64_t data = 0;
        std::uint64_t bss = 0;
        for (const auto& section : container.sections) {
            if ((section.flags & SectionAlloc) == 0) {
                continue;
            }
            if (section.type == SectionType::Nobits) {
                bss += section.nobits_size;
            } else if ((section.flags & SectionExec) != 0) {
                text += section.data.size();
            } else {
                data += section.data.size();
            }
        }
        const auto total = text + data + bss;
        std::cout << "   text    data     bss     dec     hex  file\n";
        std::cout << std::setw(7) << text << " " << std::setw(7) << data
                  << " " << std::setw(7) << bss << " " << std::setw(7)
                  << total << " " << std::setw(7) << std::hex << total
                  << std::dec << "  " << input << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kr16 size: " << error.what() << '\n';
        return 1;
    }
}

int run_debug_command(int argc, char** argv) {
    std::string input;
    DebugOptions options;
    bool any_filter = false;
    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--lines") {
            options.lines = true;
            any_filter = true;
        } else if (argument == "--types") {
            options.types = true;
            any_filter = true;
        } else if (argument == "--vars") {
            options.vars = true;
            any_filter = true;
        } else if (argument == "--frames") {
            options.frames = true;
            any_filter = true;
        } else if (argument == "--objects") {
            options.objects = true;
            any_filter = true;
        } else if (argument == "--json") {
            std::cerr << "kr16 debug: --json is not available in v1\n";
            return 2;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << command_help("debug");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 debug: unknown option " << argument << '\n';
            return 2;
        } else if (input.empty()) {
            input = argument;
        } else {
            std::cerr << "kr16 debug: multiple input files\n";
            return 2;
        }
    }
    if (input.empty()) {
        std::cerr << "kr16 debug: missing input file\n";
        return 2;
    }
    if (!any_filter) {
        options = DebugOptions{true, true, true, true, true};
    }

    try {
        const auto container = Container::read(read_bytes(input));
        std::cout << dump_debug_info(container, options);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kr16 debug: " << error.what() << '\n';
        return 1;
    }
}

int run_cc_command(int argc, char** argv) {
    std::string input;
    std::string output;
    bool assembly_only = false;
    bool object_only = false;
    CompileOptions options;

    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-S") {
            assembly_only = true;
        } else if (argument == "-c") {
            object_only = true;
        } else if (argument == "-o") {
            if (++i >= argc) {
                std::cerr << "kr16 cc: -o needs a path\n";
                return 2;
            }
            output = argv[i];
        } else if (argument == "-g") {
            options.debug = true;
        } else if (argument == "-O0") {
            options.optimize = false;
        } else if (argument == "-O1") {
            options.optimize = true;
        } else if (argument == "-std=c99" || argument == "-std=c11") {
            // Accepted; this compiler is C99-like.
        } else if (argument == "-I") {
            if (++i >= argc) {
                std::cerr << "kr16 cc: -I needs a path\n";
                return 2;
            }
            options.include_paths.push_back(argv[i]);
        } else if (argument.size() > 2 && argument.rfind("-I", 0) == 0) {
            options.include_paths.push_back(argument.substr(2));
        } else if (argument == "-D" || argument == "-U") {
            if (++i >= argc) {
                std::cerr << "kr16 cc: " << argument << " needs a value\n";
                return 2;
            }
        } else if (argument == "-E") {
            std::cerr << "kr16 cc: -E is not available in the subset compiler\n";
            return 2;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << command_help("cc");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 cc: unknown option " << argument << '\n';
            return 2;
        } else if (input.empty()) {
            input = argument;
        } else {
            std::cerr << "kr16 cc: multiple input files\n";
            return 2;
        }
    }
    if (input.empty()) {
        std::cerr << "kr16 cc: missing input file\n";
        return 2;
    }
    if (assembly_only && object_only) {
        std::cerr << "kr16 cc: -S and -c are mutually exclusive\n";
        return 2;
    }

    try {
        const auto bytes = read_bytes(input);
        const std::string source(bytes.begin(), bytes.end());
        options.source_name = input;
        const auto result = compile_c(source, options);
        if (!result.ok) {
            for (const auto& diagnostic : result.diagnostics) {
                std::cerr
                    << (diagnostic.file.empty() ? input : diagnostic.file)
                    << ":" << diagnostic.line << ":" << diagnostic.column
                    << ": " << diagnostic.message << '\n';
            }
            return 1;
        }
        if (output.empty()) {
            output = input;
            const auto dot = output.find_last_of('.');
            if (dot != std::string::npos) {
                output.erase(dot);
            }
            output += assembly_only ? ".s" : ".o";
        }
        if (assembly_only) {
            const std::string& text = result.assembly;
            write_bytes(output, std::span<const std::uint8_t>(
                                    reinterpret_cast<const std::uint8_t*>(
                                        text.data()),
                                    text.size()));
            return 0;
        }
        const auto assembled = assemble_text(
            result.assembly,
            AsmOptions{false, 0x0100, options.debug, {}, input});
        if (!assembled.ok) {
            for (const auto& diagnostic : assembled.diagnostics) {
                std::cerr << diagnostic.file << ":" << diagnostic.line << ":"
                          << diagnostic.column << ": " << diagnostic.message
                          << '\n';
            }
            return 1;
        }
        write_bytes(output, assembled.object.write());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "kr16 cc: " << error.what() << '\n';
        return 1;
    }
}

int run_dap_command(int argc, char** argv) {
    std::string program;
    std::uint16_t port = 22152;
    dap::DapOptions options;

    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--port") {
            if (++i >= argc) {
                std::cerr << "kr16 dap: --port needs a number\n";
                return 2;
            }
            try {
                const auto value = std::stoul(argv[i], nullptr, 0);
                if (value > 0xFFFF) {
                    throw std::out_of_range("port");
                }
                port = static_cast<std::uint16_t>(value);
            } catch (const std::exception&) {
                std::cerr << "kr16 dap: bad --port value\n";
                return 2;
            }
        } else if (argument == "--raw") {
            options.raw = true;
        } else if (argument == "--base") {
            if (++i >= argc) {
                std::cerr << "kr16 dap: --base needs an address\n";
                return 2;
            }
            try {
                options.base = parse_u16(argv[i]);
            } catch (const std::exception& error) {
                std::cerr << "kr16 dap: " << error.what() << '\n';
                return 2;
            }
        } else if (argument == "--bios") {
            if (++i >= argc) {
                std::cerr << "kr16 dap: --bios needs a path\n";
                return 2;
            }
            options.bios_path = argv[i];
        } else if (argument == "--no-bios") {
            options.no_bios = true;
        } else if (argument == "--ready-file") {
            if (++i >= argc) {
                std::cerr << "kr16 dap: --ready-file needs a path\n";
                return 2;
            }
            options.ready_file = argv[i];
        } else if (argument == "--stop-on-entry") {
            options.stop_on_entry = true;
        } else if (argument == "--no-stop-on-entry") {
            options.stop_on_entry = false;
        } else if (argument == "--force" || argument == "-f") {
            // Accepted for interactive-shell use; no effect on the adapter.
        } else if (argument == "--help" || argument == "-h") {
            std::cout << command_help("dap");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 dap: unknown option " << argument << '\n';
            return 2;
        } else if (program.empty()) {
            program = argument;
        } else {
            std::cerr << "kr16 dap: multiple program files\n";
            return 2;
        }
    }

    if (program.empty()) {
        std::cerr << "kr16 dap: missing program\n";
        return 2;
    }
    options.program = program;
    dap::DapServer server(options);
    return server.run(port);
}

int run_build_command(int argc, char** argv) {
    BuildOptions options;
    std::string input;
    bool listing = false;
    bool run_after = false;

    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        const std::string flag = lowercase_copy(argument);
        if (flag == "-o") {
            if (++i >= argc) {
                std::cerr << "kr16 build: -o needs a path\n";
                return 2;
            }
            options.output = argv[i];
        } else if (flag == "-i") {
            if (++i >= argc) {
                std::cerr << "kr16 build: -I needs a path\n";
                return 2;
            }
            options.include_paths.emplace_back(argv[i]);
        } else if (flag.rfind("-i", 0) == 0 && flag.size() > 2) {
            options.include_paths.emplace_back(argument.substr(2));
        } else if (flag.rfind("-l=", 0) == 0 ||
                   flag.rfind("--listing=", 0) == 0) {
            listing = true;
            options.listing = argument.substr(argument.find('=') + 1);
        } else if (flag == "-l") {
            listing = true;
        } else if (flag == "-g") {
            options.debug = true;
        } else if (flag == "--no-debug") {
            options.debug = false;
        } else if (flag == "-o0") {
            options.optimize = false;
        } else if (flag == "-o1") {
            options.optimize = true;
        } else if (flag == "--run") {
            run_after = true;
        } else if (flag == "--libc") {
            options.link_libc = true;
        } else if (flag == "--help" || flag == "-h") {
            std::cout << command_help("build");
            return 0;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "kr16 build: unknown option " << argument << '\n';
            return 2;
        } else if (input.empty()) {
            input = argument;
        } else {
            std::cerr << "kr16 build: multiple input files\n";
            return 2;
        }
    }

    if (input.empty()) {
        std::cerr << "kr16 build: missing input file\n";
        if (!options.include_paths.empty()) {
            std::cerr << "kr16 build: -I takes an include directory; "
                         "-l/-L writes a listing\n";
        }
        return 2;
    }

    options.input = input;
    if (!is_build_input(options.input)) {
        std::cerr << "kr16 build: unsupported file type '"
                  << options.input.extension().string()
                  << "' (expected .c, .s, .asm, or .o)\n";
        return 2;
    }
    if (options.input.extension() == ".c" ||
        options.input.extension() == ".C") {
        options.link_libc = true;
    }
    if (options.output.empty()) {
        options.output = input;
        options.output.replace_extension(".kex");
    }
    if (listing && options.listing.empty()) {
        options.listing = options.input;
        options.listing.replace_extension(".lst");
    }

    const auto result = build_program(options, std::cerr);
    if (!result.ok) {
        return 1;
    }
    std::cout << "build: wrote " << result.executable.string();
    if (!result.listing.empty()) {
        std::cout << " and " << result.listing.string();
    }
    std::cout << '\n';
    if (run_after) {
        std::string path = result.executable.string();
        char* vm_argv[] = {path.data()};
        return run_vm_command(1, vm_argv);
    }
    return 0;
}

}  // namespace

std::string usage_text() {
    std::string text =
        "usage: kr16 <command> [options]\n"
        "\n"
        "Commands:\n"
        "  as       Assemble a .s file into a relocatable .o\n"
        "  dis      Disassemble a .o or .kex\n"
        "  cc       Compile C to assembly, then assemble to .o\n"
        "  build    Compile, assemble, and link one input into a .kex\n"
        "  ld       Link .o files into a .kex\n"
        "  size     Report code, data and bss sizes\n"
        "  debug    Dump decoded symbols, lines, types, variables, frames, objects\n"
        "  vm       Run a .kex (or raw binary) on the virtual machine\n"
        "  dap      Run the VM with the debug adapter listening on TCP\n"
        "  version  Print tool, ISA, container, host-service ABI, and debug versions\n"
        "  shell    Start the interactive shell\n"
        "  (none)   Start the windowed UI (when built)\n"
        "  help     Print general help (also: kr16 help <command>)\n"
        "\n"
        "Every command accepts --help (or -h). Examples:\n"
        "  kr16 as --help\n"
        "  kr16 vm --help\n"
        "  kr16 help build\n";
    return text;
}

bool is_known_command(std::string_view command) {
    for (const auto known : kCommands) {
        if (known == command) {
            return true;
        }
    }
    return false;
}

int run(int argc, char** argv) {
    if (argc < 2) {
        const bool interactive = platform::stdin_is_terminal();
        return run_shell(std::cin, std::cout, std::cerr, interactive,
                         interactive ? default_history_path() : std::string{});
    }

    const std::string_view command = argv[1];
    if (command == "help" || command == "--help" || command == "-h") {
        if (argc > 2) {
            if (std::string_view(argv[2]) == "--help" ||
                std::string_view(argv[2]) == "-h") {
                std::cout << command_help("help");
                return 0;
            }
            const auto help = command_help(argv[2]);
            if (!help.empty()) {
                std::cout << help;
                return 0;
            }
            std::cerr << "kr16: unknown command '" << argv[2] << "'\n";
            return 2;
        }
        std::cout << usage_text();
        return 0;
    }
    if (command == "version" || command == "--version") {
        if (argc > 2 &&
            (std::string_view(argv[2]) == "--help" ||
             std::string_view(argv[2]) == "-h")) {
            std::cout << command_help("version");
            return 0;
        }
        std::cout << format_version() << '\n';
        return 0;
    }
    if (command == "shell") {
        if (argc > 2 &&
            (std::string_view(argv[2]) == "--help" ||
             std::string_view(argv[2]) == "-h")) {
            std::cout << command_help("shell");
            return 0;
        }
        const bool interactive = platform::stdin_is_terminal();
        return run_shell(std::cin, std::cout, std::cerr, interactive,
                         interactive ? default_history_path() : std::string{});
    }
    if (command == "as") {
        return run_as(argc - 2, argv + 2);
    }
    if (command == "build") {
        return run_build_command(argc - 2, argv + 2);
    }
    if (command == "dis") {
        return run_dis(argc - 2, argv + 2);
    }
    if (command == "vm") {
        return run_vm_command(argc - 2, argv + 2);
    }
    if (command == "size") {
        return run_size_command(argc - 2, argv + 2);
    }
    if (command == "ld") {
        return run_ld_command(argc - 2, argv + 2);
    }
    if (command == "debug") {
        return run_debug_command(argc - 2, argv + 2);
    }
    if (command == "cc") {
        return run_cc_command(argc - 2, argv + 2);
    }
    if (command == "dap") {
        return run_dap_command(argc - 2, argv + 2);
    }
    if (!is_known_command(command)) {
        std::cerr << "kr16: unknown command '" << command << "'\n\n";
        std::cerr << usage_text();
        return 2;
    }

    std::cerr << "kr16: command '" << command
              << "' is not implemented yet\n";
    return 2;
}

}  // namespace kr16::cli





