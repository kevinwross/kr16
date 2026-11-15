#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "kr16/cli.hpp"

namespace {

std::string run_cli(std::initializer_list<std::string> arguments, int& status) {
    std::vector<std::string> storage(arguments);
    std::vector<char*> argv;
    for (auto& argument : storage) {
        argv.push_back(argument.data());
    }
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    status = kr16::cli::run(static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(previous);
    return output.str();
}

}  // namespace

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

TEST_CASE("every command has detailed --help") {
    const char* commands[] = {"as", "dis", "cc", "build", "ld", "size",
                              "debug", "vm", "dap", "version", "shell", "help"};
    for (const char* command : commands) {
        int status = -1;
        const auto text = run_cli({"kr16", command, "--help"}, status);
        CHECK(status == 0);
        CHECK(text.find(std::string("usage: kr16 ") + command) !=
              std::string::npos);
        CHECK(text.find("-h, --help") != std::string::npos);
    }

    int status = -1;
    const auto as = run_cli({"kr16", "as", "--help"}, status);
    CHECK(status == 0);
    CHECK(as.find("--raw") != std::string::npos);
    CHECK(as.find("--base") != std::string::npos);
    CHECK(as.find("-l <path>") != std::string::npos);

    const auto ld = run_cli({"kr16", "ld", "--help"}, status);
    CHECK(status == 0);
    CHECK(ld.find("--gc-all") != std::string::npos);

    const auto aliased = run_cli({"kr16", "help", "vm"}, status);
    CHECK(status == 0);
    CHECK(aliased.find("usage: kr16 vm") != std::string::npos);
    CHECK(aliased.find("recursive over fs-root") != std::string::npos);
    CHECK(aliased.find("dedicated directory") != std::string::npos);
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

TEST_CASE("build accepts optimization-level flags") {
    auto source =
        std::filesystem::current_path() / "cli_opt_flag.c";
    const auto image =
        std::filesystem::current_path() / "cli_opt_flag.kex";
    {
        std::ofstream output(source);
        output << "int main(void) { return 0; }\n";
    }
    int status = 0;
    (void)run_cli({"kr16", "build", source.string(), "-O0", "-o",
                   image.string()}, status);
    CHECK(status == 0);
    (void)run_cli({"kr16", "build", source.string(), "-O1", "-o",
                   image.string()}, status);
    CHECK(status == 0);
    std::filesystem::remove(source);
    std::filesystem::remove(image);
    auto base = source;
    base.replace_extension();
    const std::string stem = base.string();
    std::filesystem::remove(stem + ".s");
    std::filesystem::remove(stem + ".o");
    std::filesystem::remove(stem + ".libc.o");
    std::filesystem::remove(stem + ".fileio.o");
}




