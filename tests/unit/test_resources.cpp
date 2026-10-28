#include <cstdint>

#include <doctest/doctest.h>

#include "kr16/machine.hpp"
#include "kr16/resources.hpp"

TEST_CASE("embedded BIOS resource is loadable") {
    const auto rom = kr16::embedded_bios_rom();
    REQUIRE(rom.size() == kr16::Machine::kRomSize);
    kr16::Machine machine;
    machine.load_rom(rom);
    CHECK(machine.read16(0xFFFE) == 0xF100);
    CHECK(machine.read8(0xF100) == 0xE0);
    CHECK(machine.read8(0xF101) == 0xF0);
}
