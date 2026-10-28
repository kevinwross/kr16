#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "kr16/container.hpp"

namespace kr16 {

struct DebugObjectRecord {
    std::uint16_t id = 0;
    std::string name;
    std::uint16_t code_address = 0;
    std::uint32_t code_size = 0;
    std::uint32_t line_offset = 0;
    std::uint32_t line_size = 0;
    std::uint32_t types_offset = 0;
    std::uint32_t types_size = 0;
    std::uint32_t vars_offset = 0;
    std::uint32_t vars_size = 0;
    std::uint32_t frames_offset = 0;
    std::uint32_t frames_size = 0;
};

struct DebugSourceRecord {
    std::string path;
    std::string build_directory;
    std::uint16_t flags = 0;
};

struct DebugObjects {
    std::vector<DebugObjectRecord> objects;
    std::vector<DebugSourceRecord> sources;

    static constexpr std::size_t HeaderSize = 16;
    static constexpr std::size_t ObjectRecordSize = 48;
    static constexpr std::size_t SourceRecordSize = 12;
};

std::vector<std::uint8_t> serialize_debug_objects(const DebugObjects& objects,
                                                  StringTable& strings);
DebugObjects parse_debug_objects(std::span<const std::uint8_t> data,
                                 const StringTable& strings);

}  // namespace kr16
