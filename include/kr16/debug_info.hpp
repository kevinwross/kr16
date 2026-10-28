#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "kr16/container.hpp"

namespace kr16 {

struct DebugLineRow {
    std::uint16_t address = 0;
    std::uint16_t file = 0;
    std::uint16_t line = 0;
    std::uint8_t column = 0;
    std::uint8_t flags = 0;
};

struct DebugOptions {
    bool lines = false;
    bool types = false;
    bool vars = false;
    bool frames = false;
    bool objects = false;
};

std::vector<DebugLineRow> parse_debug_line(std::span<const std::uint8_t> data);
std::string dump_debug_info(const Container& container,
                            const DebugOptions& options);

}  // namespace kr16
