#include "kr16/linker.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

struct Placement {
    std::size_t object = 0;
    std::size_t section = 0;
    std::size_t output = 0;
    std::uint32_t output_offset = 0;
    std::uint16_t address = 0;
};

std::uint16_t read_u16(const std::vector<std::uint8_t>& data,
                       std::uint32_t offset) {
    return static_cast<std::uint16_t>(
        data[offset] | (static_cast<std::uint16_t>(data[offset + 1]) << 8));
}

void write_u8(Section& section, std::uint32_t offset, std::uint8_t value) {
    if (section.type == SectionType::Nobits ||
        offset >= section.data.size()) {
        throw std::runtime_error("relocation outside section data");
    }
    section.data[offset] = value;
}

void write_u16(Section& section, std::uint32_t offset, std::uint16_t value) {
    write_u8(section, offset, static_cast<std::uint8_t>(value & 0xFF));
    write_u8(section, offset + 1, static_cast<std::uint8_t>(value >> 8));
}

}  // namespace

LinkResult link_objects(const std::vector<Container>& objects,
                        const LinkOptions& options) {
    LinkResult result;
    try {
        if (objects.empty()) {
            throw std::runtime_error("no input objects");
        }

        std::vector<Section> outputs;
        std::unordered_map<std::string, std::size_t> output_index;
        std::vector<Placement> placements;
        std::vector<std::pair<std::size_t, std::size_t>> debug_line_sections;

        for (std::size_t object_index = 0; object_index < objects.size();
             ++object_index) {
            const auto& object = objects[object_index];
            for (std::size_t section_index = 0;
                 section_index < object.sections.size(); ++section_index) {
                const auto& input = object.sections[section_index];
                if (!options.strip_debug && input.name == ".debug_vars") {
                    Section* target = nullptr;
                    for (auto& output : outputs) {
                        if (output.name == ".debug_vars") {
                            target = &output;
                            break;
                        }
                    }
                    if (target == nullptr) {
                        Section output;
                        output.name = input.name;
                        output.flags = input.flags;
                        output.alignment = input.alignment;
                        outputs.push_back(std::move(output));
                        target = &outputs.back();
                    }
                    target->data.insert(target->data.end(), input.data.begin(),
                                        input.data.end());
                    continue;
                }
                if (!options.strip_debug && input.name == ".debug_line") {
                    Section output;
                    output.name = input.name;
                    output.flags = input.flags;
                    output.alignment = input.alignment;
                    output.data = input.data;
                    outputs.push_back(std::move(output));
                    debug_line_sections.emplace_back(object_index,
                                                     outputs.size() - 1);
                    continue;
                }
                if ((input.flags & SectionAlloc) == 0 ||
                    input.name.rfind(".debug", 0) == 0) {
                    continue;
                }
                const bool nobits = input.type == SectionType::Nobits;
                auto it = output_index.find(input.name);
                if (it == output_index.end()) {
                    Section output;
                    output.name = input.name;
                    output.flags = input.flags;
                    output.alignment = input.alignment;
                    output.type = nobits ? SectionType::Nobits
                                          : SectionType::Progbits;
                    outputs.push_back(std::move(output));
                    const auto index = outputs.size() - 1;
                    output_index.emplace(input.name, index);
                    it = output_index.find(input.name);
                }
                auto& output = outputs[it->second];
                if (nobits != (output.type == SectionType::Nobits)) {
                    throw std::runtime_error("inconsistent section type for " +
                                             input.name);
                }
                output.alignment =
                    std::max(output.alignment, input.alignment);
                Placement placement;
                placement.object = object_index;
                placement.section = section_index;
                placement.output = it->second;
                if (nobits) {
                    placement.output_offset =
                        align_up(output.nobits_size, input.alignment);
                    output.nobits_size = placement.output_offset +
                                         static_cast<std::uint32_t>(
                                             input.nobits_size);
                } else {
                    placement.output_offset = align_up(
                        static_cast<std::uint32_t>(output.data.size()),
                        input.alignment);
                    output.data.resize(placement.output_offset, 0);
                    output.data.insert(output.data.end(), input.data.begin(),
                                       input.data.end());
                }
                placements.push_back(placement);
            }
        }

        if (output_index.find(".text") == output_index.end()) {
            throw std::runtime_error("no .text section");
        }

        auto section_by_name = [&](const std::string& name) -> Section* {
            const auto it = output_index.find(name);
            return it == output_index.end() ? nullptr : &outputs[it->second];
        };
        auto align_cursor = [](std::uint32_t cursor, std::uint32_t alignment) {
            return align_up(cursor, alignment);
        };

        std::uint32_t cursor = 0;
        if (auto* vectors = section_by_name(".vectors")) {
            if (vectors->byte_size() > 0x40) {
                throw std::runtime_error(".vectors exceeds the first 64 bytes");
            }
            vectors->address = 0;
            cursor = vectors->byte_size();
        }
        if (auto* jump = section_by_name(".bios_jump")) {
            if (jump->byte_size() > 0xC0) {
                throw std::runtime_error(".bios_jump exceeds 192 bytes");
            }
            jump->address = 0x0040;
            cursor = std::max<std::uint32_t>(cursor, 0x0040 + jump->byte_size());
        }
        if (auto* crt0 = section_by_name(".crt0")) {
            const auto crt_address =
                std::max<std::uint32_t>(options.text_address, cursor);
            if (crt_address > 0xFFFF) {
                throw std::runtime_error(".crt0 does not fit in memory");
            }
            crt0->address = static_cast<std::uint16_t>(crt_address);
            cursor = crt_address + crt0->byte_size();
        }
        if (auto* text = section_by_name(".text")) {
            const auto text_address =
                std::max<std::uint32_t>(options.text_address, cursor);
            if (text_address > 0xFFFF) {
                throw std::runtime_error(".text does not fit in memory");
            }
            text->address = static_cast<std::uint16_t>(text_address);
            cursor = text_address + text->byte_size();
        }

        const std::vector<std::string> order = {".rodata", ".data", ".bss"};
        for (const auto& name : order) {
            auto* section = section_by_name(name);
            if (section == nullptr) {
                continue;
            }
            cursor = align_cursor(cursor, section->alignment);
            if (cursor > 0xFFFF) {
                throw std::runtime_error("image exceeds memory");
            }
            section->address = static_cast<std::uint16_t>(cursor);
            cursor += section->byte_size();
        }

        for (auto& section : outputs) {
            if (section.name.rfind(".debug", 0) == 0) {
                continue;
            }
            if (section.address != 0 || section.name == ".vectors") {
                continue;
            }
            if (section.name == ".bios_jump") {
                continue;
            }
            cursor = align_cursor(cursor, section.alignment);
            if (cursor > 0xFFFF) {
                throw std::runtime_error("image exceeds memory");
            }
            section.address = static_cast<std::uint16_t>(cursor);
            cursor += section.byte_size();
        }

        auto find_placement = [&placements](std::size_t object,
                                            std::size_t section) -> const Placement& {
            for (const auto& placement : placements) {
                if (placement.object == object && placement.section == section) {
                    return placement;
                }
            }
            throw std::runtime_error("missing section placement");
        };
        for (auto& placement : placements) {
            placement.address = static_cast<std::uint16_t>(
                outputs[placement.output].address + placement.output_offset);
        }

        for (const auto& [object_index, output_index_value] :
             debug_line_sections) {
            std::uint16_t text_address = 0;
            for (const auto& placement : placements) {
                if (placement.object == object_index &&
                    outputs[placement.output].name == ".text") {
                    text_address = outputs[placement.output].address;
                    break;
                }
            }
            auto& data = outputs[output_index_value].data;
            if (data.size() < 8) {
                continue;
            }
            const auto count = static_cast<std::uint32_t>(
                data[0] | (static_cast<std::uint32_t>(data[1]) << 8) |
                (static_cast<std::uint32_t>(data[2]) << 16) |
                (static_cast<std::uint32_t>(data[3]) << 24));
            if (8 + static_cast<std::uint64_t>(count) * 8 > data.size()) {
                throw std::runtime_error("malformed .debug_line section");
            }
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto offset = 8 + i * 8;
                const auto address = read_u16(data, offset);
                const auto adjusted =
                    static_cast<std::uint16_t>(address + text_address);
                data[offset] = static_cast<std::uint8_t>(adjusted & 0xFF);
                data[offset + 1] = static_cast<std::uint8_t>(adjusted >> 8);
            }
        }

        std::unordered_map<std::string, std::uint16_t> globals;
        for (std::size_t object_index = 0; object_index < objects.size();
             ++object_index) {
            for (const auto& symbol : objects[object_index].symbols) {
                if (symbol.section == 0 || symbol.binding == SymbolBinding::Local) {
                    continue;
                }
                if (symbol.binding == SymbolBinding::Extern) {
                    continue;
                }
                const auto& placement =
                    find_placement(object_index, symbol.section - 1);
                const auto value = static_cast<std::uint16_t>(
                    placement.address + symbol.value);
                const auto [it, inserted] = globals.emplace(symbol.name, value);
                if (!inserted) {
                    throw std::runtime_error("duplicate global symbol " +
                                             symbol.name);
                }
            }
        }

        auto symbol_value = [&](std::size_t object_index,
                                std::uint32_t symbol_index) -> std::uint16_t {
            if (symbol_index >= objects[object_index].symbols.size()) {
                throw std::runtime_error("bad symbol reference");
            }
            const auto& symbol = objects[object_index].symbols[symbol_index];
            if (symbol.section != 0) {
                const auto& placement =
                    find_placement(object_index, symbol.section - 1);
                return static_cast<std::uint16_t>(placement.address +
                                                  symbol.value);
            }
            const auto it = globals.find(symbol.name);
            if (it == globals.end()) {
                throw std::runtime_error("undefined symbol " + symbol.name);
            }
            return it->second;
        };

        for (std::size_t object_index = 0; object_index < objects.size();
             ++object_index) {
            const auto& object = objects[object_index];
            for (const auto& relocation : object.relocations) {
                if (relocation.section == 0) {
                    throw std::runtime_error("relocation without section");
                }
                const auto& placement =
                    find_placement(object_index, relocation.section - 1);
                auto& output = outputs[placement.output];
                const auto field = placement.output_offset + relocation.offset;
                const auto address = static_cast<std::uint16_t>(
                    placement.address + relocation.offset);
                const auto target =
                    symbol_value(object_index, relocation.symbol);
                switch (relocation.type) {
                    case RelocationType::Abs16: {
                        const auto value = static_cast<std::int32_t>(target) +
                                           relocation.addend;
                        if (value < 0 || value > 0xFFFF) {
                            throw std::runtime_error(
                                "ABS16 relocation out of range");
                        }
                        write_u16(output, field,
                                  static_cast<std::uint16_t>(value));
                        break;
                    }
                    case RelocationType::Pcrel16w: {
                        const auto delta = static_cast<std::int32_t>(target) +
                                           relocation.addend -
                                           (static_cast<std::int32_t>(address) + 2);
                        if ((delta % 2) != 0) {
                            throw std::runtime_error(
                                "PCREL16W relocation is not word-aligned");
                        }
                        const auto word = delta / 2;
                        if (word < -32768 || word > 32767) {
                            throw std::runtime_error(
                                "PCREL16W relocation out of range");
                        }
                        write_u16(output, field,
                                  static_cast<std::uint16_t>(word & 0xFFFF));
                        break;
                    }
                    case RelocationType::Pcrel8w: {
                        const auto delta = static_cast<std::int32_t>(target) +
                                           relocation.addend -
                                           (static_cast<std::int32_t>(address) + 1);
                        if (delta < -128 || delta > 127) {
                            throw std::runtime_error(
                                "PCREL8W relocation out of range");
                        }
                        write_u8(output, field,
                                 static_cast<std::uint8_t>(delta & 0xFF));
                        break;
                    }
                    case RelocationType::Pcrel16b: {
                        const auto delta = static_cast<std::int32_t>(target) +
                                           relocation.addend -
                                           (static_cast<std::int32_t>(address) + 1);
                        if (delta < -32768 || delta > 32767) {
                            throw std::runtime_error(
                                "PCREL16B relocation out of range");
                        }
                        write_u16(output, field,
                                  static_cast<std::uint16_t>(delta & 0xFFFF));
                        break;
                    }
                }
            }
        }

        Container image;
        image.kind = ContainerKind::Exec;
        image.sections = std::move(outputs);
        for (std::size_t object_index = 0; object_index < objects.size();
             ++object_index) {
            for (const auto& symbol : objects[object_index].symbols) {
                if (symbol.section == 0) {
                    continue;
                }
                const auto& placement =
                    find_placement(object_index, symbol.section - 1);
                Symbol output;
                output.name = symbol.name;
                output.value = static_cast<std::uint16_t>(placement.address +
                                                          symbol.value);
                output.section = static_cast<std::uint16_t>(placement.output + 1);
                output.size = symbol.size;
                output.binding = symbol.binding;
                output.type = symbol.type;
                image.symbols.push_back(std::move(output));
            }
        }

        auto add_absolute_symbol = [&](const std::string& name,
                                       std::uint16_t value) {
            if (globals.count(name) != 0) {
                return;
            }
            Symbol symbol;
            symbol.name = name;
            symbol.value = value;
            symbol.section = 0;
            symbol.binding = SymbolBinding::Global;
            symbol.type = SymbolType::NoType;
            image.symbols.push_back(std::move(symbol));
        };
        add_absolute_symbol("__heap_start",
                            static_cast<std::uint16_t>(align_up(cursor, 2)));
        add_absolute_symbol("__stack_top", 0xF000);
        add_absolute_symbol(
            "__stack_limit",
            static_cast<std::uint16_t>(0xF000 - options.stack_reserve));

        for (const auto& [name, value] : globals) {
            (void)value;
            if (name == "_start" || name == "__start" || name == "start") {
                image.entry = globals.at(name);
                break;
            }
        }
        if (image.entry == 0) {
            image.entry = options.text_address;
        }
        if (options.strip_symbols) {
            image.symbols.erase(
                std::remove_if(
                    image.symbols.begin(), image.symbols.end(),
                    [](const Symbol& symbol) {
                        return symbol.name != "__heap_start" &&
                               symbol.name != "__stack_top" &&
                               symbol.name != "__stack_limit";
                    }),
                image.symbols.end());
        }
        result.image = std::move(image);
        result.ok = true;
        return result;
    } catch (const std::exception& error) {
        result.diagnostics.push_back(error.what());
    }
    return result;
}

LinkResult link_files(const std::vector<std::string>& paths,
                      const LinkOptions& options) {
    LinkResult result;
    try {
        std::vector<Container> objects;
        for (const auto& path : paths) {
            objects.push_back(Container::read(read_file(path)));
        }
        return link_objects(objects, options);
    } catch (const std::exception& error) {
        result.diagnostics.push_back(error.what());
    }
    return result;
}

}  // namespace kr16
