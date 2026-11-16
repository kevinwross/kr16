#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include <doctest/doctest.h>

#include "kr16/machine.hpp"
#include "kr16/resources.hpp"
#include "../../src/resources/embedded_libc.hpp"

TEST_CASE("embedded BIOS resource is loadable") {
    const auto rom = kr16::embedded_bios_rom();
    REQUIRE(rom.size() == kr16::Machine::kRomSize);
    kr16::Machine machine;
    machine.load_rom(rom);
    CHECK(machine.read16(0xFFFE) == 0xF100);
    CHECK(machine.read8(0xF100) == 0xE0);
    CHECK(machine.read8(0xF101) == 0xF0);
}

TEST_CASE("embedded runtime sources match checked-in resources") {
    const auto read = [](const std::string& name) {
        std::ifstream input(std::string(KR16_SOURCE_DIR) + "/" + name,
                            std::ios::binary);
        REQUIRE(input.good());
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };
    const auto libc = read("resources/libc.s");
    const auto fileio = read("resources/fileio.c");
    const auto services = read("include/kr16/systemservices.h");
    CHECK(std::string(reinterpret_cast<const char*>(
                          kr16::embedded_libc_source().data()),
                      kr16::embedded_libc_source().size()) == libc);
    CHECK(std::string(reinterpret_cast<const char*>(
                          kr16::embedded_fileio_source().data()),
                      kr16::embedded_fileio_source().size()) == fileio);
    CHECK(std::string(reinterpret_cast<const char*>(
                          kr16::embedded_systemservices_header().data()),
                      kr16::embedded_systemservices_header().size()) == services);
}




