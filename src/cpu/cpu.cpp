#include "kr16/cpu.hpp"

#include <cstdint>
#include <utility>

#include "kr16/diagnostic.hpp"
#include "kr16/instruction.hpp"

namespace kr16 {
namespace {

std::int16_t as_i16(std::uint16_t value) {
    return static_cast<std::int16_t>(value);
}

std::uint16_t as_u16(std::int32_t value) {
    return static_cast<std::uint16_t>(value & 0xFFFF);
}

}  // namespace

Cpu::Cpu() { reset(); }

void Cpu::reset() {
    regs_[0] = 0;
    regs_[1] = 0;
    regs_[2] = 0;
    regs_[3] = 0;
    regs_[4] = 0;
    pc_ = 0;
    sp_ = 0xF000;
    sr_ = 0;
    halted_ = false;
    halt_reason_.clear();
    cycles_ = 0;
}

std::uint16_t Cpu::reg(std::uint8_t code) const {
    if (code < 5) {
        return regs_[code];
    }
    switch (code) {
        case 5: return sp_;
        case 6: return static_cast<std::uint16_t>(sr_ & FlagMask);
        case 7: return pc_;
        default: throw ParseError(0, "invalid register code");
    }
}

void Cpu::set_reg(std::uint8_t code, std::uint16_t value) {
    if (code < 5) {
        regs_[code] = value;
        return;
    }
    switch (code) {
        case 5: sp_ = value; return;
        case 6: sr_ = static_cast<std::uint16_t>(value & FlagMask); return;
        case 7: pc_ = value; return;
        default: throw ParseError(0, "invalid register code");
    }
}

void Cpu::push16(std::uint16_t value, MemoryBus& bus) {
    sp_ = static_cast<std::uint16_t>(sp_ - 2);
    bus.write16(sp_, value);
}

std::uint16_t Cpu::pop16(MemoryBus& bus) {
    const std::uint16_t value = bus.read16(sp_);
    sp_ = static_cast<std::uint16_t>(sp_ + 2);
    return value;
}

bool Cpu::condition(std::uint8_t code) const {
    const bool c = flag(FlagC);
    const bool z = flag(FlagZ);
    const bool n = flag(FlagN);
    const bool v = flag(FlagV);
    switch (code) {
        case 0: return z;
        case 1: return !z;
        case 2: return c;
        case 3: return !c;
        case 4: return n;
        case 5: return !n;
        case 6: return v;
        case 7: return !v;
        case 8: return c && !z;
        case 9: return !c || z;
        case 10: return n == v;
        case 11: return n != v;
        case 12: return !z && n == v;
        case 13: return z || n != v;
        case 14: return true;
        case 15: return false;
        default: return false;
    }
}

void Cpu::raise_exception(std::uint16_t vector, std::uint16_t saved_pc,
                          MemoryBus& bus) {
    push16(sr_, bus);
    push16(saved_pc, bus);
    sr_ &= static_cast<std::uint16_t>(~FlagI);
    const std::uint16_t handler = bus.read16(static_cast<std::uint16_t>(vector * 2));
    if (handler == 0) {
        halted_ = true;
        halt_reason_ = "unhandled exception " + std::to_string(vector);
        return;
    }
    pc_ = handler;
}

std::uint32_t Cpu::step(MemoryBus& bus) {
    if (halted_) {
        return 0;
    }
    if (flag(FlagI) && interrupt_ack_) {
        const auto vector = interrupt_ack_();
        if (vector.has_value()) {
            raise_exception(*vector, pc_, bus);
            cycles_ += 7;
            return 7;
        }
    }
    const std::uint16_t instruction_pc = pc_;
    if ((instruction_pc & 1u) != 0) {
        raise_exception(5, instruction_pc, bus);
        cycles_ += 7;
        return 7;
    }

    const std::uint16_t first = bus.read16(instruction_pc);
    const bool is_32 = Instruction::first_word_is_32_bit(first);
    const std::uint16_t second =
        is_32 ? bus.read16(static_cast<std::uint16_t>(instruction_pc + 2)) : 0;
    const Instruction instruction = Instruction::decode(first, second);
    if (instruction.op == Op::Invalid) {
        raise_exception(0, instruction_pc, bus);
        cycles_ += 7;
        return 7;
    }

    const std::uint16_t next_pc = static_cast<std::uint16_t>(
        instruction_pc + instruction.size_bytes);
    pc_ = next_pc;
    std::uint32_t cycles = 1;
    execute(instruction, bus, next_pc, cycles);
    cycles_ += cycles;
    if (!halted_ && (sp_ < stack_limit_ || sp_ > 0xF000)) {
        raise_exception(5, next_pc, bus);
        cycles_ += 7;
        return cycles + 7;
    }
    return cycles;
}

void Cpu::execute(const Instruction& instruction, MemoryBus& bus,
                  std::uint16_t next_pc, std::uint32_t& cycles) {
    const auto fault_pc = static_cast<std::uint16_t>(
        next_pc - instruction.size_bytes);

    auto set_nz = [this](std::uint16_t value) {
        set_flag(FlagZ, value == 0);
        set_flag(FlagN, (value & 0x8000) != 0);
    };

    auto add_flags = [this, &set_nz](std::uint16_t a, std::uint16_t b,
                                     std::uint16_t carry,
                                     std::uint16_t result) {
        const std::int32_t signed_sum =
            static_cast<std::int32_t>(as_i16(a)) + as_i16(b) + carry;
        const std::uint32_t unsigned_sum =
            static_cast<std::uint32_t>(a) + b + carry;
        set_flag(FlagC, unsigned_sum > 0xFFFF);
        set_flag(FlagV, signed_sum != static_cast<std::int32_t>(as_i16(result)));
        set_nz(result);
    };

    auto sub_flags = [this, &set_nz](std::uint16_t a, std::uint16_t b,
                                     std::uint16_t borrow,
                                     std::uint16_t result) {
        const std::int32_t signed_diff =
            static_cast<std::int32_t>(as_i16(a)) - as_i16(b) - borrow;
        const std::uint32_t subtrahend =
            static_cast<std::uint32_t>(b) + borrow;
        set_flag(FlagC, static_cast<std::uint32_t>(a) >= subtrahend);
        set_flag(FlagV, signed_diff !=
                            static_cast<std::int32_t>(as_i16(result)));
        set_nz(result);
    };

    auto write_result = [this](std::uint8_t rd, std::uint16_t value) {
        set_reg(rd, value);
    };

    auto operand_b = [this, &instruction]() -> std::uint16_t {
        return instruction.is_immediate
                   ? static_cast<std::uint16_t>(instruction.imm & 0xFFFF)
                   : reg(instruction.rs);
    };

    auto memory_address = [this, &instruction, next_pc]() -> std::uint16_t {
        if (instruction.absolute) {
            return static_cast<std::uint16_t>(instruction.imm & 0xFFFF);
        }
        const std::uint16_t base =
            instruction.rs == 7 ? next_pc : reg(instruction.rs);
        return static_cast<std::uint16_t>(base + instruction.imm);
    };

    switch (instruction.op) {
        case Op::Mov:
            if (instruction.is_immediate) {
                set_reg(instruction.rd,
                        static_cast<std::uint16_t>(instruction.imm & 0xFFFF));
            } else {
                set_reg(instruction.rd, reg(instruction.rs));
            }
            cycles = 1 + (instruction.size_bytes == 4 ? 1 : 0);
            return;

        case Op::Add:
        case Op::AddCarry:
        case Op::Sub:
        case Op::SubBorrow:
        case Op::Cmp:
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::Test: {
            const std::uint16_t a = reg(instruction.rd);
            const std::uint16_t b = operand_b();
            std::uint16_t result = 0;
            if (instruction.op == Op::Add || instruction.op == Op::AddCarry) {
                const std::uint16_t carry =
                    instruction.op == Op::AddCarry ? flag(FlagC) : 0;
                const std::uint32_t sum =
                    static_cast<std::uint32_t>(a) + b + carry;
                result = static_cast<std::uint16_t>(sum);
                add_flags(a, b, carry, result);
                if (instruction.op == Op::Add ||
                    instruction.op == Op::AddCarry) {
                    write_result(instruction.rd, result);
                }
            } else if (instruction.op == Op::Sub ||
                       instruction.op == Op::SubBorrow ||
                       instruction.op == Op::Cmp) {
                const std::uint16_t borrow =
                    instruction.op == Op::SubBorrow ? (flag(FlagC) ? 0 : 1) : 0;
                const std::uint32_t diff =
                    static_cast<std::uint32_t>(a) - b - borrow;
                result = static_cast<std::uint16_t>(diff);
                sub_flags(a, b, borrow, result);
                if (instruction.op != Op::Cmp) {
                    write_result(instruction.rd, result);
                }
            } else {
                if (instruction.op == Op::And || instruction.op == Op::Test) {
                    result = static_cast<std::uint16_t>(a & b);
                } else if (instruction.op == Op::Or) {
                    result = static_cast<std::uint16_t>(a | b);
                } else {
                    result = static_cast<std::uint16_t>(a ^ b);
                }
                set_flag(FlagC, false);
                set_flag(FlagV, false);
                set_nz(result);
                if (instruction.op != Op::Test) {
                    write_result(instruction.rd, result);
                }
            }
            cycles = 1 + (instruction.size_bytes == 4 ? 1 : 0);
            return;
        }

        case Op::Not: {
            const std::uint16_t result = static_cast<std::uint16_t>(~reg(instruction.rd));
            set_flag(FlagC, false);
            set_flag(FlagV, false);
            set_nz(result);
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }
        case Op::Neg: {
            const std::uint16_t a = reg(instruction.rd);
            const std::uint16_t result = as_u16(-static_cast<std::int32_t>(a));
            sub_flags(0, a, 0, result);
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }
        case Op::Inc:
        case Op::Dec: {
            const std::uint16_t a = reg(instruction.rd);
            const std::uint16_t result =
                instruction.op == Op::Inc ? static_cast<std::uint16_t>(a + 1)
                                           : static_cast<std::uint16_t>(a - 1);
            if (instruction.op == Op::Inc) {
                add_flags(a, 1, 0, result);
            } else {
                sub_flags(a, 1, 0, result);
            }
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }

        case Op::Mul: {
            const std::uint32_t product =
                static_cast<std::uint32_t>(reg(instruction.rd)) *
                static_cast<std::uint32_t>(reg(instruction.rs));
            const std::uint16_t result = static_cast<std::uint16_t>(product);
            set_flag(FlagC, false);
            set_flag(FlagV, false);
            set_nz(result);
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }
        case Op::MulS:
        case Op::MulU: {
            if ((instruction.rd != 0 && instruction.rd != 2) ||
                instruction.rs > 4) {
                raise_exception(0, fault_pc, bus);
                cycles = 7;
                return;
            }
            const std::uint16_t a = reg(instruction.rd);
            const std::uint16_t b = reg(instruction.rs);
            const std::uint32_t product =
                instruction.op == Op::MulS
                    ? static_cast<std::uint32_t>(
                          static_cast<std::int32_t>(as_i16(a)) *
                          static_cast<std::int32_t>(as_i16(b)))
                    : static_cast<std::uint32_t>(a) *
                          static_cast<std::uint32_t>(b);
            const std::uint16_t low = static_cast<std::uint16_t>(product);
            const std::uint16_t high = static_cast<std::uint16_t>(product >> 16);
            set_reg(instruction.rd, low);
            set_reg(static_cast<std::uint8_t>(instruction.rd + 1), high);
            set_flag(FlagC, false);
            set_flag(FlagV, false);
            set_flag(FlagZ, product == 0);
            set_flag(FlagN, (product & 0x80000000u) != 0);
            cycles = 1;
            return;
        }
        case Op::Div:
        case Op::DivU:
        case Op::Mod:
        case Op::ModU: {
            const std::uint16_t divisor = reg(instruction.rs);
            if (divisor == 0) {
                raise_exception(2, fault_pc, bus);
                cycles = 7;
                return;
            }
            std::uint16_t result = 0;
            if (instruction.op == Op::Div || instruction.op == Op::Mod) {
                const std::int32_t dividend = as_i16(reg(instruction.rd));
                const std::int32_t signed_divisor = as_i16(divisor);
                if (instruction.op == Op::Div) {
                    result = as_u16(dividend / signed_divisor);
                } else {
                    result = as_u16(dividend % signed_divisor);
                }
            } else {
                const std::uint16_t dividend = reg(instruction.rd);
                if (instruction.op == Op::DivU) {
                    result = static_cast<std::uint16_t>(dividend / divisor);
                } else {
                    result = static_cast<std::uint16_t>(dividend % divisor);
                }
            }
            set_flag(FlagC, false);
            set_flag(FlagV, false);
            set_nz(result);
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }

        case Op::Shl:
        case Op::Shr:
        case Op::Sar: {
            const std::uint16_t a = reg(instruction.rd);
            const std::uint8_t count = static_cast<std::uint8_t>(
                instruction.is_immediate ? (instruction.imm & 0x0F)
                                         : (reg(instruction.rs) & 0x0F));
            if (count == 0) {
                cycles = 1;
                return;
            }
            std::uint16_t result = 0;
            bool carry = false;
            if (instruction.op == Op::Shl) {
                carry = ((a >> (16 - count)) & 1u) != 0;
                result = static_cast<std::uint16_t>(a << count);
            } else if (instruction.op == Op::Shr) {
                carry = ((a >> (count - 1)) & 1u) != 0;
                result = static_cast<std::uint16_t>(a >> count);
            } else {
                carry = ((a >> (count - 1)) & 1u) != 0;
                result = static_cast<std::uint16_t>(as_i16(a) >> count);
            }
            set_flag(FlagC, carry);
            set_flag(FlagV, false);
            set_nz(result);
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }

        case Op::Btst:
        case Op::Bset:
        case Op::Bclr:
        case Op::Btog: {
            const std::uint16_t a = reg(instruction.rd);
            const std::uint8_t bit = static_cast<std::uint8_t>(
                instruction.is_immediate ? (instruction.imm & 0x0F)
                                         : (reg(instruction.rs) & 0x0F));
            const bool set = ((a >> bit) & 1u) != 0;
            if (instruction.op == Op::Btst) {
                set_flag(FlagZ, !set);
            } else {
                std::uint16_t result = a;
                if (instruction.op == Op::Bset) {
                    result = static_cast<std::uint16_t>(a | (1u << bit));
                } else if (instruction.op == Op::Bclr) {
                    result = static_cast<std::uint16_t>(a & ~(1u << bit));
                } else {
                    result = static_cast<std::uint16_t>(a ^ (1u << bit));
                }
                write_result(instruction.rd, result);
            }
            cycles = 1;
            return;
        }

        case Op::SignExtendByte: {
            const std::uint16_t result = static_cast<std::uint16_t>(
                static_cast<std::int16_t>(
                    static_cast<std::int8_t>(reg(instruction.rd) & 0xFF)));
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }
        case Op::ZeroExtendByte: {
            const std::uint16_t result =
                static_cast<std::uint16_t>(reg(instruction.rd) & 0xFF);
            write_result(instruction.rd, result);
            cycles = 1;
            return;
        }

        case Op::Load:
        case Op::LoadByte:
        case Op::LoadSignedByte:
        case Op::Lea: {
            if (instruction.rd == 7 || instruction.rd == 6) {
                raise_exception(0, fault_pc, bus);
                cycles = 7;
                return;
            }
            const std::uint16_t address = memory_address();
            std::uint16_t value = 0;
            if (instruction.op == Op::Lea) {
                value = address;
                cycles = 2;
            } else if (instruction.op == Op::Load) {
                value = bus.read16(address);
                cycles = 2 + ((address & 1u) != 0 ? 1 : 0);
            } else if (instruction.op == Op::LoadByte) {
                value = bus.read8(address);
                cycles = 2;
            } else {
                value = static_cast<std::uint16_t>(
                    static_cast<std::int16_t>(
                        static_cast<std::int8_t>(bus.read8(address))));
                cycles = 2;
            }
            write_result(instruction.rd, value);
            return;
        }

        case Op::Store:
        case Op::StoreByte: {
            const std::uint16_t address = memory_address();
            const std::uint16_t value =
                instruction.rd == 7 ? next_pc : reg(instruction.rd);
            if (instruction.op == Op::Store) {
                bus.write16(address, value);
                cycles = 2 + ((address & 1u) != 0 ? 1 : 0);
            } else {
                bus.write8(address, static_cast<std::uint8_t>(value & 0xFF));
                cycles = 2;
            }
            return;
        }

        case Op::Push: {
            const std::uint16_t value = reg(instruction.rd);
            push16(value, bus);
            cycles = 2;
            return;
        }
        case Op::Pop: {
            const std::uint16_t value = pop16(bus);
            set_reg(instruction.rd, value);
            cycles = 2;
            return;
        }

        case Op::Bcc: {
            if (condition(instruction.cond)) {
                pc_ = static_cast<std::uint16_t>(next_pc + instruction.imm);
                cycles = 2;
            } else {
                cycles = 1;
            }
            return;
        }
        case Op::JmpRel: {
            pc_ = static_cast<std::uint16_t>(next_pc +
                                             instruction.imm * 2);
            cycles = 3;
            return;
        }
        case Op::CallRel: {
            push16(next_pc, bus);
            pc_ = static_cast<std::uint16_t>(next_pc +
                                             instruction.imm * 2);
            cycles = 4;
            return;
        }
        case Op::JmpReg:
            pc_ = reg(instruction.rs);
            cycles = 2;
            return;
        case Op::CallReg: {
            const std::uint16_t target = reg(instruction.rs);
            push16(next_pc, bus);
            pc_ = target;
            cycles = 3;
            return;
        }
        case Op::Ret:
            pc_ = pop16(bus);
            cycles = 3;
            return;
        case Op::Reti:
            pc_ = pop16(bus);
            set_sr(pop16(bus));
            cycles = 5;
            return;
        case Op::Trap: {
            if (instruction.n > 119) {
                raise_exception(0, fault_pc, bus);
                cycles = 7;
                return;
            }
            const std::uint16_t handler = bus.read16(
                static_cast<std::uint16_t>(0x0010 + 2 * instruction.n));
            if (handler == 0) {
                halted_ = true;
                halt_reason_ = "unhandled trap";
                cycles = 4;
                return;
            }
            push16(next_pc, bus);
            pc_ = handler;
            cycles = 4;
            return;
        }
        case Op::Brk:
            raise_exception(1, next_pc, bus);
            cycles = 7;
            return;
        case Op::Hlt:
            halted_ = true;
            halt_reason_ = "HLT";
            cycles = 1;
            return;
        case Op::Nop:
            cycles = 1;
            return;
        case Op::Ei:
            set_flag(FlagI, true);
            cycles = 1;
            return;
        case Op::Di:
            set_flag(FlagI, false);
            cycles = 1;
            return;
        case Op::Wait:
            cycles = 1;
            return;
        case Op::Syscall:
            if (syscall_handler_) {
                syscall_handler_(instruction.n);
            }
            cycles = 10;
            return;
        case Op::Invalid:
            break;
    }

    raise_exception(0, fault_pc, bus);
    cycles = 7;
}

}  // namespace kr16
