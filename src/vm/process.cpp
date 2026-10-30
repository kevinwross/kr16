#include "kr16/process.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace kr16 {

bool initialize_process_stack(
    MemoryBus& memory, std::string_view program_name,
    const std::vector<std::string>& arguments, std::uint16_t stack_top,
    std::uint16_t stack_limit, ProcessStackLayout& layout,
    std::string& error) {
    layout = ProcessStackLayout{};
    if (program_name.empty()) {
        error = "program name is empty";
        return false;
    }
    if (arguments.size() + 1 > kMaxProcessArguments) {
        error = "too many program arguments (maximum 32)";
        return false;
    }
    if (stack_top < stack_limit) {
        error = "stack top is below the stack limit";
        return false;
    }
    if (stack_top > 0xF000) {
        error = "stack top overlaps the I/O region";
        return false;
    }

    const std::size_t argc = arguments.size() + 1;
    std::uint64_t command_line_bytes = program_name.size() + 1;
    for (const auto& argument : arguments) {
        command_line_bytes += argument.size() + 1;
    }
    if (command_line_bytes > kMaxProcessCommandLine) {
        error = "command line is too long (maximum 1024 bytes)";
        return false;
    }

    // Account for the worst-case one-byte alignment padding before each
    // string, then the argv pointer array and its trailing null.
    const std::uint64_t block_bytes =
        command_line_bytes + argc + (argc + 1) * 2;
    const std::uint64_t available =
        static_cast<std::uint64_t>(stack_top) - stack_limit;
    if (block_bytes > available) {
        error = "argument block does not fit above __stack_limit";
        return false;
    }

    std::uint32_t cursor = stack_top;
    std::vector<std::uint16_t> pointers;
    pointers.reserve(argc);

    auto write_string = [&](std::string_view value) {
        const std::uint32_t start =
            (cursor - static_cast<std::uint32_t>(value.size() + 1)) & ~1u;
        for (std::size_t i = 0; i < value.size(); ++i) {
            memory.write8(
                static_cast<std::uint16_t>(start + i),
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(value[i])));
        }
        memory.write8(
            static_cast<std::uint16_t>(start + value.size()), 0);
        pointers.push_back(static_cast<std::uint16_t>(start));
        cursor = start;
    };

    // The program name is written first and is therefore closest to the top
    // of the stack. Subsequent strings are placed below it.
    write_string(program_name);
    for (const auto& argument : arguments) {
        write_string(argument);
    }

    const std::uint32_t array_size =
        static_cast<std::uint32_t>((argc + 1) * 2);
    std::uint32_t array_address = (cursor - array_size) & ~1u;
    if (array_address < stack_limit) {
        error = "argv block does not fit above __stack_limit";
        return false;
    }
    for (std::size_t i = 0; i < pointers.size(); ++i) {
        memory.write16(
            static_cast<std::uint16_t>(array_address + i * 2),
            pointers[i]);
    }
    memory.write16(static_cast<std::uint16_t>(
                       array_address + pointers.size() * 2),
                   0);

    layout.sp = static_cast<std::uint16_t>(array_address);
    layout.argc = static_cast<std::uint16_t>(argc);
    layout.argv = static_cast<std::uint16_t>(array_address);
    return true;
}

}  // namespace kr16
