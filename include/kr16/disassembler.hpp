#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "kr16/container.hpp"

namespace kr16 {

struct DisassemblyOptions {
    std::uint16_t base = 0;
    bool show_bytes = true;
};

std::string disassemble_raw(std::span<const std::uint8_t> bytes,
                            const DisassemblyOptions& options = {});
std::string disassemble_container(const Container& container,
                                  const DisassemblyOptions& options = {});

}  // namespace kr16
