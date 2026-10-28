#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/container.hpp"

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        throw std::runtime_error("cannot open fixture: " + path.string());
    }
    const std::vector<char> chars{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
    return std::vector<std::uint8_t>(chars.begin(), chars.end());
}

}  // namespace

TEST_CASE("golden EXEC container decodes") {
    const auto path = std::filesystem::path(KR16_SOURCE_DIR) / "tests" /
                      "golden" / "exec_min.kex";
    const auto bytes = read_file(path);
    const auto container = kr16::Container::read(bytes);
    CHECK(container.kind == kr16::ContainerKind::Exec);
    CHECK(container.entry == 0x0100);
    REQUIRE(container.symbols.size() == 1);
    CHECK(container.symbols[0].name == "main");
    REQUIRE(container.find_section(".text") != nullptr);
    CHECK(container.find_section(".text")->data.size() == 4);
}

TEST_CASE("golden REL container decodes") {
    const auto path = std::filesystem::path(KR16_SOURCE_DIR) / "tests" /
                      "golden" / "rel_min.o";
    const auto bytes = read_file(path);
    const auto container = kr16::Container::read(bytes);
    CHECK(container.kind == kr16::ContainerKind::Rel);
    REQUIRE(container.relocations.size() == 1);
    CHECK(container.relocations[0].type == kr16::RelocationType::Abs16);
    CHECK(container.relocations[0].addend == 4);
}
