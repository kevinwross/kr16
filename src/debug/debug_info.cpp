#include "kr16/debug_info.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "kr16/debug_objects.hpp"

namespace kr16 {
namespace {

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        throw ParseError(static_cast<std::uint32_t>(offset),
                         "truncated debug section");
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        throw ParseError(static_cast<std::uint32_t>(offset),
                         "truncated debug section");
    }
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::string hex(std::uint32_t value, int width = 4) {
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << std::setw(width)
         << std::setfill('0') << value;
    return text.str();
}

}  // namespace

std::vector<DebugLineRow> parse_debug_line(
    std::span<const std::uint8_t> data) {
    if (data.size() < 8) {
        throw ParseError(0, "truncated .debug_line header");
    }
    const auto count = read_u32(data, 0);
    const auto version = read_u16(data, 4);
    if (version != 1) {
        throw ParseError(4, "unsupported .debug_line version");
    }
    if (8 + static_cast<std::uint64_t>(count) * 8 > data.size()) {
        throw ParseError(8, "truncated .debug_line rows");
    }
    std::vector<DebugLineRow> rows;
    rows.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto offset = 8 + static_cast<std::size_t>(i) * 8;
        DebugLineRow row;
        row.address = read_u16(data, offset);
        row.file = read_u16(data, offset + 2);
        row.line = read_u16(data, offset + 4);
        row.column = data[offset + 6];
        row.flags = data[offset + 7];
        rows.push_back(row);
    }
    return rows;
}

std::string dump_debug_info(const Container& container,
                            const DebugOptions& options) {
    const bool all = !options.lines && !options.types && !options.vars &&
                     !options.frames && !options.objects;
    std::ostringstream out;

    if (all || options.lines) {
        bool found = false;
        for (const auto& section : container.sections) {
            if (section.name != ".debug_line") {
                continue;
            }
            found = true;
            out << "line table (" << section.name << ")\n";
            for (const auto& row : parse_debug_line(section.data)) {
                out << "  " << hex(row.address) << "  file " << row.file
                    << ":" << row.line << ":" << static_cast<unsigned>(row.column)
                    << "\n";
            }
        }
        if (!found) {
            out << "no line table\n";
        }
    }

    auto dump_count = [&](const char* name, bool wanted) {
        if (!all && !wanted) {
            return;
        }
        const auto* section = container.find_section(name);
        if (section == nullptr) {
            out << name << ": absent\n";
            return;
        }
        const auto count =
            section->data.size() >= 4 ? read_u32(section->data, 0) : 0;
        out << name << ": " << section->data.size() << " bytes, " << count
            << " records\n";
    };
    dump_count(".debug_types", options.types);
    dump_count(".debug_vars", options.vars);
    dump_count(".debug_frames", options.frames);

    if (all || options.objects) {
        const auto* objects = container.find_section(".debug_objects");
        if (objects == nullptr) {
            out << ".debug_objects: absent\n";
        } else {
            const auto* strings = container.find_section(".debug_str");
            if (strings == nullptr) {
                out << ".debug_objects: missing .debug_str\n";
            } else {
                const auto table = StringTable::parse(strings->data);
                const auto decoded = parse_debug_objects(objects->data, table);
                out << "sources: " << decoded.sources.size() << "\n";
                for (std::size_t i = 0; i < decoded.sources.size(); ++i) {
                    out << "  [" << i << "] " << decoded.sources[i].path
                        << "  build=" << decoded.sources[i].build_directory
                        << "\n";
                }
                out << "objects: " << decoded.objects.size() << "\n";
                for (const auto& object : decoded.objects) {
                    out << "  [" << object.id << "] " << object.name
                        << " code=" << hex(object.code_address) << "+"
                        << object.code_size << "\n";
                }
            }
        }
    }
    return out.str();
}

}  // namespace kr16
