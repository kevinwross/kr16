#include "kr16/instruction.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "kr16/diagnostic.hpp"

namespace kr16 {
namespace {

std::int32_t sign_extend(std::uint32_t value, int bits) {
    const std::uint32_t sign = 1u << (bits - 1);
    const std::uint32_t mask = (1u << bits) - 1u;
    value &= mask;
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw ParseError(0, message);
    }
}

std::uint16_t r_type(std::uint8_t func, std::uint8_t rd, std::uint8_t rs) {
    require(rd < 8 && rs < 8, "register code out of range");
    return static_cast<std::uint16_t>((rd << 9) | (rs << 6) | func);
}

std::uint16_t i_type(std::uint8_t func, std::uint8_t rd, std::int32_t imm) {
    require(rd < 8, "register code out of range");
    require(imm >= -32 && imm <= 31, "6-bit immediate out of range");
    return static_cast<std::uint16_t>(0x1000u | (rd << 9) | (func << 6) |
                                      (static_cast<std::uint32_t>(imm) & 0x3F));
}

std::uint16_t m_type(std::uint8_t top, std::uint8_t rd, std::uint8_t rs,
                     std::int32_t disp) {
    require(rd < 8 && rs < 8, "register code out of range");
    require(disp >= -32 && disp <= 31, "6-bit displacement out of range");
    return static_cast<std::uint16_t>((top << 12) | (rd << 9) | (rs << 6) |
                                      (static_cast<std::uint32_t>(disp) & 0x3F));
}

std::uint16_t c_type(std::uint8_t subop, std::uint8_t reg_a,
                     std::uint8_t rest) {
    require(reg_a < 8 && rest < 32, "control field out of range");
    return static_cast<std::uint16_t>(0x8000u | (subop << 8) | (reg_a << 5) |
                                      rest);
}

std::uint16_t x_type(std::uint8_t ext, std::uint8_t rd, std::uint8_t aux) {
    require(rd < 8 && aux < 32, "extended field out of range");
    return static_cast<std::uint16_t>(0xF000u | (ext << 8) | (rd << 5) | aux);
}

std::uint8_t alu_r_func(Op op) {
    switch (op) {
        case Op::Add: return 0;
        case Op::AddCarry: return 1;
        case Op::Sub: return 2;
        case Op::SubBorrow: return 3;
        case Op::Cmp: return 4;
        case Op::And: return 5;
        case Op::Or: return 6;
        case Op::Xor: return 7;
        case Op::Mov: return 8;
        case Op::Test: return 9;
        case Op::Not: return 11;
        case Op::Neg: return 10;
        case Op::Inc: return 12;
        case Op::Dec: return 13;
        case Op::Shl: return 14;
        case Op::Shr: return 15;
        case Op::Sar: return 16;
        case Op::Mul: return 17;
        case Op::Div: return 18;
        case Op::DivU: return 19;
        case Op::Mod: return 20;
        case Op::ModU: return 21;
        case Op::Btst: return 22;
        case Op::Bset: return 23;
        case Op::Bclr: return 24;
        case Op::Btog: return 25;
        case Op::SignExtendByte: return 26;
        case Op::ZeroExtendByte: return 27;
        case Op::MulS: return 28;
        case Op::MulU: return 29;
        default: return 0xFF;
    }
}

std::uint8_t alu_i_func(Op op) {
    switch (op) {
        case Op::Add: return 0;
        case Op::Sub: return 1;
        case Op::Cmp: return 2;
        case Op::And: return 3;
        case Op::Or: return 4;
        case Op::Xor: return 5;
        case Op::Mov: return 6;
        case Op::Test: return 7;
        default: return 0xFF;
    }
}

std::uint8_t alu_x_ext(Op op) {
    switch (op) {
        case Op::Mov: return 0;
        case Op::Add: return 1;
        case Op::Sub: return 2;
        case Op::Cmp: return 3;
        case Op::And: return 4;
        case Op::Or: return 5;
        case Op::Xor: return 6;
        case Op::Test: return 7;
        default: return 0xFF;
    }
}

std::uint8_t memory_top(Op op) {
    switch (op) {
        case Op::Load: return 0x2;
        case Op::Store: return 0x3;
        case Op::LoadByte: return 0x4;
        case Op::LoadSignedByte: return 0x5;
        case Op::StoreByte: return 0x6;
        case Op::Lea: return 0xA;
        default: return 0xFF;
    }
}

std::uint8_t memory_abs_ext(Op op) {
    switch (op) {
        case Op::Load: return 8;
        case Op::Store: return 9;
        case Op::LoadByte: return 10;
        case Op::LoadSignedByte: return 11;
        case Op::StoreByte: return 12;
        default: return 0xFF;
    }
}

std::uint8_t memory_index_ext(Op op) {
    switch (op) {
        case Op::Load: return 13;
        case Op::Store: return 14;
        case Op::Lea: return 15;
        default: return 0xFF;
    }
}

Op r_decode(std::uint8_t func) {
    switch (func) {
        case 0: return Op::Add;
        case 1: return Op::AddCarry;
        case 2: return Op::Sub;
        case 3: return Op::SubBorrow;
        case 4: return Op::Cmp;
        case 5: return Op::And;
        case 6: return Op::Or;
        case 7: return Op::Xor;
        case 8: return Op::Mov;
        case 9: return Op::Test;
        case 10: return Op::Neg;
        case 11: return Op::Not;
        case 12: return Op::Inc;
        case 13: return Op::Dec;
        case 14: return Op::Shl;
        case 15: return Op::Shr;
        case 16: return Op::Sar;
        case 17: return Op::Mul;
        case 18: return Op::Div;
        case 19: return Op::DivU;
        case 20: return Op::Mod;
        case 21: return Op::ModU;
        case 22: return Op::Btst;
        case 23: return Op::Bset;
        case 24: return Op::Bclr;
        case 25: return Op::Btog;
        case 26: return Op::SignExtendByte;
        case 27: return Op::ZeroExtendByte;
        case 28: return Op::MulS;
        case 29: return Op::MulU;
        default: return Op::Invalid;
    }
}

Op i_decode(std::uint8_t func) {
    switch (func) {
        case 0: return Op::Add;
        case 1: return Op::Sub;
        case 2: return Op::Cmp;
        case 3: return Op::And;
        case 4: return Op::Or;
        case 5: return Op::Xor;
        case 6: return Op::Mov;
        case 7: return Op::Test;
        default: return Op::Invalid;
    }
}

Op x_decode(std::uint8_t ext) {
    switch (ext) {
        case 0: return Op::Mov;
        case 1: return Op::Add;
        case 2: return Op::Sub;
        case 3: return Op::Cmp;
        case 4: return Op::And;
        case 5: return Op::Or;
        case 6: return Op::Xor;
        case 7: return Op::Test;
        default: return Op::Invalid;
    }
}

}  // namespace

bool Instruction::first_word_is_32_bit(std::uint16_t first) {
    if ((first >> 12) == 0xF) {
        return true;
    }
    return (first >> 12) == 0x8 && ((first >> 8) & 0xF) <= 1;
}

std::vector<std::uint16_t> Instruction::encode() const {
    std::vector<std::uint16_t> words;
    switch (op) {
        case Op::Mov:
            if (!is_immediate) {
                words.push_back(r_type(8, rd, rs));
            } else if (force_long) {
                require(imm >= -32768 && imm <= 65535,
                        "16-bit immediate out of range");
                words.push_back(x_type(0, rd, 0));
                words.push_back(static_cast<std::uint16_t>(imm & 0xFFFF));
            } else if (imm >= -32 && imm <= 31) {
                words.push_back(i_type(6, rd, imm));
            } else if (imm >= 0 && imm <= 255) {
                words.push_back(static_cast<std::uint16_t>(
                    0x9000u | (rd << 9) | (static_cast<std::uint32_t>(imm) & 0xFF)));
            } else if (imm >= -128 && imm < 0) {
                words.push_back(static_cast<std::uint16_t>(
                    0x9100u | (rd << 9) | (static_cast<std::uint32_t>(imm) & 0xFF)));
            } else {
                require(imm >= -32768 && imm <= 65535,
                        "16-bit immediate out of range");
                words.push_back(x_type(0, rd, 0));
                words.push_back(static_cast<std::uint16_t>(imm & 0xFFFF));
            }
            break;
        case Op::Add:
        case Op::Sub:
        case Op::Cmp:
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::Test:
            if (!is_immediate) {
                words.push_back(r_type(alu_r_func(op), rd, rs));
            } else if (imm >= -32 && imm <= 31 && !force_long) {
                words.push_back(i_type(alu_i_func(op), rd, imm));
            } else {
                require(imm >= -32768 && imm <= 65535,
                        "16-bit immediate out of range");
                words.push_back(x_type(alu_x_ext(op), rd, 0));
                words.push_back(static_cast<std::uint16_t>(imm & 0xFFFF));
            }
            break;
        case Op::AddCarry:
        case Op::SubBorrow:
        case Op::Not:
        case Op::Neg:
        case Op::Inc:
        case Op::Dec:
        case Op::Mul:
        case Op::MulS:
        case Op::MulU:
        case Op::Div:
        case Op::DivU:
        case Op::Mod:
        case Op::ModU:
        case Op::SignExtendByte:
        case Op::ZeroExtendByte:
            words.push_back(r_type(alu_r_func(op), rd, rs));
            break;
        case Op::Shl:
        case Op::Shr:
        case Op::Sar:
            if (is_immediate) {
                require(imm >= 0 && imm <= 15, "shift count out of range");
                const std::uint8_t func =
                    op == Op::Shl ? 0 : (op == Op::Shr ? 1 : 2);
                words.push_back(static_cast<std::uint16_t>(
                    0xB000u | (rd << 9) | (func << 6) |
                    (static_cast<std::uint32_t>(imm) & 0x3F)));
            } else {
                words.push_back(r_type(alu_r_func(op), rd, rs));
            }
            break;
        case Op::Btst:
        case Op::Bset:
        case Op::Bclr:
        case Op::Btog:
            if (is_immediate) {
                require(imm >= 0 && imm <= 15, "bit index out of range");
                const std::uint8_t func =
                    op == Op::Btst ? 0 : (op == Op::Bset ? 1
                                           : (op == Op::Bclr ? 2 : 3));
                words.push_back(static_cast<std::uint16_t>(
                    0xC000u | (rd << 9) | (func << 6) |
                    (static_cast<std::uint32_t>(imm) & 0x3F)));
            } else {
                words.push_back(r_type(alu_r_func(op), rd, rs));
            }
            break;
        case Op::Load:
        case Op::LoadByte:
        case Op::LoadSignedByte:
        case Op::Store:
        case Op::StoreByte:
        case Op::Lea:
            if (absolute) {
                if (op == Op::Lea) {
                    throw ParseError(0, "LEA has no absolute form");
                }
                require(imm >= 0 && imm <= 0xFFFF, "address out of range");
                words.push_back(x_type(memory_abs_ext(op), rd, 0));
                words.push_back(static_cast<std::uint16_t>(imm));
            } else if (imm >= -32 && imm <= 31 && !force_long) {
                words.push_back(m_type(memory_top(op), rd, rs, imm));
            } else {
                const std::uint8_t ext = memory_index_ext(op);
                if (ext == 0xFF) {
                    throw ParseError(0, "byte indexed form requires a 6-bit displacement");
                }
                words.push_back(x_type(ext, rd, rs));
                words.push_back(static_cast<std::uint16_t>(imm & 0xFFFF));
            }
            break;
        case Op::Push:
            words.push_back(c_type(13, rd, 0));
            break;
        case Op::Pop:
            words.push_back(c_type(14, rd, 0));
            break;
        case Op::Bcc:
            require(cond < 16 && imm >= -256 && imm <= 254 && (imm % 2 == 0),
                    "branch displacement out of range");
            words.push_back(static_cast<std::uint16_t>(
                0x7000u | (cond << 8) |
                (static_cast<std::uint32_t>(imm / 2) & 0xFF)));
            break;
        case Op::JmpRel:
        case Op::CallRel:
            require(imm >= -32768 && imm <= 32767,
                    "relative jump out of range");
            words.push_back(c_type(op == Op::JmpRel ? 0 : 1, 0, 0));
            words.push_back(static_cast<std::uint16_t>(imm & 0xFFFF));
            break;
        case Op::JmpReg:
        case Op::CallReg:
            words.push_back(c_type(op == Op::JmpReg ? 2 : 3, rs, 0));
            break;
        case Op::Ret:
            words.push_back(c_type(4, 0, 0));
            break;
        case Op::Reti:
            words.push_back(c_type(5, 0, 0));
            break;
        case Op::Trap:
            require(n <= 119, "trap number out of range");
            words.push_back(static_cast<std::uint16_t>(0x8600u | n));
            break;
        case Op::Brk:
            words.push_back(c_type(7, 0, 0));
            break;
        case Op::Hlt:
            words.push_back(c_type(8, 0, 0));
            break;
        case Op::Nop:
            words.push_back(c_type(9, 0, 0));
            break;
        case Op::Ei:
            words.push_back(c_type(10, 0, 0));
            break;
        case Op::Di:
            words.push_back(c_type(11, 0, 0));
            break;
        case Op::Wait:
            words.push_back(c_type(12, 0, 0));
            break;
        case Op::Syscall:
            words.push_back(static_cast<std::uint16_t>(0x8F00u | n));
            break;
        case Op::Invalid:
            throw ParseError(0, "cannot encode an invalid instruction");
    }
    return words;
}

Instruction Instruction::decode(std::uint16_t first, std::uint16_t second) {
    Instruction instruction;
    const std::uint8_t top = static_cast<std::uint8_t>(first >> 12);
    switch (top) {
        case 0x0: {
            instruction.op = r_decode(first & 0x3F);
            instruction.rd = static_cast<std::uint8_t>((first >> 9) & 7);
            instruction.rs = static_cast<std::uint8_t>((first >> 6) & 7);
            instruction.size_bytes = 2;
            break;
        }
        case 0x1: {
            instruction.op = i_decode((first >> 6) & 7);
            instruction.rd = static_cast<std::uint8_t>((first >> 9) & 7);
            instruction.imm = sign_extend(first & 0x3F, 6);
            instruction.is_immediate = true;
            instruction.size_bytes = 2;
            break;
        }
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0xA: {
            const Op op = top == 0x2   ? Op::Load
                          : top == 0x3 ? Op::Store
                          : top == 0x4 ? Op::LoadByte
                          : top == 0x5 ? Op::LoadSignedByte
                          : top == 0x6 ? Op::StoreByte
                                       : Op::Lea;
            instruction.op = op;
            instruction.rd = static_cast<std::uint8_t>((first >> 9) & 7);
            instruction.rs = static_cast<std::uint8_t>((first >> 6) & 7);
            instruction.imm = sign_extend(first & 0x3F, 6);
            instruction.size_bytes = 2;
            break;
        }
        case 0x7: {
            instruction.op = Op::Bcc;
            instruction.cond = static_cast<std::uint8_t>((first >> 8) & 0xF);
            instruction.imm = sign_extend(first & 0xFF, 8) * 2;
            instruction.size_bytes = 2;
            break;
        }
        case 0x8: {
            const std::uint8_t subop = static_cast<std::uint8_t>((first >> 8) & 0xF);
            const std::uint8_t reg_a = static_cast<std::uint8_t>((first >> 5) & 7);
            instruction.size_bytes = 2;
            switch (subop) {
                case 0:
                case 1:
                    instruction.op = subop == 0 ? Op::JmpRel : Op::CallRel;
                    instruction.imm = sign_extend(second, 16);
                    instruction.size_bytes = 4;
                    break;
                case 2:
                case 3:
                    instruction.op = subop == 2 ? Op::JmpReg : Op::CallReg;
                    instruction.rs = reg_a;
                    instruction.size_bytes = 2;
                    break;
                case 4: instruction.op = Op::Ret; break;
                case 5: instruction.op = Op::Reti; break;
                case 6:
                    instruction.op = Op::Trap;
                    instruction.n = static_cast<std::uint8_t>(first & 0xFF);
                    if (instruction.n > 119) {
                        instruction.op = Op::Invalid;
                    }
                    break;
                case 7: instruction.op = Op::Brk; break;
                case 8: instruction.op = Op::Hlt; break;
                case 9: instruction.op = Op::Nop; break;
                case 10: instruction.op = Op::Ei; break;
                case 11: instruction.op = Op::Di; break;
                case 12: instruction.op = Op::Wait; break;
                case 13:
                    instruction.op = Op::Push;
                    instruction.rd = reg_a;
                    break;
                case 14:
                    instruction.op = Op::Pop;
                    instruction.rd = reg_a;
                    break;
                case 15:
                    instruction.op = Op::Syscall;
                    instruction.n = static_cast<std::uint8_t>(first & 0xFF);
                    break;
            }
            break;
        }
        case 0x9: {
            instruction.op = Op::Mov;
            instruction.rd = static_cast<std::uint8_t>((first >> 9) & 7);
            const bool sign = ((first >> 8) & 1) != 0;
            const std::uint8_t value = static_cast<std::uint8_t>(first & 0xFF);
            instruction.imm = sign ? sign_extend(value, 8) : value;
            instruction.is_immediate = true;
            instruction.size_bytes = 2;
            break;
        }
        case 0xB: {
            const std::uint8_t func = static_cast<std::uint8_t>((first >> 6) & 7);
            const std::uint8_t count = static_cast<std::uint8_t>(first & 0x3F);
            instruction.op = func == 0 ? Op::Shl
                              : func == 1 ? Op::Shr
                              : func == 2 ? Op::Sar
                                          : Op::Invalid;
            instruction.rd = static_cast<std::uint8_t>((first >> 9) & 7);
            instruction.imm = count;
            instruction.is_immediate = true;
            instruction.size_bytes = 2;
            if (count > 15) {
                instruction.op = Op::Invalid;
            }
            break;
        }
        case 0xC: {
            const std::uint8_t func = static_cast<std::uint8_t>((first >> 6) & 7);
            const std::uint8_t bit = static_cast<std::uint8_t>(first & 0x3F);
            instruction.op = func == 0 ? Op::Btst
                              : func == 1 ? Op::Bset
                              : func == 2 ? Op::Bclr
                              : func == 3 ? Op::Btog
                                          : Op::Invalid;
            instruction.rd = static_cast<std::uint8_t>((first >> 9) & 7);
            instruction.imm = bit;
            instruction.is_immediate = true;
            instruction.size_bytes = 2;
            if (bit > 15) {
                instruction.op = Op::Invalid;
            }
            break;
        }
        case 0xF: {
            const std::uint8_t ext = static_cast<std::uint8_t>((first >> 8) & 0xF);
            instruction.rd = static_cast<std::uint8_t>((first >> 5) & 7);
            const std::uint8_t aux = static_cast<std::uint8_t>(first & 0x1F);
            instruction.imm = second;
            instruction.size_bytes = 4;
            if (ext <= 7) {
                instruction.op = x_decode(ext);
                instruction.is_immediate = true;
                if (aux != 0) {
                    instruction.op = Op::Invalid;
                }
            } else if (ext <= 12) {
                instruction.op = ext == 8    ? Op::Load
                                  : ext == 9  ? Op::Store
                                  : ext == 10 ? Op::LoadByte
                                  : ext == 11 ? Op::LoadSignedByte
                                              : Op::StoreByte;
                instruction.absolute = true;
                if (aux != 0) {
                    instruction.op = Op::Invalid;
                }
            } else {
                instruction.op = ext == 13 ? Op::Load
                                  : ext == 14 ? Op::Store
                                              : Op::Lea;
                instruction.rs = aux;
                if (aux > 7) {
                    instruction.op = Op::Invalid;
                }
            }
            break;
        }
        default:
            instruction.op = Op::Invalid;
            break;
    }
    return instruction;
}

}  // namespace kr16
