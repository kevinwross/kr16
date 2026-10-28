#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"

TEST_CASE("assemble a small raw program") {
    const std::string source =
        ".text\n"
        "start:\n"
        "  MOV R0, #1\n"
        "  ADD R0, #2\n"
        "  HLT\n";
    kr16::AsmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto result = kr16::assemble_text(source, options);
    REQUIRE(result.ok);
    REQUIRE(result.raw.size() == 6);
    CHECK(result.raw[0] == 0x81);
    CHECK(result.raw[1] == 0x11);
    CHECK(result.raw[2] == 0x02);
    CHECK(result.raw[3] == 0x10);
    CHECK(result.raw[4] == 0x00);
    CHECK(result.raw[5] == 0x88);
}

TEST_CASE("long conditional branches are relaxed") {
    std::string source =
        ".text\n"
        "start:\n"
        "  MOV R0, #1\n"
        "  CMP R0, #1\n"
        "  BEQ far\n"
        "  MOV R0, #2\n";
    for (int i = 0; i < 200; ++i) {
        source += "  NOP\n";
    }
    source += "far:\n  HLT\n";

    kr16::AsmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto result = kr16::assemble_text(source, options);
    REQUIRE(result.ok);
    REQUIRE(result.raw.size() == 414);
    // BNE +4 bytes skips the following JMP when BEQ is false.
    CHECK(result.raw[4] == 0x02);
    CHECK(result.raw[5] == 0x71);
}

TEST_CASE("long unconditional branches are relaxed") {
    std::string source = ".text\nstart:\n";
    for (int i = 0; i < 200; ++i) {
        source += "  NOP\n";
    }
    source += "  BRA start\n";
    kr16::AsmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto result = kr16::assemble_text(source, options);
    REQUIRE(result.ok);
    CHECK(result.raw.size() == 404);
}

TEST_CASE("branches resolve local labels") {
    const std::string source =
        ".text\n"
        "start:\n"
        "  MOV R0, #1\n"
        "  BEQ start\n";
    kr16::AsmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto result = kr16::assemble_text(source, options);
    REQUIRE(result.ok);
    REQUIRE(result.raw.size() == 4);
    CHECK(result.raw[0] == 0x81);
    CHECK(result.raw[1] == 0x11);
    CHECK(result.raw[2] == 0xFE);  // disp8 = -2 words = -4 bytes
    CHECK(result.raw[3] == 0x70);
}

TEST_CASE("object symbols and data directives") {
    const std::string source =
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "  MOV R0, #1\n"
        ".word _start\n";
    kr16::AsmOptions options;
    const auto result = kr16::assemble_text(source, options);
    REQUIRE(result.ok);
    CHECK(result.object.kind == kr16::ContainerKind::Rel);
    REQUIRE(result.object.sections.size() >= 1);
    REQUIRE(result.object.symbols.size() == 1);
    CHECK(result.object.symbols[0].name == "_start");
    CHECK(result.object.symbols[0].binding == kr16::SymbolBinding::Global);
    CHECK(result.object.symbols[0].section == 1);
    CHECK(result.object.find_section(".text")->data.size() == 4);
}

TEST_CASE("external references create relocations") {
    const std::string source =
        ".text\n"
        ".extern foo\n"
        "  MOV R0, #foo\n"
        "  CALL foo\n"
        "  .word foo\n";
    const auto result = kr16::assemble_text(source, kr16::AsmOptions{});
    REQUIRE(result.ok);
    REQUIRE(result.object.symbols.size() == 1);
    CHECK(result.object.symbols[0].name == "foo");
    CHECK(result.object.symbols[0].binding == kr16::SymbolBinding::Extern);
    REQUIRE(result.object.relocations.size() == 3);
    CHECK(result.object.relocations[0].type == kr16::RelocationType::Abs16);
    CHECK(result.object.relocations[1].type == kr16::RelocationType::Pcrel16w);
    CHECK(result.object.relocations[2].type == kr16::RelocationType::Abs16);
}

TEST_CASE("equ, alignment, and strings") {
    const std::string source =
        ".text\n"
        ".equ ANSWER, 42\n"
        ".byte ANSWER\n"
        ".align 4\n"
        ".asciz \"hi\"\n";
    const auto result = kr16::assemble_text(source, kr16::AsmOptions{});
    REQUIRE(result.ok);
    const auto* text = result.object.find_section(".text");
    REQUIRE(text != nullptr);
    REQUIRE(text->data.size() == 7);
    CHECK(text->data[0] == 42);
    CHECK(text->data[4] == 'h');
    CHECK(text->data[5] == 'i');
    CHECK(text->data[6] == 0);
}

TEST_CASE("assembler reports syntax errors") {
    const auto result = kr16::assemble_text(".text\n  NOPE R0, R1\n",
                                            kr16::AsmOptions{});
    CHECK_FALSE(result.ok);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].line == 2);
}

TEST_CASE("macros and conditionals expand") {
    const std::string source =
        ".macro load reg, value\n"
        "  MOV \\reg, #\\value\n"
        ".endm\n"
        ".text\n"
        ".if 1\n"
        "  load R0, 5\n"
        ".else\n"
        "  HLT\n"
        ".endif\n";
    const auto result = kr16::assemble_text(source, kr16::AsmOptions{});
    REQUIRE(result.ok);
    const auto* text = result.object.find_section(".text");
    REQUIRE(text != nullptr);
    REQUIRE(text->data.size() == 2);
    CHECK(text->data[0] == 0x85);
    CHECK(text->data[1] == 0x11);
}

TEST_CASE("assembler emits debug line rows") {
    const std::string source =
        ".file 0 \"hello.c\"\n"
        ".text\n"
        ".loc 0 10 2\n"
        "  MOV R0, #1\n"
        ".loc 0 11 3\n"
        "  HLT\n";
    const auto result = kr16::assemble_text(source, kr16::AsmOptions{});
    if (!result.ok && !result.diagnostics.empty()) {
        INFO(result.diagnostics[0].message);
    }
    REQUIRE(result.ok);
    const auto* line = result.object.find_section(".debug_line");
    REQUIRE(line != nullptr);
    REQUIRE(line->data.size() == 24);
    CHECK(line->data[0] == 2);
    CHECK(line->data[4] == 1);
    CHECK(line->data[8] == 0);   // first row address low
    CHECK(line->data[9] == 0);
    CHECK(line->data[10] == 0);  // file
    CHECK(line->data[11] == 0);
    CHECK(line->data[12] == 10);  // line
    CHECK(line->data[13] == 0);
    CHECK(line->data[14] == 2);   // column
    CHECK(line->data[16] == 2);   // second row address low
    CHECK(line->data[20] == 11);  // second line
    CHECK(line->data[22] == 3);   // second column
}

