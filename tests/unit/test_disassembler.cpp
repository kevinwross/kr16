#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"
#include "kr16/disassembler.hpp"

TEST_CASE("disassemble a small raw program") {
    const std::string source =
        ".text\n"
        "start:\n"
        "  MOV R0, #1\n"
        "  ADD R0, #2\n"
        "  BNE start\n"
        "  HLT\n";
    kr16::AsmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto assembled = kr16::assemble_text(source, options);
    REQUIRE(assembled.ok);
    kr16::DisassemblyOptions disassembly;
    disassembly.base = 0x0100;
    const std::string text =
        kr16::disassemble_raw(assembled.raw, disassembly);
    INFO(text);
    CHECK(text.find("MOV R0, #0x1") != std::string::npos);
    CHECK(text.find("ADD R0, #0x2") != std::string::npos);
    CHECK(text.find("BNE 0x0100") != std::string::npos);
    CHECK(text.find("HLT") != std::string::npos);
}

TEST_CASE("disassemble a container with labels") {
    const std::string source =
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "  MOV R0, #1\n"
        "loop:\n"
        "  BRA loop\n";
    const auto assembled = kr16::assemble_text(source, kr16::AsmOptions{});
    REQUIRE(assembled.ok);
    const std::string text = kr16::disassemble_container(assembled.object);
    CHECK(text.find(".text:") != std::string::npos);
    CHECK(text.find("_start:") != std::string::npos);
    CHECK(text.find("loop:") != std::string::npos);
    CHECK(text.find("BRA") != std::string::npos);
}
