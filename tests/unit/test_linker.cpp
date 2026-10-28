#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"
#include "kr16/linker.hpp"

namespace {

kr16::Container assemble(const std::string& source) {
    const auto result = kr16::assemble_text(source, kr16::AsmOptions{});
    REQUIRE(result.ok);
    return result.object;
}

}  // namespace

TEST_CASE("linker merges sections and resolves symbols") {
    const auto a = assemble(
        ".text\n"
        ".global foo\n"
        "foo:\n"
        "  MOV R0, #1\n"
        "  RET\n");
    const auto b = assemble(
        ".text\n"
        ".extern foo\n"
        ".global _start\n"
        "_start:\n"
        "  CALL foo\n"
        "  HLT\n"
        "  .word foo\n");
    const auto linked = kr16::link_objects({a, b});
    REQUIRE(linked.ok);
    const auto& image = linked.image;
    CHECK(image.kind == kr16::ContainerKind::Exec);
    CHECK(image.entry == 0x0104);
    const auto* text = image.find_section(".text");
    REQUIRE(text != nullptr);
    CHECK(text->address == 0x0100);
    REQUIRE(text->data.size() == 12);
    CHECK(text->data[0] == 0x81);
    CHECK(text->data[1] == 0x11);
    CHECK(text->data[4] == 0x00);  // CALL low byte
    CHECK(text->data[5] == 0x81);
    // CALL foo at 0x0104: word2 at 0x0106, target -4 words.
    CHECK(text->data[6] == 0xFC);
    CHECK(text->data[7] == 0xFF);
    CHECK(text->data[10] == 0x00);
    CHECK(text->data[11] == 0x01);
    CHECK(image.relocations.empty());
}

TEST_CASE("linker rejects duplicate globals") {
    const auto a = assemble(
        ".text\n.global same\nsame:\n  NOP\n");
    const auto b = assemble(
        ".text\n.global same\nsame:\n  NOP\n");
    const auto linked = kr16::link_objects({a, b});
    CHECK_FALSE(linked.ok);
    REQUIRE(linked.diagnostics.size() == 1);
    CHECK(linked.diagnostics[0].find("duplicate global") != std::string::npos);
}

TEST_CASE("linker strip options remove debug and symbols") {
    const auto object = assemble(
        ".file 0 \"x.c\"\n"
        ".text\n"
        ".loc 0 1 1\n"
        "  NOP\n");
    kr16::LinkOptions options;
    options.strip_debug = true;
    const auto debug_stripped = kr16::link_objects({object}, options);
    REQUIRE(debug_stripped.ok);
    CHECK(debug_stripped.image.find_section(".debug_line") == nullptr);

    options.strip_symbols = true;
    const auto fully_stripped = kr16::link_objects({object}, options);
    REQUIRE(fully_stripped.ok);
    for (const auto& symbol : fully_stripped.image.symbols) {
        CHECK(symbol.name.rfind("__", 0) == 0);
    }
}
