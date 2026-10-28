#include "kr16/cli.hpp"

#include <array>
#include <iostream>

#include "kr16/version.hpp"

namespace kr16::cli {
namespace {

constexpr std::array<std::string_view, 10> kCommands = {
    "as", "dis", "cc", "ld", "size", "debug", "vm", "dap", "version", "help",
};

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
