#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/cpu.hpp"
#include "kr16/instruction.hpp"

namespace {

void emit(kr16::FlatMemory& memory, std::uint16_t address,
          const kr16::Instruction& instruction) {
    const auto words = instruction.encode();
    for (std::size_t i = 0; i < words.size(); ++i) {
        memory.write16(static_cast<std::uint16_t>(address + i * 2), words[i]);
    }
}

kr16::Instruction mov_imm(std::uint8_t rd, std::int32_t imm) {
    kr16::Instruction instruction;
    instruction.op = kr16::Op::Mov;
    instruction.rd = rd;
    instruction.imm = imm;
    instruction.is_immediate = true;
    return instruction;
}

kr16::Instruction alu(kr16::Op op, std::uint8_t rd, std::uint8_t rs) {
    kr16::Instruction instruction;
    instruction.op = op;
    instruction.rd = rd;
    instruction.rs = rs;
    return instruction;
}

}  // namespace

TEST_CASE("CPU reset state") {
    kr16::Cpu cpu;
    CHECK(cpu.reg(0) == 0);
    CHECK(cpu.reg(5) == 0xF000);
    CHECK(cpu.reg(6) == 0);
    CHECK(cpu.pc() == 0);
    CHECK_FALSE(cpu.halted());
}

TEST_CASE("MOV and ALU flag behavior") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);

    emit(memory, 0x0100, mov_imm(0, 0x1234));
    CHECK(cpu.step(memory) == 2);  // 32-bit MOV: 1 + 1
    CHECK(cpu.reg(0) == 0x1234);

    cpu.set_reg(0, 0x7FFF);
    cpu.set_reg(1, 1);
    emit(memory, 0x0104, alu(kr16::Op::Add, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0x8000);
    CHECK(cpu.flag(kr16::Cpu::FlagV));
    CHECK(cpu.flag(kr16::Cpu::FlagN));
    CHECK_FALSE(cpu.flag(kr16::Cpu::FlagZ));
    CHECK_FALSE(cpu.flag(kr16::Cpu::FlagC));

    cpu.set_reg(0, 0xFFFF);
    cpu.set_reg(1, 1);
    emit(memory, 0x0106, alu(kr16::Op::Add, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0);
    CHECK(cpu.flag(kr16::Cpu::FlagC));
    CHECK(cpu.flag(kr16::Cpu::FlagZ));

    cpu.set_reg(0, 5);
    cpu.set_reg(1, 3);
    emit(memory, 0x0108, alu(kr16::Op::Sub, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 2);
    CHECK(cpu.flag(kr16::Cpu::FlagC));

    cpu.set_reg(0, 3);
    cpu.set_reg(1, 5);
    emit(memory, 0x010A, alu(kr16::Op::Sub, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0xFFFE);
    CHECK_FALSE(cpu.flag(kr16::Cpu::FlagC));
    CHECK(cpu.flag(kr16::Cpu::FlagN));
}

TEST_CASE("CMP and TEST do not write") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);
    cpu.set_reg(0, 7);
    cpu.set_reg(1, 9);

    emit(memory, 0x0100, alu(kr16::Op::Cmp, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 7);
    CHECK(cpu.flag(kr16::Cpu::FlagC) == false);

    cpu.set_reg(0, 0x00F1);
    cpu.set_reg(1, 1);
    emit(memory, 0x0102, alu(kr16::Op::Test, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0x00F1);
    CHECK_FALSE(cpu.flag(kr16::Cpu::FlagZ));
}

TEST_CASE("MUL, MULS, and MULU") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);
    cpu.set_reg(0, 0x1000);
    cpu.set_reg(1, 0x0010);
    emit(memory, 0x0100, alu(kr16::Op::Mul, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0);

    cpu.set_pc(0x0102);
    cpu.set_reg(0, 0xFFFF);
    cpu.set_reg(1, 2);
    emit(memory, 0x0102, alu(kr16::Op::MulS, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0xFFFE);
    CHECK(cpu.reg(1) == 0xFFFF);
    CHECK(cpu.flag(kr16::Cpu::FlagN));
    CHECK_FALSE(cpu.flag(kr16::Cpu::FlagZ));

    cpu.set_pc(0x0104);
    cpu.set_reg(0, 0xFFFF);
    cpu.set_reg(1, 2);
    emit(memory, 0x0104, alu(kr16::Op::MulU, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0xFFFE);
    CHECK(cpu.reg(1) == 0x0001);
}

TEST_CASE("divide by zero takes vector 2") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    memory.write16(4, 0x0200);
    cpu.set_pc(0x0100);
    cpu.set_reg(0, 100);
    cpu.set_reg(1, 0);
    emit(memory, 0x0100, alu(kr16::Op::Div, 0, 1));
    CHECK(cpu.step(memory) == 7);
    CHECK(cpu.pc() == 0x0200);
    CHECK(cpu.reg(0) == 100);
    CHECK(cpu.reg(5) == 0xEFFC);
}

TEST_CASE("loads and stores") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);
    cpu.set_reg(1, 0x0300);
    memory.write16(0x0304, 0xBEEF);
    memory.write8(0x0306, 0x80);
    memory.write8(0x0307, 0x7F);

    kr16::Instruction load;
    load.op = kr16::Op::Load;
    load.rd = 0;
    load.rs = 1;
    load.imm = 4;
    emit(memory, 0x0100, load);
    CHECK(cpu.step(memory) == 2);
    CHECK(cpu.reg(0) == 0xBEEF);

    kr16::Instruction store;
    store.op = kr16::Op::Store;
    store.rd = 0;
    store.rs = 1;
    store.imm = 6;
    emit(memory, 0x0102, store);
    CHECK(cpu.step(memory) == 2);
    CHECK(memory.read16(0x0306) == 0xBEEF);
    memory.write8(0x0306, 0x80);
    memory.write8(0x0307, 0x7F);

    kr16::Instruction load_signed;
    load_signed.op = kr16::Op::LoadSignedByte;
    load_signed.rd = 2;
    load_signed.rs = 1;
    load_signed.imm = 6;
    emit(memory, 0x0104, load_signed);
    CHECK(cpu.step(memory) == 2);
    CHECK(cpu.reg(2) == 0xFF80);

    kr16::Instruction load_byte;
    load_byte.op = kr16::Op::LoadByte;
    load_byte.rd = 3;
    load_byte.rs = 1;
    load_byte.imm = 7;
    emit(memory, 0x0106, load_byte);
    CHECK(cpu.step(memory) == 2);
    CHECK(cpu.reg(3) == 0x007F);

    kr16::Instruction store_byte;
    store_byte.op = kr16::Op::StoreByte;
    store_byte.rd = 3;
    store_byte.rs = 1;
    store_byte.imm = 8;
    emit(memory, 0x0108, store_byte);
    CHECK(cpu.step(memory) == 2);
    CHECK(memory.read8(0x0308) == 0x7F);
}

TEST_CASE("stack, call, and return") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);
    cpu.set_reg(0, 0x1234);

    kr16::Instruction push;
    push.op = kr16::Op::Push;
    push.rd = 0;
    emit(memory, 0x0100, push);
    CHECK(cpu.step(memory) == 2);
    CHECK(cpu.reg(5) == 0xEFFE);
    CHECK(memory.read16(0xEFFE) == 0x1234);

    kr16::Instruction pop;
    pop.op = kr16::Op::Pop;
    pop.rd = 1;
    emit(memory, 0x0102, pop);
    CHECK(cpu.step(memory) == 2);
    CHECK(cpu.reg(1) == 0x1234);
    CHECK(cpu.reg(5) == 0xF000);

    cpu.set_reg(2, 0x0200);
    kr16::Instruction call;
    call.op = kr16::Op::CallReg;
    call.rs = 2;
    emit(memory, 0x0104, call);
    CHECK(cpu.step(memory) == 3);
    CHECK(cpu.pc() == 0x0200);
    CHECK(cpu.reg(5) == 0xEFFE);
    CHECK(memory.read16(0xEFFE) == 0x0106);

    kr16::Instruction ret;
    ret.op = kr16::Op::Ret;
    emit(memory, 0x0200, ret);
    CHECK(cpu.step(memory) == 3);
    CHECK(cpu.pc() == 0x0106);
    CHECK(cpu.reg(5) == 0xF000);
}

TEST_CASE("branches and PC-relative calls") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);
    cpu.set_flag(kr16::Cpu::FlagZ, true);

    kr16::Instruction branch;
    branch.op = kr16::Op::Bcc;
    branch.cond = 0;
    branch.imm = 2;
    emit(memory, 0x0100, branch);
    CHECK(cpu.step(memory) == 2);
    CHECK(cpu.pc() == 0x0104);

    cpu.set_pc(0x0104);
    cpu.set_flag(kr16::Cpu::FlagZ, false);
    emit(memory, 0x0104, branch);
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.pc() == 0x0106);

    cpu.set_pc(0x0106);
    kr16::Instruction call;
    call.op = kr16::Op::CallRel;
    call.imm = 1;
    emit(memory, 0x0106, call);
    CHECK(cpu.step(memory) == 4);
    CHECK(cpu.pc() == 0x010C);
    CHECK(memory.read16(0xEFFE) == 0x010A);
}

TEST_CASE("trap and breakpoint vectors") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    memory.write16(0x0010 + 2 * 5, 0x0300);
    memory.write16(0x0002, 0x0400);
    cpu.set_pc(0x0100);

    kr16::Instruction trap;
    trap.op = kr16::Op::Trap;
    trap.n = 5;
    emit(memory, 0x0100, trap);
    CHECK(cpu.step(memory) == 4);
    CHECK(cpu.pc() == 0x0300);
    CHECK(memory.read16(0xEFFE) == 0x0102);

    cpu.set_pc(0x0102);
    kr16::Instruction brk;
    brk.op = kr16::Op::Brk;
    emit(memory, 0x0102, brk);
    CHECK(cpu.step(memory) == 7);
    CHECK(cpu.pc() == 0x0400);
    CHECK(cpu.reg(5) == 0xEFFA);
    CHECK(memory.read16(0xEFFA) == 0x0104);
}

TEST_CASE("shift, bit, and carry instructions") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);
    cpu.set_reg(0, 0x8001);

    kr16::Instruction shift;
    shift.op = kr16::Op::Shl;
    shift.rd = 0;
    shift.imm = 1;
    shift.is_immediate = true;
    emit(memory, 0x0100, shift);
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0x0002);
    CHECK(cpu.flag(kr16::Cpu::FlagC));

    cpu.set_reg(0, 0x8001);
    shift.op = kr16::Op::Sar;
    emit(memory, 0x0102, shift);
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0xC000);
    CHECK(cpu.flag(kr16::Cpu::FlagN));
    CHECK(cpu.flag(kr16::Cpu::FlagC));

    kr16::Instruction bit;
    bit.op = kr16::Op::Btst;
    bit.rd = 0;
    bit.imm = 1;
    bit.is_immediate = true;
    emit(memory, 0x0104, bit);
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.flag(kr16::Cpu::FlagZ));

    bit.op = kr16::Op::Bset;
    emit(memory, 0x0106, bit);
    CHECK(cpu.step(memory) == 1);
    CHECK((cpu.reg(0) & 2) != 0);

    bit.op = kr16::Op::Bclr;
    emit(memory, 0x0108, bit);
    CHECK(cpu.step(memory) == 1);
    CHECK((cpu.reg(0) & 2) == 0);

    cpu.set_pc(0x010A);
    cpu.set_flag(kr16::Cpu::FlagC, true);
    cpu.set_reg(0, 0xFFFF);
    cpu.set_reg(1, 0);
    emit(memory, 0x010A, alu(kr16::Op::AddCarry, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0);

    cpu.set_pc(0x010C);
    cpu.set_flag(kr16::Cpu::FlagC, false);
    cpu.set_reg(0, 5);
    cpu.set_reg(1, 3);
    emit(memory, 0x010C, alu(kr16::Op::SubBorrow, 0, 1));
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 1);

    cpu.set_pc(0x010E);
    cpu.set_reg(0, 5);
    kr16::Instruction neg;
    neg.op = kr16::Op::Neg;
    neg.rd = 0;
    emit(memory, 0x010E, neg);
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.reg(0) == 0xFFFB);
}

TEST_CASE("invalid MULS pair base takes vector 0") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    memory.write16(0, 0x0300);
    cpu.set_pc(0x0100);
    emit(memory, 0x0100, alu(kr16::Op::MulS, 1, 2));
    CHECK(cpu.step(memory) == 7);
    CHECK(cpu.pc() == 0x0300);
}

TEST_CASE("random instruction words decode without crashing") {
    std::uint32_t state = 0x4B523136u;
    for (int i = 0; i < 2000; ++i) {
        state = state * 1664525u + 1013904223u;
        const auto word = static_cast<std::uint16_t>(state >> 8);
        const auto instruction = kr16::Instruction::decode(word, word);
        CHECK(instruction.size_bytes >= 2);
    }
}

TEST_CASE("HLT and SYSCALL") {    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    cpu.set_pc(0x0100);
    bool called = false;
    cpu.set_syscall_handler([&called](std::uint8_t n, std::uint16_t,
                                     kr16::MemoryBus&) {
        called = true;
        CHECK(n == 3);
    });

    kr16::Instruction syscall;
    syscall.op = kr16::Op::Syscall;
    syscall.n = 3;
    emit(memory, 0x0100, syscall);
    CHECK(cpu.step(memory) == 10);
    CHECK(called);
    CHECK(cpu.pc() == 0x0102);

    kr16::Instruction hlt;
    hlt.op = kr16::Op::Hlt;
    emit(memory, 0x0102, hlt);
    CHECK(cpu.step(memory) == 1);
    CHECK(cpu.halted());
    CHECK(cpu.step(memory) == 0);
}

TEST_CASE("stack guard takes vector 5") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    memory.write16(10, 0x0300);
    cpu.set_pc(0x0100);
    cpu.set_sp(0xF000);
    cpu.set_stack_limit(0xF000);
    kr16::Instruction push;
    push.op = kr16::Op::Push;
    push.rd = 0;
    emit(memory, 0x0100, push);
    CHECK(cpu.step(memory) == 9);
    CHECK(cpu.pc() == 0x0300);
}

TEST_CASE("illegal instruction takes vector 0") {
    kr16::FlatMemory memory;
    kr16::Cpu cpu;
    memory.write16(0, 0x0200);
    cpu.set_pc(0x0100);
    memory.write16(0x0100, 0xD000);
    CHECK(cpu.step(memory) == 7);
    CHECK(cpu.pc() == 0x0200);
    CHECK(cpu.reg(5) == 0xEFFC);
}
