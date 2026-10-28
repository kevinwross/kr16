#include <doctest/doctest.h>

#include "kr16/cli.hpp"

TEST_CASE("known commands are recognized") {
    CHECK(kr16::cli::is_known_command("as"));
    CHECK(kr16::cli::is_known_command("debug"));
    CHECK(kr16::cli::is_known_command("dap"));
    CHECK(kr16::cli::is_known_command("version"));
    CHECK_FALSE(kr16::cli::is_known_command("nope"));
    CHECK_FALSE(kr16::cli::is_known_command(""));
}

TEST_CASE("usage lists every command") {
    const std::string text = kr16::cli::usage_text();
    CHECK(text.find("usage: kr16 <command>") != std::string::npos);
    CHECK(text.find("as") != std::string::npos);
    CHECK(text.find("cc") != std::string::npos);
    CHECK(text.find("ld") != std::string::npos);
    CHECK(text.find("debug") != std::string::npos);
    CHECK(text.find("vm") != std::string::npos);
    CHECK(text.find("dap") != std::string::npos);
    CHECK(text.find("version") != std::string::npos);
    CHECK(text.find("help") != std::string::npos);
}
