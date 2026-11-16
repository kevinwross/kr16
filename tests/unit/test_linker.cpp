#include <algorithm>
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

TEST_CASE("linker gc-all drops unreachable objects") {
    const auto unused = assemble(
        ".text\n"
        ".global unused\n"
        "unused:\n"
        "  NOP\n"
        "  RET\n");
    const auto start = assemble(
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "  HLT\n");
    kr16::LinkOptions options;
    options.gc_all = true;
    const auto linked = kr16::link_objects({unused, start}, options);
    REQUIRE(linked.ok);
    const auto* text = linked.image.find_section(".text");
    REQUIRE(text != nullptr);
    CHECK(text->data.size() == 2);
    for (const auto& symbol : linked.image.symbols) {
        CHECK(symbol.name != "unused");
    }
    bool dropped = false;
    for (const auto& symbol : linked.gc_dropped_symbols) {
        if (symbol == "unused") {
            dropped = true;
        }
    }
    CHECK(dropped);
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

TEST_CASE("linker resolves its provided heap and stack symbols") {
    const auto object = assemble(
        ".data\n"
        ".extern __heap_start\n"
        ".extern __stack_limit\n"
        ".global heap_word\n"
        "heap_word:\n"
        "  .word __heap_start\n"
        "  .word __stack_limit\n"
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "  HLT\n");
    const auto linked = kr16::link_objects({object});
    REQUIRE(linked.ok);
    const auto* data = linked.image.find_section(".data");
    REQUIRE(data != nullptr);
    REQUIRE(data->data.size() == 4);
    const auto word = [&](std::size_t offset) {
        return static_cast<std::uint16_t>(
            data->data[offset] |
            (static_cast<std::uint16_t>(data->data[offset + 1]) << 8));
    };
    const auto heap = std::find_if(
        linked.image.symbols.begin(), linked.image.symbols.end(),
        [](const kr16::Symbol& symbol) {
            return symbol.name == "__heap_start";
        });
    REQUIRE(heap != linked.image.symbols.end());
    CHECK(word(0) == heap->value);
    CHECK(word(0) != 0);
    CHECK(word(2) == 0xF000 - kr16::LinkOptions{}.stack_reserve);
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



TEST_CASE("relocatable link merges objects and preserves relocations") {
    const auto a = assemble(
        ".extern bar\n"
        ".text\n"
        ".global foo\n"
        "foo:\n"
        "  CALL bar\n"
        "  RET\n");
    const auto b = assemble(
        ".text\n"
        ".global bar\n"
        "bar:\n"
        "  RET\n");
    kr16::LinkOptions options;
    options.relocatable = true;
    const auto partial = kr16::link_objects({a, b}, options);
    REQUIRE(partial.ok);
    CHECK(partial.image.kind == kr16::ContainerKind::Rel);
    const auto* text = partial.image.find_section(".text");
    REQUIRE(text != nullptr);
    CHECK(text->data.size() == 8);
    CHECK_FALSE(partial.image.relocations.empty());
    bool foo = false;
    bool bar = false;
    for (const auto& symbol : partial.image.symbols) {
        foo = foo || symbol.name == "foo";
        bar = bar || symbol.name == "bar";
    }
    CHECK(foo);
    CHECK(bar);

    const auto start = assemble(
        ".extern foo\n"
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "  CALL foo\n"
        "  HLT\n");
    const auto linked = kr16::link_objects({partial.image, start});
    CHECK(linked.ok);
}

