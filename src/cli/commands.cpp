#include "kr16/cli.hpp"

#include <array>
#include <cstdint>
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
#include "kr16/container.hpp"
#include "kr16/diagnostic.hpp"
#include "kr16/disassembler.hpp"
#include "kr16/version.hpp"
#include "kr16/vm.hpp"

namespace kr16::cli {
namespace {

constexpr std::array<std::string_view, 10> kCommands = {
    "as", "dis", "cc", "ld", "size", "debug", "vm", "dap", "version", "help",
};

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
            std::cout << "usage: kr16 as file.s -o file.o [--raw --base addr] "
                         "[-I dir] [-g]\n";
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
            std::cout << "usage: kr16 dis file.o|file.kex [--raw --base addr] "
                         "[--no-hex]\n";
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
        } else if (argument == "--headless" || argument == "--line-input") {
            options.headless = true;
        } else if (argument == "-i") {
            std::cerr << "kr16 vm: interactive monitor is not available yet\n";
            return 2;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "usage: kr16 vm program.kex|program.bin [options] "
                         "[-- args...]\n"
                         "  --raw --base addr   load a raw image\n"
                         "  --bios path         load a 3.75 KiB BIOS ROM\n"
                         "  --no-bios           start directly at the entry point\n"
                         "  --max-cycles n      stop after n cycles\n"
                         "  --input path        feed scripted keyboard bytes\n"
                         "  --trace --stats     diagnostic output\n";
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

int run_size_command(int argc, char** argv) {
    std::string input;
    for (int i = 0; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << "usage: kr16 size file.kex\n";
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

}  // namespace

std::string usage_text() {
    std::string text =
        "usage: kr16 <command> [options]\n"
        "\n"
        "Commands:\n"
        "  as       Assemble a .s file into a relocatable .o\n"
        "  dis      Disassemble a .o or .kex\n"
        "  cc       Compile C to assembly, then assemble to .o\n"
        "  ld       Link .o files into a .kex\n"
        "  size     Report code/data/bss and free heap/stack\n"
        "  debug    Dump decoded symbols, lines, types, variables, frames, objects\n"
        "  vm       Run a .kex (or raw binary) on the virtual machine\n"
        "  dap      Run the VM with the debug adapter listening on TCP\n"
        "  version  Print tool, ISA, container, host-service ABI, and debug versions\n"
        "  help     Print help (also: kr16 <command> --help)\n";
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
        std::cout << usage_text();
        return 0;
    }

    const std::string_view command = argv[1];
    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << usage_text();
        return 0;
    }
    if (command == "version" || command == "--version") {
        std::cout << format_version() << '\n';
        return 0;
    }
    if (command == "as") {
        return run_as(argc - 2, argv + 2);
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
