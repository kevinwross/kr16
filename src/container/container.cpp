#include "kr16/container.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "kr16/bytes.hpp"
#include "kr16/crc32.hpp"

namespace kr16 {
namespace {

constexpr std::uint32_t kMagic = 0x3631524Bu;  // "KR16" little-endian
constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kSectionHeaderSize = 32;
constexpr std::size_t kSymbolSize = 20;
constexpr std::size_t kRelocationSize = 16;
constexpr std::size_t kChecksumOffset = 52;

struct RawSection {
    std::uint32_t name_offset = 0;
    std::uint16_t type = 0;
    std::uint16_t flags = 0;
    std::uint16_t address = 0;
    std::uint16_t reserved = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t size = 0;
    std::uint16_t alignment = 0;
    std::uint16_t link = 0;
    std::uint16_t info = 0;
    std::uint16_t entry_size = 0;
    std::uint32_t reserved2 = 0;
};

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

std::uint16_t align_for(const Section& section) {
    return std::max<std::uint16_t>(2, section.alignment);
}

}  // namespace

std::uint32_t StringTable::add(std::string_view text) {
    if (text.find('\0') != std::string_view::npos) {
        throw ParseError(0, "string table entry contains NUL");
    }
    const std::string key(text);
    const auto existing = by_string_.find(key);
    if (existing != by_string_.end()) {
        return existing->second;
    }
    if (bytes_.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw ParseError(bytes_.size(), "string table is too large");
    }
    const auto offset = static_cast<std::uint32_t>(bytes_.size());
    bytes_.append(key);
    bytes_.push_back('\0');
    by_string_.emplace(key, offset);
    by_offset_.emplace(offset, key);
    return offset;
}

std::string_view StringTable::get(std::uint32_t offset) const {
    const auto it = by_offset_.find(offset);
    if (it == by_offset_.end()) {
        throw ParseError(offset, "invalid string table offset");
    }
    return it->second;
}

std::vector<std::uint8_t> StringTable::serialize() const {
    return std::vector<std::uint8_t>(bytes_.begin(), bytes_.end());
}

StringTable StringTable::parse(std::span<const std::uint8_t> data) {
    StringTable table;
    table.bytes_.assign(reinterpret_cast<const char*>(data.data()), data.size());
    std::size_t start = 0;
    while (start < data.size()) {
        std::size_t end = start;
        while (end < data.size() && data[end] != 0) {
            ++end;
        }
        if (end == data.size()) {
            throw ParseError(start, "string table is not NUL-terminated");
        }
        const std::string value(reinterpret_cast<const char*>(data.data() + start),
                               end - start);
        const auto offset = static_cast<std::uint32_t>(start);
        table.by_string_.emplace(value, offset);
        table.by_offset_.emplace(offset, value);
        start = end + 1;
    }
    return table;
}

std::vector<std::uint8_t> Container::write() const {
    if (sections.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw ParseError(0, "too many sections");
    }
    if (symbols.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw ParseError(0, "too many symbols");
    }
    if (kind == ContainerKind::Exec && !relocations.empty()) {
        throw ParseError(0, "EXEC container cannot contain relocations");
    }

    std::vector<Section> output_sections = sections;
    auto debug_str = std::find_if(
        output_sections.begin(), output_sections.end(),
        [](const Section& section) { return section.name == ".debug_str"; });
    if (debug_str == output_sections.end()) {
        Section section;
        section.name = ".debug_str";
        section.flags = SectionDebug;
        section.alignment = 2;
        output_sections.push_back(std::move(section));
        debug_str = std::prev(output_sections.end());
    }

    StringTable strings;
    for (const auto& section : output_sections) {
        strings.add(section.name);
    }
    for (const auto& symbol : symbols) {
        strings.add(symbol.name);
    }

    debug_str->type = SectionType::Progbits;
    debug_str->flags |= SectionDebug;
    debug_str->alignment = 2;
    debug_str->nobits_size = 0;
    debug_str->data = strings.serialize();

    std::uint32_t header_flags = flags;
    if (!symbols.empty()) {
        header_flags |= ContainerHasSymbols;
    }
    if (std::any_of(output_sections.begin(), output_sections.end(),
                    [](const Section& section) { return section.is_debug(); })) {
        header_flags |= ContainerHasDebug;
    }
    if (std::any_of(output_sections.begin(), output_sections.end(),
                    [](const Section& section) {
                        return section.name == ".debug_objects";
                    })) {
        header_flags |= ContainerHasObjects;
    }

    ByteWriter out;
    out.write_zeros(kHeaderSize);
    const auto section_table_offset = static_cast<std::uint32_t>(out.position());
    out.write_zeros(output_sections.size() * kSectionHeaderSize);

    std::vector<std::uint32_t> section_offsets(output_sections.size(), 0);
    std::vector<std::uint32_t> section_sizes(output_sections.size(), 0);
    for (std::size_t i = 0; i < output_sections.size(); ++i) {
        const Section& section = output_sections[i];
        out.align(align_for(section));
        section_offsets[i] = static_cast<std::uint32_t>(out.position());
        section_sizes[i] = section.byte_size();
        if (section.type == SectionType::Progbits) {
            out.write_bytes(section.data);
        }
    }

    out.align(2);
    const auto symbol_table_offset = static_cast<std::uint32_t>(out.position());
    for (const auto& symbol : symbols) {
        out.write_u32(strings.add(symbol.name));
        out.write_u16(symbol.value);
        out.write_u16(symbol.section);
        out.write_u32(symbol.size);
        out.write_u8(static_cast<std::uint8_t>(symbol.binding));
        out.write_u8(static_cast<std::uint8_t>(symbol.type));
        out.write_u8(static_cast<std::uint8_t>(symbol.visibility));
        out.write_u8(0);
        out.write_u16(symbol.object_index);
        out.write_u16(0);
    }

    std::uint32_t reloc_table_offset = 0;
    std::uint32_t reloc_table_size = 0;
    if (kind == ContainerKind::Rel) {
        out.align(2);
        reloc_table_offset = static_cast<std::uint32_t>(out.position());
        reloc_table_size = static_cast<std::uint32_t>(relocations.size() *
                                                      kRelocationSize);
        for (const auto& relocation : relocations) {
            out.write_u16(relocation.section);
            out.write_u16(static_cast<std::uint16_t>(relocation.type));
            out.write_u32(relocation.offset);
            out.write_u32(relocation.symbol);
            out.write_i32(relocation.addend);
        }
    }

    ByteWriter section_table;
    for (std::size_t i = 0; i < output_sections.size(); ++i) {
        const Section& section = output_sections[i];
        section_table.write_u32(strings.add(section.name));
        section_table.write_u16(static_cast<std::uint16_t>(section.type));
        section_table.write_u16(section.flags);
        section_table.write_u16(section.address);
        section_table.write_u16(0);
        section_table.write_u32(section_offsets[i]);
        section_table.write_u32(section_sizes[i]);
        section_table.write_u16(section.alignment);
        section_table.write_u16(section.link);
        section_table.write_u16(section.info);
        section_table.write_u16(section.entry_size);
        section_table.write_u32(0);
    }

    const auto debug_str_index =
        static_cast<std::size_t>(std::distance(output_sections.begin(), debug_str));
    const auto string_table_offset = section_offsets[debug_str_index];
    const auto string_table_size = section_sizes[debug_str_index];

    ByteWriter header;
    const std::array<std::uint8_t, 4> magic = {'K', 'R', '1', '6'};
    header.write_bytes(std::span<const std::uint8_t>(magic));
    header.write_u8(major);
    header.write_u8(minor);
    header.write_u16(static_cast<std::uint16_t>(kind));
    header.write_u32(header_flags);
    header.write_u32(static_cast<std::uint32_t>(kHeaderSize));
    header.write_u16(kind == ContainerKind::Rel ? 0 : entry);
    header.write_u16(0);
    header.write_u16(static_cast<std::uint16_t>(output_sections.size()));
    header.write_u16(0);
    header.write_u32(static_cast<std::uint32_t>(symbols.size()));
    header.write_u32(section_table_offset);
    header.write_u32(symbol_table_offset);
    header.write_u32(string_table_offset);
    header.write_u32(string_table_size);
    header.write_u32(reloc_table_offset);
    header.write_u32(reloc_table_size);
    header.write_u32(0);  // checksum placeholder
    header.write_zeros(8);
    require(header.position() == kHeaderSize, 0, "internal header size mismatch");

    out.patch_bytes(0, header.data());
    out.patch_bytes(section_table_offset, section_table.data());
    out.patch_u32(kChecksumOffset, crc32(out.data()));
    return out.take();
}

Container Container::read(std::span<const std::uint8_t> data) {
    ByteReader reader(data);
    const auto magic = reader.read_u32();
    if (magic != kMagic) {
        throw ParseError(0, "invalid KR16 magic");
    }
    const auto major = reader.read_u8();
    const auto minor = reader.read_u8();
    if (major != 1) {
        throw ParseError(4, "unsupported container major version");
    }

    const auto kind_value = reader.read_u16();
    if (kind_value != static_cast<std::uint16_t>(ContainerKind::Rel) &&
        kind_value != static_cast<std::uint16_t>(ContainerKind::Exec)) {
        throw ParseError(6, "invalid container kind");
    }
    const auto flags = reader.read_u32();
    const auto header_size = reader.read_u32();
    if (header_size != kHeaderSize) {
        throw ParseError(12, "invalid container header size");
    }
    const auto entry = reader.read_u16();
    require_zero(reader.read_u16(), 18, "header reserved field");
    const auto section_count = reader.read_u16();
    require_zero(reader.read_u16(), 22, "header reserved field");
    const auto symbol_count = reader.read_u32();
    const auto section_table_offset = reader.read_u32();
    const auto symbol_table_offset = reader.read_u32();
    const auto string_table_offset = reader.read_u32();
    const auto string_table_size = reader.read_u32();
    const auto reloc_table_offset = reader.read_u32();
    const auto reloc_table_size = reader.read_u32();
    const auto checksum = reader.read_u32();
    require_zero(reader.read_u32(), 56, "header reserved field");
    require_zero(reader.read_u32(), 60, "header reserved field");

    if (section_table_offset != kHeaderSize) {
        throw ParseError(28, "section table must follow the header");
    }
    const auto section_table_bytes =
        static_cast<std::uint64_t>(section_count) * kSectionHeaderSize;
    if (section_table_bytes > data.size() - kHeaderSize) {
        throw ParseError(section_table_offset, "section table out of bounds");
    }
    if (string_table_size > data.size() ||
        string_table_offset > data.size() - string_table_size) {
        throw ParseError(36, "string table out of bounds");
    }

    const auto string_table = StringTable::parse(
        data.subspan(string_table_offset, string_table_size));

    ByteReader section_reader(
        data.subspan(section_table_offset, section_table_bytes));
    std::vector<RawSection> raw_sections;
    raw_sections.reserve(section_count);
    for (std::uint16_t i = 0; i < section_count; ++i) {
        RawSection section;
        section.name_offset = section_reader.read_u32();
        section.type = section_reader.read_u16();
        section.flags = section_reader.read_u16();
        section.address = section_reader.read_u16();
        section.reserved = section_reader.read_u16();
        section.file_offset = section_reader.read_u32();
        section.size = section_reader.read_u32();
        section.alignment = section_reader.read_u16();
        section.link = section_reader.read_u16();
        section.info = section_reader.read_u16();
        section.entry_size = section_reader.read_u16();
        section.reserved2 = section_reader.read_u32();
        require_zero(section.reserved, section_table_offset + i * kSectionHeaderSize + 10,
                     "section reserved field");
        require_zero(section.reserved2, section_table_offset + i * kSectionHeaderSize + 28,
                     "section reserved field");
        if (section.type != static_cast<std::uint16_t>(SectionType::Progbits) &&
            section.type != static_cast<std::uint16_t>(SectionType::Nobits)) {
            throw ParseError(section_table_offset + i * kSectionHeaderSize + 4,
                             "invalid section type");
        }
        if (section.type == static_cast<std::uint16_t>(SectionType::Progbits)) {
            if (section.size > data.size() ||
                section.file_offset > data.size() - section.size) {
                throw ParseError(section_table_offset + i * kSectionHeaderSize + 12,
                                 "section data out of bounds");
            }
        }
        raw_sections.push_back(section);
    }

    Container container;
    container.major = major;
    container.minor = minor;
    container.kind = static_cast<ContainerKind>(kind_value);
    container.flags = flags;
    container.entry = entry;

    for (const auto& raw : raw_sections) {
        Section section;
        section.name = std::string(string_table.get(raw.name_offset));
        section.type = static_cast<SectionType>(raw.type);
        section.flags = raw.flags;
        section.address = raw.address;
        section.alignment = raw.alignment;
        section.link = raw.link;
        section.info = raw.info;
        section.entry_size = raw.entry_size;
        if (section.type == SectionType::Progbits) {
            const auto bytes = data.subspan(raw.file_offset, raw.size);
            section.data.assign(bytes.begin(), bytes.end());
        } else {
            section.nobits_size = raw.size;
        }
        container.sections.push_back(std::move(section));
    }

    if (symbol_table_offset > data.size()) {
        throw ParseError(32, "symbol table out of bounds");
    }
    const auto symbol_bytes = static_cast<std::uint64_t>(symbol_count) * kSymbolSize;
    if (symbol_bytes > data.size() - symbol_table_offset) {
        throw ParseError(32, "symbol table out of bounds");
    }
    ByteReader symbol_reader(data.subspan(symbol_table_offset, symbol_bytes));
    container.symbols.reserve(symbol_count);
    for (std::uint32_t i = 0; i < symbol_count; ++i) {
        Symbol symbol;
        symbol.name = std::string(string_table.get(symbol_reader.read_u32()));
        symbol.value = symbol_reader.read_u16();
        symbol.section = symbol_reader.read_u16();
        symbol.size = symbol_reader.read_u32();
        symbol.binding = static_cast<SymbolBinding>(symbol_reader.read_u8());
        symbol.type = static_cast<SymbolType>(symbol_reader.read_u8());
        symbol.visibility = static_cast<SymbolVisibility>(symbol_reader.read_u8());
        require_zero(symbol_reader.read_u8(), symbol_table_offset + i * kSymbolSize + 15,
                     "symbol reserved field");
        symbol.object_index = symbol_reader.read_u16();
        require_zero(symbol_reader.read_u16(), symbol_table_offset + i * kSymbolSize + 18,
                     "symbol reserved field");
        if (symbol.section > section_count) {
            throw ParseError(symbol_table_offset + i * kSymbolSize + 6,
                             "symbol section index out of range");
        }
        if (static_cast<std::uint8_t>(symbol.binding) > 4 ||
            static_cast<std::uint8_t>(symbol.type) > 4 ||
            static_cast<std::uint8_t>(symbol.visibility) > 2) {
            throw ParseError(symbol_table_offset + i * kSymbolSize + 12,
                             "invalid symbol metadata");
        }
        container.symbols.push_back(std::move(symbol));
    }

    if (container.kind == ContainerKind::Exec && reloc_table_size != 0) {
        throw ParseError(44, "EXEC container must not contain relocations");
    }
    if (reloc_table_size % kRelocationSize != 0) {
        throw ParseError(48, "relocation table size is not a multiple of 16");
    }
    if (reloc_table_offset > data.size() ||
        reloc_table_size > data.size() - reloc_table_offset) {
        throw ParseError(44, "relocation table out of bounds");
    }
    ByteReader reloc_reader(
        data.subspan(reloc_table_offset, reloc_table_size));
    const auto reloc_count = reloc_table_size / kRelocationSize;
    container.relocations.reserve(reloc_count);
    for (std::size_t i = 0; i < reloc_count; ++i) {
        Relocation relocation;
        relocation.section = reloc_reader.read_u16();
        relocation.type = static_cast<RelocationType>(reloc_reader.read_u16());
        relocation.offset = reloc_reader.read_u32();
        relocation.symbol = reloc_reader.read_u32();
        relocation.addend = reloc_reader.read_i32();
        if (relocation.section == 0 || relocation.section > section_count) {
            throw ParseError(reloc_table_offset + i * kRelocationSize,
                             "relocation section index out of range");
        }
        if (relocation.symbol >= symbol_count) {
            throw ParseError(reloc_table_offset + i * kRelocationSize + 8,
                             "relocation symbol index out of range");
        }
        container.relocations.push_back(relocation);
    }

    std::vector<std::uint8_t> crc_data(data.begin(), data.end());
    crc_data[kChecksumOffset] = 0;
    crc_data[kChecksumOffset + 1] = 0;
    crc_data[kChecksumOffset + 2] = 0;
    crc_data[kChecksumOffset + 3] = 0;
    if (crc32(crc_data) != checksum) {
        throw ParseError(kChecksumOffset, "CRC32 mismatch");
    }

    const Section* debug_str = container.find_section(".debug_str");
    if (debug_str == nullptr) {
        throw ParseError(36, "missing .debug_str section");
    }
    if (debug_str->data.size() != string_table_size) {
        throw ParseError(36, ".debug_str size does not match the header");
    }

    return container;
}

const Section* Container::find_section(std::string_view name) const {
    const auto it = std::find_if(sections.begin(), sections.end(),
                                 [name](const Section& section) {
                                     return section.name == name;
                                 });
    return it == sections.end() ? nullptr : &*it;
}

Section* Container::find_section(std::string_view name) {
    const auto it = std::find_if(sections.begin(), sections.end(),
                                 [name](const Section& section) {
                                     return section.name == name;
                                 });
    return it == sections.end() ? nullptr : &*it;
}

std::string Container::dump() const {
    std::ostringstream out;
    out << "KR16 container v" << static_cast<int>(major) << "."
        << static_cast<int>(minor) << " "
        << (kind == ContainerKind::Rel ? "REL" : "EXEC") << "\n";
    out << "entry=0x" << std::hex << entry << std::dec
        << " sections=" << sections.size() << " symbols=" << symbols.size()
        << " relocations=" << relocations.size() << "\n";
    for (const auto& section : sections) {
        out << "  section " << section.name << " addr=0x" << std::hex
            << section.address << std::dec << " size=" << section.byte_size()
            << (section.type == SectionType::Nobits ? " NOBITS" : "")
            << "\n";
    }
    for (const auto& symbol : symbols) {
        out << "  symbol " << symbol.name << " value=0x" << std::hex
            << symbol.value << std::dec << " section=" << symbol.section
            << " size=" << symbol.size << "\n";
    }
    for (const auto& relocation : relocations) {
        out << "  relocation section=" << relocation.section << " offset=0x"
            << std::hex << relocation.offset << std::dec
            << " symbol=" << relocation.symbol << "\n";
    }
    return out.str();
}

}  // namespace kr16


