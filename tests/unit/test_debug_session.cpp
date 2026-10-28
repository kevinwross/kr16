#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "kr16/build.hpp"
#include "kr16/debug_session.hpp"

namespace {

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(KR16_SOURCE_DIR) / "tests" / "golden" /
           name;
}

std::filesystem::path make_debug_fixture(std::string& error) {
    const auto directory =
        std::filesystem::current_path() / "kr16_session_debug";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    ec.clear();
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "create_directories: " + ec.message();
        return {};
    }

    {
        std::ofstream source(directory / "session_debug.c");
        if (!source.good()) {
            error = "cannot write session_debug.c";
            return {};
        }
        source << "int main(void) {\n"
                  "    int local;\n"
                  "    local = 7;\n"
                  "    return local;\n"
                  "}\n";
    }
    {
        std::ofstream assembly(directory / "session_debug.s");
        if (!assembly.good()) {
            error = "cannot write session_debug.s";
            return {};
        }
        assembly << ".file 0 \"session_debug.c\"\n"
                    ".text\n"
                    ".global _start\n"
                    "_start:\n"
                    "  PUSH R4\n"
                    "  MOV R4, SP\n"
                    "  SUB SP, #2\n"
                    ".loc 0 3 0\n"
                    "  MOV R0, #7\n"
                    "  ST [R4-2], R0\n"
                    ".loc 0 4 0\n"
                    "  LD R0, [R4-2]\n"
                    "  HLT\n"
                    ".section .debug_vars\n"
                    ".asciz \"_start|local|2|2\"\n"
                    ".text\n"
                    ".section .debug_frames\n"
                    ".asciz \"_start|1\"\n"
                    ".text\n";
    }

    kr16::BuildOptions options;
    options.input = directory / "session_debug.s";
    options.output = directory / "session_debug.kex";
    options.debug = true;
    options.command = "session-test";
    std::ostringstream diagnostics;
    const auto result = kr16::build_program(options, diagnostics);
    if (!result.ok) {
        error = diagnostics.str();
        return {};
    }
    return result.executable;
}

}  // namespace

TEST_CASE("debug session loads an executable and decodes disassembly") {
    kr16::DebugSession session;
    std::string error;
    REQUIRE(session.load(fixture("exec_min.kex"), error));
    CHECK(session.loaded());
    CHECK(session.image_path() == fixture("exec_min.kex"));
    CHECK(session.pc() == 0x0100);

    REQUIRE(session.disassembly().size() >= 2);
    const auto& first = session.disassembly()[0];
    CHECK(first.address == 0x0100);
    CHECK(first.size == 2);
    CHECK(first.bytes.size() == 2);
    CHECK(first.bytes[0] == 0x01);
    CHECK(first.bytes[1] == 0x02);
    CHECK(first.text.find("ADC") != std::string::npos);

    CHECK(session.text_low() == 0x0100);
    CHECK(session.text_high() == 0x0104);
    CHECK(session.read8(0x0100) == 0x01);
    CHECK(session.read16(0x0100) == 0x0201);
}

TEST_CASE("debug session exposes registers and writable memory") {
    kr16::DebugSession session;
    std::string error;
    REQUIRE(session.load(fixture("exec_min.kex"), error));

    session.set_reg(0, 0x1234);
    CHECK(session.reg(0) == 0x1234);
    session.set_reg(5, 0x0102);
    CHECK(session.pc() == 0x0102);
    session.set_reg(6, 0xE000);
    CHECK(session.sp() == 0xE000);
    session.set_reg(7, 0x001F);
    CHECK(session.sr() == 0x001F);

    session.write8(0x0200, 0x5A);
    CHECK(session.read8(0x0200) == 0x5A);
    session.write16(0x0200, 0x1234);
    CHECK(session.read16(0x0200) == 0x1234);
    CHECK(session.read8(0x0200) == 0x34);
}

TEST_CASE("debug session steps one instruction and counts it") {
    kr16::DebugSession session;
    std::string error;
    REQUIRE(session.load(fixture("exec_min.kex"), error));
    CHECK(session.step_instruction());
    CHECK(session.pc() == 0x0102);
    CHECK(session.instructions_executed() == 1);

    session.reset_stats();
    CHECK(session.instructions_executed() == 0);
    CHECK(session.run_time_ms() == 0);
}

TEST_CASE("debug session stops at an address breakpoint and keeps it on restart") {
    kr16::DebugSession session;
    std::string error;
    REQUIRE(session.load(fixture("exec_min.kex"), error));

    session.toggle_address_breakpoint(0x0102);
    CHECK(session.has_breakpoint(0x0102));
    session.start_run();
    CHECK(session.running());

    const auto stop = session.run_slice(1000);
    CHECK(stop == kr16::SessionRunStop::Breakpoint);
    CHECK_FALSE(session.running());
    CHECK(session.pc() == 0x0102);
    CHECK(session.instructions_executed() == 1);

    REQUIRE(session.restart(error));
    CHECK(session.pc() == 0x0100);
    CHECK(session.has_breakpoint(0x0102));
    CHECK(session.instructions_executed() == 0);
}

TEST_CASE("debug session reports a load failure") {
    kr16::DebugSession session;
    std::string error;
    CHECK_FALSE(session.load(fixture("does-not-exist.kex"), error));
    CHECK_FALSE(session.loaded());
    CHECK_FALSE(error.empty());
}

TEST_CASE("debug session exposes source, functions, frames and locals") {
    std::string error;
    const auto image = make_debug_fixture(error);
    INFO(error);
    REQUIRE_FALSE(image.empty());

    kr16::DebugSession session;
    REQUIRE(session.load(image, error));
    CHECK(session.line_at(session.pc()) == 3);
    REQUIRE(session.source_lines().size() == 5);
    CHECK(session.source_lines()[2].text.find("local = 7") !=
          std::string::npos);
    CHECK(session.source_lines()[2].executable);
    CHECK(session.source_lines()[3].executable);
    CHECK(session.executable_lines().count(3) == 1);
    CHECK(session.executable_lines().count(4) == 1);

    REQUIRE_FALSE(session.functions().empty());
    CHECK(session.functions().front().name == "_start");
    CHECK(session.function_at(session.pc()) != nullptr);
    CHECK(session.function_at(session.pc())->name == "_start");

    const auto frames = session.frames();
    REQUIRE_FALSE(frames.empty());
    CHECK(frames.front().name == "_start");
    CHECK(frames.front().line == 3);

    const auto locals = session.locals();
    REQUIRE(locals.size() == 1);
    CHECK(locals[0].name == "local");
    CHECK(locals[0].scope == "_start");
    CHECK(locals[0].local);
    CHECK(locals[0].ok);

    REQUIRE(session.step_line(kr16::StepMode::LineIn));
    CHECK(session.line_at(session.pc()) == 4);
    const auto after = session.locals();
    REQUIRE(after.size() == 1);
    CHECK(after[0].value == 7);

    std::filesystem::remove_all(image.parent_path());
}

TEST_CASE("debug session builds and loads a C source file") {
    const auto directory =
        std::filesystem::current_path() / "kr16_session_build";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    ec.clear();
    std::filesystem::create_directories(directory, ec);
    REQUIRE_FALSE(ec);

    {
        std::ofstream source(directory / "main.c");
        REQUIRE(source.good());
        source << "int main(void) {\n"
                  "    int local;\n"
                  "    local = 42;\n"
                  "    return local;\n"
                  "}\n";
    }

    kr16::DebugSession session;
    std::string error;
    const auto libc = std::filesystem::path(KR16_SOURCE_DIR) / "resources" /
                      "libc.s";
    INFO(error);
    REQUIRE(session.build_and_load(directory / "main.c", libc, error));
    CHECK(session.loaded());
    CHECK_FALSE(session.source_lines().empty());
    CHECK(session.line_at(session.pc()) == 3);
    CHECK(session.function_at(session.pc()) != nullptr);
    CHECK(session.function_at(session.pc())->name == "main");

    std::filesystem::remove_all(directory, ec);
}

TEST_CASE("debug session runs to a line breakpoint") {
    std::string error;
    const auto image = make_debug_fixture(error);
    INFO(error);
    REQUIRE_FALSE(image.empty());

    kr16::DebugSession session;
    REQUIRE(session.load(image, error));
    REQUIRE(session.line_at(session.pc()) == 3);

    session.toggle_line_breakpoint(4);
    CHECK(session.has_line_breakpoint(4));
    session.start_run();
    const auto stop = session.run_slice(1000);
    CHECK(stop == kr16::SessionRunStop::Breakpoint);
    CHECK(session.line_at(session.pc()) == 4);
    CHECK_FALSE(session.running());

    REQUIRE(session.restart(error));
    CHECK(session.has_line_breakpoint(4));

    std::filesystem::remove_all(image.parent_path());
}
