#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "kr16/cli.hpp"

TEST_CASE("known commands are recognized") {
    CHECK(kr16::cli::is_known_command("as"));
    CHECK(kr16::cli::is_known_command("debug"));
    CHECK(kr16::cli::is_known_command("dap"));
    CHECK(kr16::cli::is_known_command("version"));
    CHECK(kr16::cli::is_known_command("shell"));
    CHECK_FALSE(kr16::cli::is_known_command("nope"));
    CHECK_FALSE(kr16::cli::is_known_command(""));
}

TEST_CASE("usage lists every command") {
    const std::string text = kr16::cli::usage_text();
    CHECK(text.find("usage: kr16 <command>") != std::string::npos);
    CHECK(text.find("as") != std::string::npos);
    CHECK(text.find("build") != std::string::npos);
    CHECK(text.find("cc") != std::string::npos);
    CHECK(text.find("ld") != std::string::npos);
    CHECK(text.find("debug") != std::string::npos);
    CHECK(text.find("vm") != std::string::npos);
    CHECK(text.find("dap") != std::string::npos);
    CHECK(text.find("version") != std::string::npos);
    CHECK(text.find("help") != std::string::npos);
}

TEST_CASE("as -l writes a listing beside the object") {
    const auto directory =
        std::filesystem::current_path() / "kr16_as_listing";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    const auto source = directory / "probe.s";
    {
        std::ofstream out(source);
        out << ".text\n"
               "start:\n"
               "  MOV R0, #1\n"
               "  HLT\n";
    }

    std::string program = "kr16";
    std::string command = "as";
    std::string source_arg = source.string();
    std::string output_flag = "-o";
    std::string object_arg = (directory / "probe.o").string();
    std::string listing_flag = "-l";
    std::string listing_arg = (directory / "probe.lst").string();

    std::vector<char*> argv;
    for (auto* argument : {&program, &command, &source_arg, &output_flag,
                           &object_arg, &listing_flag, &listing_arg}) {
        argv.push_back(argument->data());
    }

    const int status = kr16::cli::run(static_cast<int>(argv.size()),
                                      argv.data());
    CHECK(status == 0);
    CHECK(std::filesystem::exists(directory / "probe.o"));
    REQUIRE(std::filesystem::exists(directory / "probe.lst"));

    std::string text;
    {
        std::ifstream listing(directory / "probe.lst");
        text.assign(std::istreambuf_iterator<char>(listing),
                    std::istreambuf_iterator<char>());
    }
    CHECK(text.find("MOV R0, #1") != std::string::npos);
    CHECK(text.find("81 11") != std::string::npos);
    CHECK(text.find("0x0000") != std::string::npos);
    CHECK(text.find("0x0002") != std::string::npos);
    CHECK(text.find("HLT") != std::string::npos);

    std::filesystem::remove_all(directory);
}

TEST_CASE("as -l without a path is a usage error") {
    std::string program = "kr16";
    std::string command = "as";
    std::string source_arg = "probe.s";
    std::string listing_flag = "-l";
    std::vector<char*> argv;
    for (auto* argument : {&program, &command, &source_arg, &listing_flag}) {
        argv.push_back(argument->data());
    }
    CHECK(kr16::cli::run(static_cast<int>(argv.size()), argv.data()) == 2);
}
