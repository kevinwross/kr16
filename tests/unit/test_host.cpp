#include <cstdint>

#include <doctest/doctest.h>

#include "kr16/cpu.hpp"
#include "kr16/host.hpp"
#include "kr16/instruction.hpp"

TEST_CASE("SYS_VERSION returns ABI and capabilities") {
    kr16::FlatMemory memory;
    kr16::HostServices host;
    constexpr std::uint16_t block = 0x0300;
    const auto status = host.call(0, block, memory);
    CHECK(status == 0);
    CHECK(memory.read16(block) == 0);
    CHECK(memory.read16(block + 2) == 1);
    CHECK(memory.read16(block + 4) == ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5)));
    CHECK(memory.read16(block + 6) == 0);
}

TEST_CASE("unimplemented host services return ENOSYS") {
    kr16::FlatMemory memory;
    kr16::HostServices host;
    constexpr std::uint16_t block = 0x0300;
    CHECK(host.call(99, block, memory) == 8);
    CHECK(memory.read16(block) == 8);
}

TEST_CASE("invalid host call blocks return EFAULT") {
    kr16::FlatMemory memory;
    kr16::HostServices host;
    CHECK(host.call(0, 0x0301, memory) == 5);
    CHECK(host.call(0, 0xF000, memory) == 5);
    CHECK(host.call(0, 0xFFFF, memory) == 5);
}

TEST_CASE("CPU SYS leaves flags and R0 unchanged") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    kr16::HostServices host;
    constexpr std::uint16_t block = 0x0300;
    cpu.set_pc(0x0100);
    cpu.set_reg(0, block);
    cpu.set_flag(kr16::Cpu::FlagC, true);
    cpu.set_flag(kr16::Cpu::FlagZ, true);
    cpu.set_syscall_handler(
        [&host](std::uint8_t service, std::uint16_t address,
                kr16::MemoryBus& bus) {
            host.call(service, address, bus);
        });
    memory.write16(0x0100, 0x8F00);    CHECK(cpu.step(memory) == 10);
    CHECK(cpu.pc() == 0x0102);
    CHECK(cpu.reg(0) == block);
    CHECK(cpu.flag(kr16::Cpu::FlagC));
    CHECK(cpu.flag(kr16::Cpu::FlagZ));
    CHECK(memory.read16(block + 2) == 1);
    CHECK(host.call_count() == 1);
}

TEST_CASE("host call tracing records service and status") {
    kr16::FlatMemory memory;
    kr16::HostServices host(true);
    host.call(0, 0x0300, memory);
    host.call(99, 0x0300, memory);
    CHECK(host.trace().find("SYS #0") != std::string::npos);
    CHECK(host.trace().find("status=8") != std::string::npos);
}
