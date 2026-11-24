#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <doctest/doctest.h>

#include "kr16/shell.hpp"

TEST_CASE("shell tokenizer handles quotes and escapes") {
    const auto result = kr16::cli::tokenize_shell_line(
        "as \"my program.s\" -o \"out put.o\"");
    REQUIRE(result.error.empty());
    REQUIRE(result.tokens.size() == 4);
    CHECK(result.tokens[0] == "as");
    CHECK(result.tokens[1] == "my program.s");
    CHECK(result.tokens[2] == "-o");
    CHECK(result.tokens[3] == "out put.o");
}

TEST_CASE("shell tokenizer reports an unterminated quote") {
    const auto result = kr16::cli::tokenize_shell_line("as \"missing");
    CHECK_FALSE(result.error.empty());
    CHECK(result.tokens.empty());
}

TEST_CASE("shell tokenizer preserves quoted Windows paths") {
    const auto result =
        kr16::cli::tokenize_shell_line("load \"C:\\work\\hello.c\"");
    REQUIRE(result.error.empty());
    REQUIRE(result.tokens.size() == 2);
    CHECK(result.tokens[1] == "C:\\work\\hello.c");
}

TEST_CASE("shell history is capped and persists") {
    const auto path = std::filesystem::current_path() / "kr16_shell_history";
    std::filesystem::remove(path);
    {
        kr16::cli::ShellHistory history(3);
        history.add("one");
        history.add("two");
        history.add("three");
        history.add("four");
        REQUIRE(history.entries().size() == 3);
        CHECK(history.entries()[0] == "two");
        CHECK(history.entries()[2] == "four");
        history.save(path.string());
    }
    {
        kr16::cli::ShellHistory history(100);
        history.load(path.string());
        REQUIRE(history.entries().size() == 3);
        CHECK(history.entries()[2] == "four");
    }
    std::filesystem::remove(path);
}

TEST_CASE("shell loop continues after failures") {
    std::istringstream input(
        "pwd\nversion\nnot-a-command\ndap build/hello.kr16\nquit\n");
    std::ostringstream output;
    std::ostringstream error;
    const int status = kr16::cli::run_shell(input, output, error, false);
    CHECK(status == 0);
    CHECK(output.str().find("kr16 1.0.0") != std::string::npos);
    CHECK(error.str().find("unknown command") != std::string::npos);
    CHECK(error.str().find("refusing to block") != std::string::npos);
}

TEST_CASE("shell load builds and links C source") {
    const auto directory =
        std::filesystem::current_path() / "kr16_shell_load";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto source = directory / "hello.c";
    {
        std::ofstream output(source);
        output << "int main(void) { return 7; }\n";
    }
    const std::string script =
        "load \"" + source.string() + "\"\nquit\n";
    std::istringstream input(script);
    std::ostringstream output;
    std::ostringstream error;
    const int status =
        kr16::cli::run_shell(input, output, error, false);
    CHECK(status == 0);
    CHECK(std::filesystem::exists(directory / "hello.kr16"));
    std::filesystem::remove_all(directory);
}
