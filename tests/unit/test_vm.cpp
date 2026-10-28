#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/assembler.hpp"
#include "kr16/vm.hpp"

namespace {

std::string write_raw(const std::string& source) {
    static int counter = 0;
    kr16::AsmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto assembled = kr16::assemble_text(source, options);
    REQUIRE(assembled.ok);
    const auto path = std::filesystem::current_path() /
                      ("kr16_vm_test_" + std::to_string(counter++) + ".bin");
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(assembled.raw.data()),
                 static_cast<std::streamsize>(assembled.raw.size()));
    output.close();
    return path.string();
}

}  // namespace

TEST_CASE("VM runs a raw program and returns HLT value") {
    const auto path = write_raw(
        ".text\n"
        "  MOV R0, #7\n"
        "  HLT\n");
    kr16::VmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto result = kr16::run_vm(path, options);
    std::filesystem::remove(path);
    INFO(result.error);
    REQUIRE(result.ok);
    CHECK(result.exit_code == 7);
    CHECK(result.instructions == 3);  // embedded BIOS JMP, MOV, HLT
    CHECK(result.cycles > 0);
}

TEST_CASE("VM captures console output") {
    const auto path = write_raw(
        ".text\n"
        "  MOV R1, #0xF000\n"
        "  MOV R0, #72\n"
        "  STB [R1], R0\n"
        "  HLT\n");
    kr16::VmOptions options;
    options.raw = true;
    options.base = 0x0100;
    const auto result = kr16::run_vm(path, options);
    std::filesystem::remove(path);
    REQUIRE(result.ok);
    REQUIRE(result.output.size() == 1);
    CHECK(result.output[0] == 'H');
}

TEST_CASE("VM builds argc/argv and sets R1/R2") {
    const auto path = write_raw(
        ".text\n"
        "  MOV R0, R1\n"
        "  HLT\n");
    kr16::VmOptions options;
    options.raw = true;
    options.base = 0x0100;
    options.program_args = {"x"};
    const auto result = kr16::run_vm(path, options);
    std::filesystem::remove(path);
    REQUIRE(result.ok);
    CHECK(result.exit_code == 2);
}

TEST_CASE("VM enforces max cycles") {
    const auto path = write_raw(
        ".text\n"
        "loop:\n"
        "  BRA loop\n");
    kr16::VmOptions options;
    options.raw = true;
    options.base = 0x0100;
    options.max_cycles = 10;
    const auto result = kr16::run_vm(path, options);
    std::filesystem::remove(path);
    REQUIRE(result.ok);
    CHECK(result.timed_out);
    CHECK(result.exit_code == 124);
    CHECK(result.cycles >= 10);
}
