#include "kr16/vm.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "kr16/container.hpp"
#include "kr16/cpu.hpp"
#include "kr16/host.hpp"
#include "kr16/machine.hpp"

namespace kr16 {
namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string base_name(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

std::uint16_t symbol_value(const Container& container, const std::string& name,
                           std::uint16_t fallback) {
    for (const auto& symbol : container.symbols) {
        if (symbol.name == name) {
            return static_cast<std::uint16_t>(symbol.value);
        }
    }
    return fallback;
}

}  // namespace

VmResult run_vm(const std::string& program_path, const VmOptions& options) {
    VmResult result;
    try {
        const auto program = read_file(program_path);
        Machine machine;
        machine.zero_ram();
        Cpu cpu;
        cpu.reset();

        std::uint16_t entry = options.base;
        Container container;
        bool is_container = false;

        if (options.raw) {
            machine.load_bytes(options.base, program);
        } else {
            container = Container::read(program);
            is_container = true;
            for (const auto& section : container.sections) {
                if ((section.flags & SectionAlloc) == 0 ||
                    section.type == SectionType::Nobits) {
                    continue;
                }
                if (section.address + section.data.size() > 0x10000) {
                    throw std::runtime_error("section exceeds address space");
                }
                machine.load_bytes(section.address, section.data);
            }
            entry = container.entry;
        }

        std::uint16_t stack_limit = is_container
                                        ? symbol_value(container, "__stack_limit",
                                                       0xE000)
                                        : 0xE000;

        std::uint16_t sp = 0xF000;
        if (!options.program_args.empty() || !options.raw) {
            std::vector<std::string> args;
            args.push_back(base_name(program_path));
            args.insert(args.end(), options.program_args.begin(),
                        options.program_args.end());
            if (args.size() > 32) {
                throw std::runtime_error("too many program arguments");
            }

            std::uint16_t cursor = 0xF000;
            std::vector<std::uint16_t> pointers;
            for (const auto& argument : args) {
                const auto length = static_cast<std::uint16_t>(argument.size() + 1);
                if (static_cast<std::uint32_t>(cursor) <
                    static_cast<std::uint32_t>(stack_limit) + length + 64) {
                    throw std::runtime_error("argument block does not fit on stack");
                }
                cursor = static_cast<std::uint16_t>(cursor - length);
                cursor = static_cast<std::uint16_t>(cursor & ~1u);
                machine.load_bytes(
                    cursor, std::span<const std::uint8_t>(
                                reinterpret_cast<const std::uint8_t*>(argument.data()),
                                argument.size()));
                machine.write8(static_cast<std::uint16_t>(cursor + length - 1), 0);
                pointers.push_back(cursor);
            }

            const auto array_size =
                static_cast<std::uint16_t>((pointers.size() + 1) * 2);
            std::uint16_t array_address =
                static_cast<std::uint16_t>((cursor - array_size) & ~1u);
            if (array_address < stack_limit) {
                throw std::runtime_error("argv block does not fit on stack");
            }
            for (std::size_t i = 0; i < pointers.size(); ++i) {
                machine.write16(static_cast<std::uint16_t>(array_address + i * 2),
                                pointers[i]);
            }
            machine.write16(static_cast<std::uint16_t>(
                                array_address + pointers.size() * 2),
                            0);

            cpu.set_reg(1, static_cast<std::uint16_t>(pointers.size()));
            cpu.set_reg(2, array_address);
            sp = static_cast<std::uint16_t>(array_address - 2);
        }

        if (!options.no_bios && !options.bios_path.empty()) {
            const auto bios = read_file(options.bios_path);
            if (bios.size() > Machine::kRomSize) {
                throw std::runtime_error("BIOS image is larger than 3.75 KiB");
            }
            machine.load_rom(bios);
            const auto reset = machine.read16(0xFFFE);
            if (reset == 0) {
                throw std::runtime_error("BIOS reset vector is zero");
            }
            cpu.set_pc(reset);
        } else {
            if (entry < 0x0100) {
                throw std::runtime_error("program entry is below the first page");
            }
            cpu.set_pc(entry);
        }

        cpu.set_sp(sp);
        cpu.set_stack_limit(stack_limit);

        std::vector<std::uint8_t> input;
        if (!options.input_path.empty()) {
            input = read_file(options.input_path);
        }

        HostServices host(options.trace_syscalls);
        cpu.set_syscall_handler(
            [&host](std::uint8_t service, std::uint16_t block,
                    MemoryBus& bus) { host.call(service, block, bus); });
        cpu.set_interrupt_ack([&machine]() { return machine.pending_interrupt(); });

        std::size_t input_index = 0;
        std::size_t output_index = 0;
        std::uint64_t cycles = 0;
        std::uint64_t instructions = 0;
        std::ostringstream trace;

        while (!cpu.halted()) {
            if (options.max_cycles != 0 && cycles >= options.max_cycles) {
                result.timed_out = true;
                result.exit_code = 124;
                break;
            }
            if (!machine.keyboard_available() && input_index < input.size()) {
                machine.push_key(input[input_index++]);
            }

            const auto pc = cpu.pc();
            const auto used = cpu.step(machine);
            if (used == 0) {
                break;
            }
            cycles += used;
            ++instructions;
            machine.tick(used);

            if (options.trace) {
                trace << "pc=" << std::hex << pc << std::dec
                      << " cycles=" << used << "\n";
            }

            const auto& output = machine.console_output();
            while (output_index < output.size()) {
                result.output.push_back(static_cast<char>(output[output_index++]));
            }
        }

        const auto& output = machine.console_output();
        while (output_index < output.size()) {
            result.output.push_back(static_cast<char>(output[output_index++]));
        }

        result.cycles = cycles;
        result.instructions = instructions;
        result.trace = trace.str();
        result.syscall_trace = host.trace();
        if (!result.timed_out) {
            if (cpu.halt_reason() == "HLT") {
                result.exit_code = static_cast<int>(cpu.reg(0) & 0xFF);
            } else if (cpu.halt_reason().rfind("unhandled", 0) == 0) {
                result.exit_code = 1;
                result.error = cpu.halt_reason();
            } else {
                result.exit_code = 0;
            }
        }
        result.ok = true;
        return result;
    } catch (const std::exception& error) {
        result.ok = false;
        result.exit_code = 1;
        result.error = error.what();
        return result;
    }
}

}  // namespace kr16
