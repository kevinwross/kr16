#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "kr16/diagnostic.hpp"
#include "kr16/instruction.hpp"

namespace {

kr16::Instruction roundtrip(const kr16::Instruction& instruction) {
    const auto words = instruction.encode();
    REQUIRE(!words.empty());
    CHECK(kr16::Instruction::first_word_is_32_bit(words[0]) ==
          (words.size() == 2));
    return kr16::Instruction::decode(words[0],
                                     words.size() == 2 ? words[1] : 0);
}

}  // namespace

TEST_CASE("documented instruction encodings decode correctly") {
    CHECK(kr16::Instruction::decode(0x0040).op == kr16::Op::Add);
    CHECK(kr16::Instruction::decode(0x0040).rd == 0);
    CHECK(kr16::Instruction::decode(0x0040).rs == 1);

    CHECK(kr16::Instruction::decode(0x1001).op == kr16::Op::Add);
    CHECK(kr16::Instruction::decode(0x1001).imm == 1);
    CHECK(kr16::Instruction::decode(0x1001).is_immediate);

    CHECK(kr16::Instruction::decode(0x9012).op == kr16::Op::Mov);
    CHECK(kr16::Instruction::decode(0x9012).imm == 0x12);

    const auto mov16 = kr16::Instruction::decode(0xF000, 0x1234);
    CHECK(mov16.op == kr16::Op::Mov);
    CHECK(mov16.size_bytes == 4);
    CHECK(mov16.imm == 0x1234);

    CHECK(kr16::Instruction::decode(0x2044).op == kr16::Op::Load);
    CHECK(kr16::Instruction::decode(0x2044).imm == 4);
    CHECK(kr16::Instruction::decode(0x3044).op == kr16::Op::Store);
    CHECK(kr16::Instruction::decode(0x8320).op == kr16::Op::CallReg);
    CHECK(kr16::Instruction::decode(0x8320).rs == 1);
    CHECK(kr16::Instruction::decode(0x8400).op == kr16::Op::Ret);
    CHECK(kr16::Instruction::decode(0x8F00).op == kr16::Op::Syscall);
    CHECK(kr16::Instruction::decode(0x8F00).n == 0);
}

TEST_CASE("representative instructions round-trip") {
    kr16::Instruction instruction;

    instruction = {};
    instruction.op = kr16::Op::Mov;
    instruction.rd = 3;
    instruction.rs = 4;
    CHECK(roundtrip(instruction).op == kr16::Op::Mov);

    instruction = {};
    instruction.op = kr16::Op::Mov;
    instruction.rd = 2;
    instruction.imm = 200;
    instruction.is_immediate = true;
    CHECK(roundtrip(instruction).imm == 200);

    instruction = {};
    instruction.op = kr16::Op::Add;
    instruction.rd = 1;
    instruction.rs = 2;
    CHECK(roundtrip(instruction).rs == 2);

    instruction = {};
    instruction.op = kr16::Op::Sub;
    instruction.rd = 1;
    instruction.imm = -100;
    instruction.is_immediate = true;
    CHECK(roundtrip(instruction).op == kr16::Op::Sub);

    instruction = {};
    instruction.op = kr16::Op::Shl;
    instruction.rd = 1;
    instruction.imm = 7;
    instruction.is_immediate = true;
    CHECK(roundtrip(instruction).imm == 7);

    instruction = {};
    instruction.op = kr16::Op::Btog;
    instruction.rd = 1;
    instruction.imm = 3;
    instruction.is_immediate = true;
    CHECK(roundtrip(instruction).op == kr16::Op::Btog);

    instruction = {};
    instruction.op = kr16::Op::LoadSignedByte;
    instruction.rd = 1;
    instruction.rs = 2;
    instruction.imm = -4;
    CHECK(roundtrip(instruction).op == kr16::Op::LoadSignedByte);

    instruction = {};
    instruction.op = kr16::Op::Store;
    instruction.rd = 3;
    instruction.imm = 0x1234;
    instruction.absolute = true;
    CHECK(roundtrip(instruction).absolute);

    instruction = {};
    instruction.op = kr16::Op::Bcc;
    instruction.cond = 12;
    instruction.imm = -8;
    CHECK(roundtrip(instruction).cond == 12);

    instruction = {};
    instruction.op = kr16::Op::JmpRel;
    instruction.imm = -4;
    CHECK(roundtrip(instruction).op == kr16::Op::JmpRel);

    instruction = {};
    instruction.op = kr16::Op::Trap;
    instruction.n = 5;
    CHECK(roundtrip(instruction).n == 5);

    instruction = {};
    instruction.op = kr16::Op::Push;
    instruction.rd = 4;
    CHECK(roundtrip(instruction).op == kr16::Op::Push);

    instruction = {};
    instruction.op = kr16::Op::Pop;
    instruction.rd = 4;
    CHECK(roundtrip(instruction).op == kr16::Op::Pop);

    instruction = {};
    instruction.op = kr16::Op::Syscall;
    instruction.n = 255;
    CHECK(roundtrip(instruction).n == 255);
}

TEST_CASE("instruction length rules") {
    CHECK(kr16::Instruction::first_word_is_32_bit(0xF000));
    CHECK(kr16::Instruction::first_word_is_32_bit(0x8000));
    CHECK(kr16::Instruction::first_word_is_32_bit(0x8100));
    CHECK_FALSE(kr16::Instruction::first_word_is_32_bit(0x8200));
    CHECK_FALSE(kr16::Instruction::first_word_is_32_bit(0x0040));
}

TEST_CASE("invalid encodings are rejected") {
    CHECK(kr16::Instruction::decode(0xD000).op == kr16::Op::Invalid);
    CHECK(kr16::Instruction::decode(0xE000).op == kr16::Op::Invalid);
    CHECK(kr16::Instruction::decode(0xF001, 0x0001).op == kr16::Op::Invalid);
    CHECK(kr16::Instruction::decode(0xF000 | (13 << 8) | (0 << 5) | 31).op ==
          kr16::Op::Invalid);

    kr16::Instruction branch;
    branch.op = kr16::Op::Bcc;
    branch.imm = 1;  // odd byte displacement
    CHECK_THROWS_AS(branch.encode(), kr16::ParseError);

    kr16::Instruction trap;
    trap.op = kr16::Op::Trap;
    trap.n = 120;
    CHECK_THROWS_AS(trap.encode(), kr16::ParseError);
}
