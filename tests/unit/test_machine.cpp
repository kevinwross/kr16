#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/cpu.hpp"
#include "kr16/machine.hpp"

TEST_CASE("RAM, ROM, and MMIO boundaries") {
    kr16::Machine machine;
    machine.zero_ram();
    machine.write8(0x0100, 0x42);
    CHECK(machine.read8(0x0100) == 0x42);

    std::vector<std::uint8_t> rom(kr16::Machine::kRomSize, 0);
    rom[0] = 0xAA;
    rom[kr16::Machine::kRomSize - 1] = 0x55;
    machine.load_rom(rom);
    CHECK(machine.read8(0xF100) == 0xAA);
    CHECK(machine.read8(0xFFFF) == 0x55);
    machine.write8(0xF100, 0x00);
    CHECK(machine.read8(0xF100) == 0xAA);
}

TEST_CASE("console output and echo") {
    kr16::Machine machine;
    machine.write8(0xF000, 'H');
    machine.write8(0xF000, 'i');
    REQUIRE(machine.console_output().size() == 2);
    CHECK(machine.console_output()[0] == 'H');
    CHECK(machine.console_output()[1] == 'i');

    machine.clear_console_output();
    machine.write8(0xF002, 1);
    machine.push_key('A');
    REQUIRE(machine.console_output().size() == 1);
    CHECK(machine.console_output()[0] == 'A');
}

TEST_CASE("keyboard FIFO and status") {
    kr16::Machine machine;
    machine.push_key('A');
    machine.push_key('B');
    CHECK(machine.keyboard_available());
    CHECK(machine.read8(0xF005) == 1);
    CHECK(machine.read8(0xF004) == 'A');
    CHECK(machine.read8(0xF004) == 'B');
    CHECK_FALSE(machine.keyboard_available());
    CHECK(machine.read8(0xF004) == 0);

    for (int i = 0; i < 65; ++i) {
        machine.push_key(static_cast<std::uint8_t>('a' + (i % 26)));
    }
    CHECK(machine.keyboard_overflow());
    CHECK((machine.read8(0xF005) & 2) != 0);
    machine.write8(0xF005, 2);
    CHECK_FALSE(machine.keyboard_overflow());
}

TEST_CASE("timer counts, matches, and raises IRQ status") {
    kr16::Machine machine;
    machine.write8(0xF00A, 0);  // count low
    machine.write8(0xF00B, 0);  // count high
    machine.write8(0xF00C, 2);  // compare low
    machine.write8(0xF00D, 0);  // compare high
    machine.write8(0xF010, 1);  // prescale = 1 => divide by 2
    machine.write8(0xF008, 4 | 1);  // IRQ_EN | EN, one-shot
    machine.tick(1);
    CHECK(machine.timer_count() == 0);
    machine.tick(1);
    CHECK(machine.timer_count() == 1);
    machine.tick(2);
    CHECK(machine.timer_count() == 2);
    CHECK(machine.timer_match());
    CHECK(machine.read8(0xF00E) == 1);
    CHECK(machine.read8(0xF012) == 1);
    machine.write8(0xF00E, 1);
    CHECK_FALSE(machine.timer_match());
    CHECK(machine.read8(0xF012) == 0);
}

TEST_CASE("CPU accepts timer interrupt") {
    kr16::Machine machine;
    kr16::Cpu cpu;
    machine.write16(0x0006, 0x0200);  // timer vector 3
    machine.write8(0xF00C, 1);
    machine.write8(0xF00D, 0);
    machine.write8(0xF008, 4 | 1);
    machine.tick(1);
    REQUIRE(machine.pending_interrupt().has_value());
    CHECK(*machine.pending_interrupt() == 3);

    cpu.set_pc(0x0100);
    cpu.set_flag(kr16::Cpu::FlagI, true);
    cpu.set_interrupt_ack([&machine]() { return machine.pending_interrupt(); });
    machine.write16(0x0100, 0x8900);  // NOP
    CHECK(cpu.step(machine) == 7);
    CHECK(cpu.pc() == 0x0200);
    CHECK(cpu.reg(5) == 0xEFFC);
    CHECK_FALSE(cpu.flag(kr16::Cpu::FlagI));
}

TEST_CASE("CPU accepts keyboard interrupt before timer") {
    kr16::Machine machine;
    kr16::Cpu cpu;
    machine.write16(0x0006, 0x0200);
    machine.write16(0x0008, 0x0300);
    machine.write8(0xF00C, 1);
    machine.write8(0xF008, 4 | 1);
    machine.tick(1);
    machine.write8(0xF006, 1);
    machine.push_key('Z');
    cpu.set_pc(0x0100);
    cpu.set_flag(kr16::Cpu::FlagI, true);
    cpu.set_interrupt_ack([&machine]() { return machine.pending_interrupt(); });
    machine.write16(0x0100, 0x8900);
    CHECK(cpu.step(machine) == 7);
    CHECK(cpu.pc() == 0x0300);
}

TEST_CASE("boot reads reset vector from ROM") {
    kr16::Machine machine;
    std::vector<std::uint8_t> rom(kr16::Machine::kRomSize, 0);
    rom[kr16::Machine::kRomSize - 2] = 0x00;
    rom[kr16::Machine::kRomSize - 1] = 0xF1;
    machine.load_rom(rom);
    CHECK(machine.read16(0xFFFE) == 0xF100);
}
