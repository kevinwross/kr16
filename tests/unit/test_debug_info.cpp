#include <string>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"
#include "kr16/debug_info.hpp"
#include "kr16/linker.hpp"

TEST_CASE("debug line rows survive linking") {
    const std::string source =
        ".file 0 \"main.c\"\n"
        ".text\n"
        ".loc 0 4 5\n"
        "  MOV R0, #1\n"
        ".loc 0 5 6\n"
        "  HLT\n";
    const auto assembled = kr16::assemble_text(source, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const auto linked = kr16::link_objects({assembled.object});
    REQUIRE(linked.ok);
    const auto* line = linked.image.find_section(".debug_line");
    REQUIRE(line != nullptr);
    const auto rows = kr16::parse_debug_line(line->data);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].address == 0x0100);
    CHECK(rows[0].file == 0);
    CHECK(rows[0].line == 4);
    CHECK(rows[0].column == 5);
    CHECK(rows[1].address == 0x0102);
    CHECK(rows[1].line == 5);
    const auto* sources = linked.image.find_section(".debug_sources");
    REQUIRE(sources != nullptr);
    const std::string source_data(sources->data.begin(), sources->data.end());
    CHECK(source_data == std::string("main.c\0", 7));
}

TEST_CASE("debug dump renders line information") {
    const std::string source =
        ".file 0 \"main.c\"\n"
        ".text\n"
        ".loc 0 7 2\n"
        "  NOP\n";
    const auto assembled = kr16::assemble_text(source, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    kr16::DebugOptions options;
    options.lines = true;
    const auto text = kr16::dump_debug_info(assembled.object, options);
    CHECK(text.find("0x0000") != std::string::npos);
    CHECK(text.find("file 0:7:2") != std::string::npos);
}


TEST_CASE("debug line rows account for object placement") {
    const auto prefix = kr16::assemble_text(
        ".text\n  NOP\n  NOP\n", kr16::AsmOptions{});
    REQUIRE(prefix.ok);
    const auto debug = kr16::assemble_text(
        ".file 0 \"main.c\"\n.text\n.loc 0 2 0\n  NOP\n",
        kr16::AsmOptions{});
    REQUIRE(debug.ok);
    const auto linked = kr16::link_objects({prefix.object, debug.object});
    REQUIRE(linked.ok);
    const auto* line = linked.image.find_section(".debug_line");
    REQUIRE(line != nullptr);
    const auto rows = kr16::parse_debug_line(line->data);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].address == 0x0104);
}
