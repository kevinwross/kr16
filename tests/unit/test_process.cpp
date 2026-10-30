#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/command_line.hpp"
#include "kr16/cpu.hpp"
#include "kr16/process.hpp"

namespace {

std::string read_string(kr16::FlatMemory& memory, std::uint16_t address) {
    std::string value;
    for (std::uint32_t i = 0; i < 0x1000; ++i) {
        const char ch = static_cast<char>(
            memory.read8(static_cast<std::uint16_t>(address + i)));
        if (ch == '\0') {
            break;
        }
        value.push_back(ch);
    }
    return value;
}

}  // namespace

TEST_CASE("command line parsing preserves Windows paths") {
    const auto plain = kr16::parse_command_line(
        "one C:\\Users\\kevin\\file.txt \"C:\\Program Files\\KR16\"");
    REQUIRE(plain.ok);
    REQUIRE(plain.arguments.size() == 3);
    CHECK(plain.arguments[0] == "one");
    CHECK(plain.arguments[1] == "C:\\Users\\kevin\\file.txt");
    CHECK(plain.arguments[2] == "C:\\Program Files\\KR16");
}

TEST_CASE("command line parsing supports empty and escaped quotes") {
    const auto empty = kr16::parse_command_line("\"\" \\\"quoted\\\"");
    REQUIRE(empty.ok);
    REQUIRE(empty.arguments.size() == 2);
    CHECK(empty.arguments[0].empty());
    CHECK(empty.arguments[1] == "\"quoted\"");
}

TEST_CASE("command line parsing rejects unterminated quotes") {
    const auto result = kr16::parse_command_line("\"unfinished");
    CHECK(!result.ok);
    CHECK(result.error.find("unterminated") != std::string::npos);
}

TEST_CASE("process stack exposes argc and argv strings") {
    kr16::FlatMemory memory;
    kr16::ProcessStackLayout layout;
    std::string error;
    REQUIRE(kr16::initialize_process_stack(
        memory, "bigtest.kex", {"one", "two words"}, 0xF000, 0xE000, layout,
        error));
    CHECK(layout.argc == 3);
    CHECK(layout.argv == layout.sp);
    CHECK(layout.sp < 0xF000);
    const auto program =
        memory.read16(layout.argv);
    CHECK(read_string(memory, program) == "bigtest.kex");
    const auto first =
        memory.read16(static_cast<std::uint16_t>(layout.argv + 2));
    const auto second =
        memory.read16(static_cast<std::uint16_t>(layout.argv + 4));
    CHECK(read_string(memory, first) == "one");
    CHECK(read_string(memory, second) == "two words");
    CHECK(memory.read16(static_cast<std::uint16_t>(layout.argv + 6)) == 0);
}

TEST_CASE("process stack rejects oversized argument blocks") {
    kr16::FlatMemory memory;
    kr16::ProcessStackLayout layout;
    std::string error;
    std::vector<std::string> too_many(32, "x");
    CHECK(!kr16::initialize_process_stack(
        memory, "program", too_many, 0xF000, 0xE000, layout, error));
    CHECK(error.find("too many") != std::string::npos);

    error.clear();
    const std::string long_argument(1024, 'x');
    CHECK(!kr16::initialize_process_stack(
        memory, "program", {long_argument}, 0xF000, 0xE000, layout, error));
    CHECK(error.find("too long") != std::string::npos);

    error.clear();
    CHECK(!kr16::initialize_process_stack(
        memory, "program", {"x"}, 0xE010, 0xE000, layout, error));
    CHECK(error.find("fit") != std::string::npos);
}
