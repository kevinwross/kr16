#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "kr16/diagnostic.hpp"

namespace kr16 {

enum class ContainerKind : std::uint16_t {
    Rel = 1,
    Exec = 2,
};

enum class SectionType : std::uint16_t {
    Progbits = 1,
    Nobits = 2,
};

enum SectionFlags : std::uint16_t {
    SectionAlloc = 1u << 0,
    SectionExec = 1u << 1,
    SectionWrite = 1u << 2,
    SectionDebug = 1u << 3,
};

enum ContainerFlags : std::uint32_t {
    ContainerHasSymbols = 1u << 0,
    ContainerHasDebug = 1u << 1,
    ContainerStripped = 1u << 2,
    ContainerHasObjects = 1u << 3,
};

enum class SymbolBinding : std::uint8_t {
    Local = 0,
    Global = 1,
    Extern = 2,
    Weak = 3,
    Common = 4,
};

enum class SymbolType : std::uint8_t {
    NoType = 0,
    Object = 1,
    Function = 2,
    Section = 3,
    File = 4,
};

enum class SymbolVisibility : std::uint8_t {
    Default = 0,
    Hidden = 1,
    Protected = 2,
};

enum class RelocationType : std::uint16_t {
    Abs16 = 1,
    Pcrel8w = 2,
    Pcrel16w = 3,
    Pcrel16b = 4,
};

class StringTable {
public:
    std::uint32_t add(std::string_view text);
    std::string_view get(std::uint32_t offset) const;
    std::vector<std::uint8_t> serialize() const;
    static StringTable parse(std::span<const std::uint8_t> data);
    std::size_t size() const noexcept { return by_offset_.size(); }

private:
    std::string bytes_;
    std::unordered_map<std::string, std::uint32_t> by_string_;
    std::unordered_map<std::uint32_t, std::string> by_offset_;
};

struct Section {
    std::string name;
    SectionType type = SectionType::Progbits;
    std::uint16_t flags = 0;
    std::uint16_t address = 0;
    std::uint16_t alignment = 2;
    std::uint16_t link = 0;
    std::uint16_t info = 0;
    std::uint16_t entry_size = 0;
    std::vector<std::uint8_t> data;
    std::uint32_t nobits_size = 0;

    std::uint32_t byte_size() const {
        return type == SectionType::Nobits
                   ? nobits_size
                   : static_cast<std::uint32_t>(data.size());
    }
    bool is_alloc() const { return (flags & SectionAlloc) != 0; }
    bool is_debug() const { return (flags & SectionDebug) != 0; }
};

struct Symbol {
    std::string name;
    std::uint16_t value = 0;
    std::uint16_t section = 0;
    std::uint32_t size = 0;
    SymbolBinding binding = SymbolBinding::Local;
    SymbolType type = SymbolType::NoType;
    SymbolVisibility visibility = SymbolVisibility::Default;
    std::uint16_t object_index = 0;
};

struct Relocation {
    std::uint16_t section = 0;
    RelocationType type = RelocationType::Abs16;
    std::uint32_t offset = 0;
    std::uint32_t symbol = 0;
    std::int32_t addend = 0;
};

struct Container {
    std::uint8_t major = 1;
    std::uint8_t minor = 0;
    ContainerKind kind = ContainerKind::Exec;
    std::uint32_t flags = 0;
    std::uint16_t entry = 0;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<Relocation> relocations;

    std::vector<std::uint8_t> write() const;
    static Container read(std::span<const std::uint8_t> data);
    std::string dump() const;

    const Section* find_section(std::string_view name) const;
    Section* find_section(std::string_view name);
};

}  // namespace kr16


