#include "kr16/debug_instruments.hpp"
#include "kr16/vm.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "kr16/container.hpp"
#include "kr16/cpu.hpp"
#include "kr16/host.hpp"
#include "kr16/machine.hpp"
#include "kr16/process.hpp"
#include "kr16/resources.hpp"

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
            ProcessStackLayout layout;
            std::string stack_error;
            if (!initialize_process_stack(
                    machine, base_name(program_path), options.program_args,
                    0xF000, stack_limit, layout, stack_error)) {
                throw std::runtime_error(stack_error);
            }
            cpu.set_reg(1, layout.argc);
            cpu.set_reg(2, layout.argv);
            sp = layout.sp;
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
        } else if (!options.no_bios && entry == 0x0100) {
            machine.load_rom(embedded_bios_rom());
            const auto reset = machine.read16(0xFFFE);
            if (reset == 0) {
                throw std::runtime_error("embedded BIOS reset vector is zero");
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

        HostServices host(
            options.trace_syscalls,
            HostFsOptions{options.fs_root, !options.no_host_fs,
                          options.fs_readonly, options.fs_root_explicit,
                          options.fs_quota});
        cpu.set_syscall_handler(
            [&host](std::uint8_t service, std::uint16_t block,
                    MemoryBus& bus) { host.call(service, block, bus); });
        cpu.set_interrupt_ack([&machine]() { return machine.pending_interrupt(); });

        std::size_t input_index = 0;
        std::size_t output_index = 0;
        std::uint64_t cycles = 0;
        std::uint64_t instructions = 0;
        std::ostringstream trace;

        if (options.console) {
            // The keyboard asks for a line when the guest reads it empty, so a
            // getchar blocks where a terminal would wait.
            machine.set_keyboard_refill([&machine] {
                std::string line;
                if (std::getline(std::cin, line)) {
                    for (const char character : line) {
                        machine.push_key(static_cast<std::uint8_t>(character));
                    }
                    machine.push_key('\n');
                }
            });
        }

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
            if (g_debug.breakpoint && pc == g_debug.break_address) {
                std::printf("break at 0x%04X: ", static_cast<unsigned>(pc));
                for (std::uint8_t reg = 0; reg < 5; ++reg) {
                    std::printf("r%u=0x%04X ", static_cast<unsigned>(reg),
                                static_cast<unsigned>(cpu.reg(reg)));
                }
                std::printf("pc=0x%04X\n", static_cast<unsigned>(cpu.pc()));
                if (++g_debug.break_hits >= g_debug.break_limit) {
                    break;
                }
            }
            std::uint16_t watched_before = 0;
            if (g_debug.watch) {
                watched_before = static_cast<std::uint16_t>(
                    machine.read8(g_debug.watch_address) |
                    (machine.read8(static_cast<std::uint16_t>(g_debug.watch_address + 1))
                     << 8));
            }
            if (g_debug.page && !g_debug.page_valid) {
                for (std::uint32_t byte = 0; byte < 4096; ++byte) {
                    g_debug.page_before[byte] =
                        machine.read8(static_cast<std::uint16_t>(g_debug.page_base + byte));
                }
                g_debug.page_valid = true;
            }
            const auto used = cpu.step(machine);
            if (g_debug.watch) {
                const std::uint16_t watched_after = static_cast<std::uint16_t>(
                    machine.read8(g_debug.watch_address) |
                    (machine.read8(static_cast<std::uint16_t>(g_debug.watch_address + 1))
                     << 8));
                if (watched_after != watched_before) {
                    std::printf("watch 0x%04X at pc=0x%04X: 0x%04X -> 0x%04X\n",
                                static_cast<unsigned>(g_debug.watch_address),
                                static_cast<unsigned>(pc),
                                static_cast<unsigned>(watched_before),
                                static_cast<unsigned>(watched_after));
                }
            }
            if (g_debug.page) {
                for (std::uint32_t byte = 0; byte < 4096; ++byte) {
                    const std::uint8_t value =
                        machine.read8(static_cast<std::uint16_t>(g_debug.page_base + byte));
                    if (value != g_debug.page_before[byte]) {
                        std::printf("page 0x%04X at pc=0x%04X: 0x%02X -> 0x%02X\n",
                                    static_cast<unsigned>(g_debug.page_base + byte),
                                    static_cast<unsigned>(pc),
                                    static_cast<unsigned>(g_debug.page_before[byte]),
                                    static_cast<unsigned>(value));
                        g_debug.page_before[byte] = value;
                    }
                }
            }
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
                const char byte = static_cast<char>(output[output_index++]);
                if (options.console) {
                    std::fputc(byte, stdout);
                    std::fflush(stdout);
                } else {
                    result.output.push_back(byte);
                }
            }
        }

        const auto& output = machine.console_output();
        while (output_index < output.size()) {
            const char byte = static_cast<char>(output[output_index++]);
            if (options.console) {
                std::fputc(byte, stdout);
                std::fflush(stdout);
            } else {
                result.output.push_back(byte);
            }
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
