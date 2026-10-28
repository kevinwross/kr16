#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "kr16/build.hpp"
#include "kr16/cli.hpp"
#include "kr16/container.hpp"

namespace {

namespace fs = std::filesystem;

int run_cli(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& argument : args) {
        argv.push_back(argument.data());
    }
    return kr16::cli::run(static_cast<int>(argv.size()), argv.data());
}

fs::path make_workspace(const std::string& name) {
    const auto directory = fs::current_path() / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    const auto text = read_text(path);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

}  // namespace

TEST_CASE("build turns C source into a runnable .kex") {
    const auto directory = make_workspace("kr16_build_c");
    const auto source = directory / "program.c";
    write_file(source, "int main(void) { return 7; }\n");

    CHECK(run_cli({"kr16", "build", source.string()}) == 0);
    CHECK(std::filesystem::exists(directory / "program.kex"));
    CHECK(std::filesystem::exists(directory / "program.o"));
    CHECK(run_cli({"kr16", "vm", (directory / "program.kex").string()}) == 7);

    std::filesystem::remove_all(directory);
}

TEST_CASE("build -l writes a listing beside the source") {
    const auto directory = make_workspace("kr16_build_listing");
    const auto source = directory / "program.c";
    write_file(source, "int main(void) { return 1; }\n");

    CHECK(run_cli({"kr16", "build", "-l", source.string()}) == 0);
    REQUIRE(std::filesystem::exists(directory / "program.lst"));
    const auto listing = read_text(directory / "program.lst");
    CHECK(listing.find(".file 0") != std::string::npos);
    CHECK(listing.find("main:") != std::string::npos);
    CHECK(listing.find("0x0000") != std::string::npos);

    // -L is the same flag; --listing=FILE renames the file.
    CHECK(run_cli({"kr16", "build", "-L", source.string()}) == 0);
    const auto custom = directory / "custom.lst";
    CHECK(run_cli({"kr16", "build", "--listing=" + custom.string(),
                   source.string()}) == 0);
    CHECK(std::filesystem::exists(custom));

    std::filesystem::remove_all(directory);
}

TEST_CASE("build records debug info unless --no-debug is given") {
    const auto directory = make_workspace("kr16_build_debug");
    const auto source = directory / "program.c";
    write_file(source, "int main(void) { return 2; }\n");

    CHECK(run_cli({"kr16", "build", source.string()}) == 0);
    {
        const auto bytes = read_bytes(directory / "program.kex");
        const auto image = kr16::Container::read(bytes);
        CHECK(image.find_section(".debug_line") != nullptr);
        CHECK(image.find_section(".text") != nullptr);
    }
    CHECK(run_cli({"kr16", "build", "--no-debug", "-o",
                   (directory / "plain.kex").string(), source.string()}) == 0);
    {
        const auto bytes = read_bytes(directory / "plain.kex");
        const auto image = kr16::Container::read(bytes);
        CHECK(image.find_section(".debug_line") == nullptr);
        CHECK(image.find_section(".text") != nullptr);
    }

    std::filesystem::remove_all(directory);
}

TEST_CASE("build --run returns the program exit code") {
    const auto directory = make_workspace("kr16_build_run");
    const auto source = directory / "program.c";
    write_file(source, "int main(void) { return 9; }\n");

    CHECK(run_cli({"kr16", "build", "--run", source.string()}) == 9);

    std::filesystem::remove_all(directory);
}

TEST_CASE("build -I resolves include paths") {
    const auto directory = make_workspace("kr16_build_include");
    const auto include = directory / "inc";
    std::filesystem::create_directories(include);
    write_file(include / "answer.h", "#define ANSWER 5\n");
    const auto source = directory / "program.c";
    write_file(source,
               "#include \"answer.h\"\n"
               "int main(void) { return ANSWER; }\n");

    CHECK(run_cli({"kr16", "build", "-I", include.string(), source.string()}) ==
          0);
    CHECK(run_cli({"kr16", "vm", (directory / "program.kex").string()}) == 5);

    std::filesystem::remove_all(directory);
}

TEST_CASE("build accepts assembly and object input") {
    const auto directory = make_workspace("kr16_build_asm");
    const auto source = directory / "program.s";
    write_file(source,
               ".text\n"
               ".global _start\n"
               "_start:\n"
               "  MOV R0, #4\n"
               "  HLT\n");

    CHECK(run_cli({"kr16", "build", source.string()}) == 0);
    CHECK(run_cli({"kr16", "vm", (directory / "program.kex").string()}) == 4);

    CHECK(run_cli({"kr16", "build", (directory / "program.o").string(), "-o",
                   (directory / "from_object.kex").string()}) == 0);
    CHECK(run_cli({"kr16", "vm", (directory / "from_object.kex").string()}) ==
          4);

    std::filesystem::remove_all(directory);
}

TEST_CASE("build --libc links the runtime for object input") {
    const auto directory = make_workspace("kr16_build_libc");
    const auto source = directory / "program.c";
    write_file(source, "int main(void) { return 3; }\n");
    const auto object = directory / "program.o";

    REQUIRE(run_cli({"kr16", "cc", source.string(), "-c", "-o",
                     object.string()}) == 0);
    CHECK(run_cli({"kr16", "build", object.string(), "--libc", "-o",
                   (directory / "linked.kex").string()}) == 0);
    CHECK(run_cli({"kr16", "vm", (directory / "linked.kex").string()}) == 3);

    std::filesystem::remove_all(directory);
}

TEST_CASE("build rejects inputs it cannot turn into a .kex") {
    CHECK(run_cli({"kr16", "build"}) == 2);
    CHECK(run_cli({"kr16", "build", "program.txt"}) == 2);
    CHECK(run_cli({"kr16", "build", "program.kex"}) == 2);
    CHECK(run_cli({"kr16", "build", "--nope", "program.c"}) == 2);
}
