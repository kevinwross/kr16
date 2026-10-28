#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/container.hpp"
#include "kr16/crc32.hpp"

namespace {

kr16::Container make_exec() {
    kr16::Container container;
    container.kind = kr16::ContainerKind::Exec;
    container.entry = 0x0100;

    kr16::Section text;
    text.name = ".text";
    text.flags = kr16::SectionAlloc | kr16::SectionExec;
    text.address = 0x0100;
    text.data = {0x01, 0x02, 0x03, 0x04};
    container.sections.push_back(text);

    kr16::Section bss;
    bss.name = ".bss";
    bss.type = kr16::SectionType::Nobits;
    bss.flags = kr16::SectionAlloc | kr16::SectionWrite;
    bss.address = 0x0200;
    bss.nobits_size = 8;
    container.sections.push_back(bss);

    kr16::Section debug;
    debug.name = ".debug_line";
    debug.flags = kr16::SectionDebug;
    debug.data = {0xAA, 0xBB};
    container.sections.push_back(debug);

    kr16::Symbol main_symbol;
    main_symbol.name = "main";
    main_symbol.value = 0x0100;
    main_symbol.section = 1;
    main_symbol.size = 4;
    main_symbol.binding = kr16::SymbolBinding::Global;
    main_symbol.type = kr16::SymbolType::Function;
    container.symbols.push_back(main_symbol);
    return container;
}

kr16::Container make_rel() {
    kr16::Container container;
    container.kind = kr16::ContainerKind::Rel;

    kr16::Section text;
    text.name = ".text";
    text.flags = kr16::SectionAlloc | kr16::SectionExec;
    text.data = {0x00, 0x00};
    container.sections.push_back(text);

    kr16::Symbol target;
    target.name = "target";
    target.binding = kr16::SymbolBinding::Global;
    target.type = kr16::SymbolType::Function;
    container.symbols.push_back(target);

    kr16::Relocation relocation;
    relocation.section = 1;
    relocation.type = kr16::RelocationType::Abs16;
    relocation.offset = 0;
    relocation.symbol = 0;
    relocation.addend = 4;
    container.relocations.push_back(relocation);
    return container;
}

}  // namespace

TEST_CASE("CRC32 uses the IEEE polynomial") {
    const std::array<std::uint8_t, 9> data = {'1', '2', '3', '4',
                                              '5', '6', '7', '8', '9'};
    CHECK(kr16::crc32(data) == 0xCBF43926u);
}

TEST_CASE("string table deduplicates in first-use order") {
    kr16::StringTable table;
    const auto first = table.add("alpha");
    const auto second = table.add("beta");
    const auto alpha_again = table.add("alpha");
    CHECK(first == 0);
    CHECK(second == 6);
    CHECK(alpha_again == first);
    CHECK(table.get(first) == "alpha");
    CHECK(table.get(second) == "beta");

    const auto bytes = table.serialize();
    const auto parsed = kr16::StringTable::parse(bytes);
    CHECK(parsed.size() == 2);
    CHECK(parsed.get(first) == "alpha");
    CHECK(parsed.get(second) == "beta");
}

TEST_CASE("EXEC container round-trips") {
    const auto original = make_exec();
    const auto bytes = original.write();
    REQUIRE(bytes.size() > 64);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), 4) == "KR16");

    const auto decoded = kr16::Container::read(bytes);
    CHECK(decoded.kind == kr16::ContainerKind::Exec);
    CHECK(decoded.entry == 0x0100);
    CHECK(decoded.sections.size() == 4);  // .text, .bss, .debug_line, .debug_str
    CHECK(decoded.symbols.size() == 1);
    CHECK(decoded.relocations.empty());
    REQUIRE(decoded.find_section(".text") != nullptr);
    CHECK(decoded.find_section(".text")->data == original.sections[0].data);
    CHECK(decoded.find_section(".bss")->nobits_size == 8);
    CHECK(decoded.find_section(".debug_str") != nullptr);
    CHECK(decoded.symbols[0].name == "main");
    CHECK(decoded.symbols[0].value == 0x0100);
    CHECK(decoded.dump().find("KR16 container") != std::string::npos);
}

TEST_CASE("REL container round-trips relocations") {
    const auto original = make_rel();
    const auto bytes = original.write();
    const auto decoded = kr16::Container::read(bytes);
    CHECK(decoded.kind == kr16::ContainerKind::Rel);
    REQUIRE(decoded.relocations.size() == 1);
    CHECK(decoded.relocations[0].section == 1);
    CHECK(decoded.relocations[0].type == kr16::RelocationType::Abs16);
    CHECK(decoded.relocations[0].symbol == 0);
    CHECK(decoded.relocations[0].addend == 4);
}

TEST_CASE("container output is deterministic") {
    const auto container = make_exec();
    CHECK(container.write() == container.write());
}

TEST_CASE("container rejects corruption and bad headers") {
    const auto bytes = make_exec().write();

    auto bad_crc = bytes;
    bad_crc[52] ^= 0xFF;
    CHECK_THROWS_AS(kr16::Container::read(bad_crc), kr16::ParseError);

    auto bad_magic = bytes;
    bad_magic[0] = 'X';
    CHECK_THROWS_AS(kr16::Container::read(bad_magic), kr16::ParseError);

    auto bad_version = bytes;
    bad_version[4] = 2;
    CHECK_THROWS_AS(kr16::Container::read(bad_version), kr16::ParseError);

    auto truncated = bytes;
    truncated.resize(32);
    CHECK_THROWS_AS(kr16::Container::read(truncated), kr16::ParseError);
}

TEST_CASE("EXEC container refuses relocations") {
    auto container = make_exec();
    container.relocations.push_back(kr16::Relocation{});
    CHECK_THROWS_AS(container.write(), kr16::ParseError);
}

TEST_CASE("random bytes never crash the reader") {
    std::mt19937 rng(0x4B523136u);
    std::uniform_int_distribution<int> size_dist(0, 256);
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (int i = 0; i < 200; ++i) {
        std::vector<std::uint8_t> data(static_cast<std::size_t>(size_dist(rng)));
        for (auto& byte : data) {
            byte = static_cast<std::uint8_t>(byte_dist(rng));
        }
        try {
            (void)kr16::Container::read(data);
        } catch (const kr16::ParseError&) {
            // Expected for malformed input.
        } catch (const std::exception& error) {
            FAIL("unexpected exception: " << error.what());
        }
    }
}
