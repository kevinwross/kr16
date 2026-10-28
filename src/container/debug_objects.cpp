#include "kr16/debug_objects.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "kr16/bytes.hpp"
#include "kr16/diagnostic.hpp"

namespace kr16 {
namespace {

constexpr std::uint32_t kDebugObjectsMagic = 0x4F474244u;  // "DBGO"
constexpr std::uint16_t kDebugObjectsVersion = 1;

void require(bool condition, std::size_t offset, const char* message) {
    if (!condition) {
        throw ParseError(offset, message);
    }
}

void require_zero(std::uint32_t value, std::size_t offset, const char* field) {
    if (value != 0) {
        throw ParseError(offset, std::string(field) + " must be zero");
    }
}

}  // namespace

std::vector<std::uint8_t> serialize_debug_objects(const DebugObjects& objects,
                                                  StringTable& strings) {
    if (objects.objects.size() > std::numeric_limits<std::uint32_t>::max() ||
        objects.sources.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw ParseError(0, "too many debug objects or sources");
    }

    ByteWriter out;
    const std::array<std::uint8_t, 4> magic = {'D', 'B', 'G', 'O'};
    out.write_bytes(std::span<const std::uint8_t>(magic));
    out.write_u16(kDebugObjectsVersion);
    out.write_u16(0);
    out.write_u32(static_cast<std::uint32_t>(objects.objects.size()));
    out.write_u32(static_cast<std::uint32_t>(objects.sources.size()));

    for (const auto& object : objects.objects) {
        out.write_u16(object.id);
        out.write_u16(0);
        out.write_u32(strings.add(object.name));
        out.write_u16(object.code_address);
        out.write_u16(0);
        out.write_u32(object.code_size);
        out.write_u32(object.line_offset);
        out.write_u32(object.line_size);
        out.write_u32(object.types_offset);
        out.write_u32(object.types_size);
        out.write_u32(object.vars_offset);
        out.write_u32(object.vars_size);
        out.write_u32(object.frames_offset);
        out.write_u32(object.frames_size);
    }

    for (const auto& source : objects.sources) {
        out.write_u32(strings.add(source.path));
        out.write_u32(strings.add(source.build_directory));
        out.write_u16(source.flags);
        out.write_u16(0);
    }

    return out.take();
}

DebugObjects parse_debug_objects(std::span<const std::uint8_t> data,
                                 const StringTable& strings) {
    ByteReader reader(data);
    require(reader.read_u32() == kDebugObjectsMagic, 0,
            "invalid .debug_objects magic");
    const auto version = reader.read_u16();
    require(version == kDebugObjectsVersion, 4,
            "unsupported .debug_objects version");
    require_zero(reader.read_u16(), 6, "debug_objects reserved field");
    const auto object_count = reader.read_u32();
    const auto source_count = reader.read_u32();

    const auto object_bytes = static_cast<std::uint64_t>(object_count) *
                              DebugObjects::ObjectRecordSize;
    const auto source_bytes = static_cast<std::uint64_t>(source_count) *
                              DebugObjects::SourceRecordSize;
    const auto expected = static_cast<std::uint64_t>(DebugObjects::HeaderSize) +
                          object_bytes + source_bytes;
    require(expected == data.size(), DebugObjects::HeaderSize,
            ".debug_objects size does not match its counts");

    DebugObjects result;
    result.objects.reserve(object_count);
    for (std::uint32_t i = 0; i < object_count; ++i) {
        const auto offset = DebugObjects::HeaderSize +
                            static_cast<std::size_t>(i) *
                                DebugObjects::ObjectRecordSize;
        DebugObjectRecord object;
        object.id = reader.read_u16();
        require_zero(reader.read_u16(), offset + 2,
                     "debug object reserved field");
        object.name = std::string(strings.get(reader.read_u32()));
        object.code_address = reader.read_u16();
        require_zero(reader.read_u16(), offset + 10,
                     "debug object reserved field");
        object.code_size = reader.read_u32();
        object.line_offset = reader.read_u32();
        object.line_size = reader.read_u32();
        object.types_offset = reader.read_u32();
        object.types_size = reader.read_u32();
        object.vars_offset = reader.read_u32();
        object.vars_size = reader.read_u32();
        object.frames_offset = reader.read_u32();
        object.frames_size = reader.read_u32();
        result.objects.push_back(std::move(object));
    }

    result.sources.reserve(source_count);
    for (std::uint32_t i = 0; i < source_count; ++i) {
        const auto offset = DebugObjects::HeaderSize + object_bytes +
                            static_cast<std::size_t>(i) *
                                DebugObjects::SourceRecordSize;
        DebugSourceRecord source;
        source.path = std::string(strings.get(reader.read_u32()));
        source.build_directory = std::string(strings.get(reader.read_u32()));
        source.flags = reader.read_u16();
        require_zero(reader.read_u16(), offset + 10,
                     "debug source reserved field");
        result.sources.push_back(std::move(source));
    }

    return result;
}

}  // namespace kr16
