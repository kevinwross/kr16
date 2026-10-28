#pragma once

#include <cstdint>
#include <vector>

namespace kr16 {

enum class Op : std::uint16_t {
    Invalid = 0,
    Mov,
    Load,
    LoadByte,
    LoadSignedByte,
    Store,
    StoreByte,
    Lea,
    Push,
    Pop,
    SignExtendByte,
    ZeroExtendByte,
    Add,
    AddCarry,
    Sub,
    SubBorrow,
    Cmp,
    And,
    Or,
    Xor,
    Test,
    Not,
    Neg,
    Inc,
    Dec,
    Mul,
    MulS,
    MulU,
    Div,
    DivU,
    Mod,
    ModU,
    Shl,
    Shr,
    Sar,
    Btst,
    Bset,
    Bclr,
    Btog,
    Bcc,
    JmpRel,
    CallRel,
    JmpReg,
    CallReg,
    Ret,
    Reti,
    Trap,
    Brk,
    Hlt,
    Nop,
    Ei,
    Di,
    Wait,
    Syscall,
};

struct Instruction {
    Op op = Op::Invalid;
    std::uint8_t rd = 0;
    std::uint8_t rs = 0;
    std::int32_t imm = 0;
    std::uint8_t cond = 0;
    std::uint8_t n = 0;
    bool is_immediate = false;
    bool absolute = false;
    bool force_long = false;
    std::uint8_t size_bytes = 2;

    std::vector<std::uint16_t> encode() const;
    static Instruction decode(std::uint16_t first, std::uint16_t second = 0);
    static bool first_word_is_32_bit(std::uint16_t first);
};

}  // namespace kr16
