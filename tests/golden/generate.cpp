#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "kr16/container.hpp"

namespace {

kr16::Section section(std::string name, std::uint16_t flags,
                      std::uint16_t address,
                      std::vector<std::uint8_t> data) {
    kr16::Section result;
    result.name = std::move(name);
    result.flags = flags;
    result.address = address;
    result.data = std::move(data);
    return result;
}

void write_file(const std::string& path,
                const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main() {
    kr16::Container exec;
    exec.kind = kr16::ContainerKind::Exec;
    exec.entry = 0x0100;
    exec.sections.push_back(section(
        ".text", kr16::SectionAlloc | kr16::SectionExec, 0x0100,
        {0x01, 0x02, 0x03, 0x04}));
    kr16::Section bss;
    bss.name = ".bss";
    bss.type = kr16::SectionType::Nobits;
    bss.flags = kr16::SectionAlloc | kr16::SectionWrite;
    bss.address = 0x0200;
    bss.nobits_size = 8;
    exec.sections.push_back(bss);
    kr16::Symbol main_symbol;
    main_symbol.name = "main";
    main_symbol.value = 0x0100;
    main_symbol.section = 1;
    main_symbol.size = 4;
    main_symbol.binding = kr16::SymbolBinding::Global;
    main_symbol.type = kr16::SymbolType::Function;
    exec.symbols.push_back(main_symbol);
    write_file("tests/golden/exec_min.kr16", exec.write());

    kr16::Container rel;
    rel.kind = kr16::ContainerKind::Rel;
    rel.sections.push_back(section(
        ".text", kr16::SectionAlloc | kr16::SectionExec, 0,
        {0x00, 0x00}));
    kr16::Symbol target;
    target.name = "target";
    target.binding = kr16::SymbolBinding::Global;
    target.type = kr16::SymbolType::Function;
    rel.symbols.push_back(target);
    kr16::Relocation relocation;
    relocation.section = 1;
    relocation.type = kr16::RelocationType::Abs16;
    relocation.offset = 0;
    relocation.symbol = 0;
    relocation.addend = 4;
    rel.relocations.push_back(relocation);
    write_file("tests/golden/rel_min.o", rel.write());
    return 0;
}
