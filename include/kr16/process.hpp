#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kr16/cpu.hpp"

namespace kr16 {

struct ProcessStackLayout {
    std::uint16_t sp = 0;
    std::uint16_t argc = 0;
    std::uint16_t argv = 0;
};

constexpr std::size_t kMaxProcessArguments = 32;
constexpr std::size_t kMaxProcessCommandLine = 1024;

/// Build the C process argument block on the stack.
///
/// The block is laid out below `stack_top` (normally 0xF000):
///
///     strings, highest address first
///     argv[argc] = NULL
///     argv[argc-1] ... argv[0]
///     SP -> below the pointer array
///
/// `program_name` becomes argv[0]; `arguments` become argv[1..argc-1].
/// `argc` and `argv` are returned for R1/R2. The caller is responsible for
/// setting PC and SP after a successful call.
bool initialize_process_stack(
    MemoryBus& memory, std::string_view program_name,
    const std::vector<std::string>& arguments, std::uint16_t stack_top,
    std::uint16_t stack_limit, ProcessStackLayout& layout,
    std::string& error);

}  // namespace kr16
