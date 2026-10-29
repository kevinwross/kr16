#include "kr16/disassembler.hpp"

#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "kr16/instruction.hpp"

namespace kr16 {
namespace {

const char* register_name(std::uint8_t code) {
    static const char* names[] = {"R0", "R1", "R2", "R3", "R4", "SP", "SR", "PC"};
    return code < 8 ? names[code] : "?";
}

const char* condition_name(std::uint8_t code) {
    static const char* names[] = {"BEQ", "BNE", "BCS", "BCC", "BMI", "BPL",
                                  "BVS", "BVC", "BHI", "BLS", "BGE", "BLT",
                                  "BGT", "BLE", "BRA", "BNV"};
    return code < 16 ? names[code] : "B??";
}

const char* syscall_name(std::uint8_t number) {
    static const char* names[] = {
        "SYS_VERSION", "SYS_OPEN", "SYS_CLOSE", "SYS_READ", "SYS_WRITE",
        "SYS_SEEK",    "SYS_STAT", "SYS_UNLINK", "SYS_RENAME", "SYS_MKDIR",
        "SYS_READDIR", "SYS_TIME", "SYS_RANDOM", "SYS_FSINFO"};
    return number < 14 ? names[number] : nullptr;
}

std::string hex(std::uint32_t value, int width = 0) {
    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << std::setw(width)
         << std::setfill('0') << value;
    return text.str();
}

std::string op_name(Op op) {
    switch (op) {
        case Op::Mov: return "MOV";
        case Op::Load: return "LD";
        case Op::LoadByte: return "LDB";
        case Op::LoadSignedByte: return "LDSB";
        case Op::Store: return "ST";
        case Op::StoreByte: return "STB";
        case Op::Lea: return "LEA";
        case Op::Push: return "PUSH";
        case Op::Pop: return "POP";
        case Op::SignExtendByte: return "SEXT";
        case Op::ZeroExtendByte: return "ZXTB";
        case Op::Add: return "ADD";
        case Op::AddCarry: return "ADC";
        case Op::Sub: return "SUB";
        case Op::SubBorrow: return "SBC";
        case Op::Cmp: return "CMP";
        case Op::And: return "AND";
        case Op::Or: return "OR";
        case Op::Xor: return "XOR";
        case Op::Test: return "TEST";
        case Op::Not: return "NOT";
        case Op::Neg: return "NEG";
        case Op::Inc: return "INC";
        case Op::Dec: return "DEC";
        case Op::Mul: return "MUL";
        case Op::MulS: return "MULS";
        case Op::MulU: return "MULU";
        case Op::Div: return "DIV";
        case Op::DivU: return "DIVU";
        case Op::Mod: return "MOD";
        case Op::ModU: return "MODU";
        case Op::Shl: return "SHL";
        case Op::Shr: return "SHR";
        case Op::Sar: return "SAR";
        case Op::Btst: return "BTST";
        case Op::Bset: return "BSET";
        case Op::Bclr: return "BCLR";
        case Op::Btog: return "BTOG";
        case Op::Bcc: return "BCC";
        case Op::JmpRel: return "JMP";
        case Op::CallRel: return "CALL";
        case Op::JmpReg: return "JMP";
        case Op::CallReg: return "CALL";
        case Op::Ret: return "RET";
        case Op::Reti: return "RETI";
        case Op::Trap: return "TRAP";
        case Op::Brk: return "BRK";
        case Op::Hlt: return "HLT";
        case Op::Nop: return "NOP";
        case Op::Ei: return "EI";
        case Op::Di: return "DI";
        case Op::Wait: return "WAIT";
        case Op::Syscall: return "SYS";
        case Op::Invalid: return "???";
    }
    return "???";
}

std::string memory_operand(const Instruction& instruction) {
    std::ostringstream text;
    if (instruction.absolute) {
        text << "[" << hex(static_cast<std::uint16_t>(instruction.imm)) << "]";
        return text.str();
    }
    text << "[" << register_name(instruction.rs);
    if (instruction.imm > 0) {
        text << "+" << instruction.imm;
    } else if (instruction.imm < 0) {
        text << instruction.imm;
    }
    text << "]";
    return text.str();
}

std::string format_instruction(const Instruction& instruction,
                               std::uint16_t address,
                               const std::map<std::uint16_t, std::string>& labels) {
    auto target_label = [&labels](std::uint16_t target) {
        const auto it = labels.find(target);
        return it == labels.end() ? hex(target, 4) : it->second;
    };

    switch (instruction.op) {
        case Op::Mov:
            if (instruction.is_immediate) {
                return "MOV " + std::string(register_name(instruction.rd)) +
                       ", #" + hex(static_cast<std::uint16_t>(instruction.imm));
            }
            return "MOV " + std::string(register_name(instruction.rd)) + ", " +
                   register_name(instruction.rs);
        case Op::Load:
        case Op::LoadByte:
        case Op::LoadSignedByte:
        case Op::Lea:
            return std::string(op_name(instruction.op)) + " " +
                   register_name(instruction.rd) + ", " +
                   memory_operand(instruction);
        case Op::Store:
        case Op::StoreByte:
            return std::string(op_name(instruction.op)) + " " +
                   memory_operand(instruction) + ", " +
                   register_name(instruction.rd);
        case Op::Push:
        case Op::Pop:
            return std::string(op_name(instruction.op)) + " " +
                   register_name(instruction.rd);
        case Op::Add:
        case Op::AddCarry:
        case Op::Sub:
        case Op::SubBorrow:
        case Op::Cmp:
        case Op::And:
        case Op::Or:
        case Op::Xor:
        case Op::Test: {
            std::string text = op_name(instruction.op);
            text += " ";
            text += register_name(instruction.rd);
            text += ", ";
            if (instruction.is_immediate) {
                text += "#";
                text += hex(static_cast<std::uint16_t>(instruction.imm));
            } else {
                text += register_name(instruction.rs);
            }
            return text;
        }
        case Op::Not:
        case Op::Neg:
        case Op::Inc:
        case Op::Dec:
        case Op::SignExtendByte:
        case Op::ZeroExtendByte:
            return std::string(op_name(instruction.op)) + " " +
                   register_name(instruction.rd);
        case Op::Mul:
        case Op::MulS:
        case Op::MulU:
        case Op::Div:
        case Op::DivU:
        case Op::Mod:
        case Op::ModU:
            return std::string(op_name(instruction.op)) + " " +
                   register_name(instruction.rd) + ", " +
                   register_name(instruction.rs);
        case Op::Shl:
        case Op::Shr:
        case Op::Sar:
        case Op::Btst:
        case Op::Bset:
        case Op::Bclr:
        case Op::Btog: {
            std::string text = op_name(instruction.op);
            text += " ";
            text += register_name(instruction.rd);
            text += ", ";
            if (instruction.is_immediate) {
                text += "#";
                text += std::to_string(instruction.imm);
            } else {
                text += register_name(instruction.rs);
            }
            return text;
        }
        case Op::Bcc: {
            const auto target = static_cast<std::uint16_t>(
                address + instruction.size_bytes + instruction.imm);
            return std::string(condition_name(instruction.cond)) + " " +
                   target_label(target);
        }
        case Op::JmpRel:
        case Op::CallRel: {
            const auto target = static_cast<std::uint16_t>(
                address + instruction.size_bytes + instruction.imm * 2);
            return std::string(op_name(instruction.op)) + " " +
                   target_label(target);
        }
        case Op::JmpReg:
        case Op::CallReg:
            return std::string(op_name(instruction.op)) + " " +
                   register_name(instruction.rs);
        case Op::Trap:
            return "TRAP #" + std::to_string(instruction.n);
        case Op::Syscall: {
            const char* name = syscall_name(instruction.n);
            return name != nullptr ? std::string("SYS #") + name
                                   : "SYS #" + std::to_string(instruction.n);
        }
        case Op::Ret:
        case Op::Reti:
        case Op::Brk:
        case Op::Hlt:
        case Op::Nop:
        case Op::Ei:
        case Op::Di:
        case Op::Wait:
            return op_name(instruction.op);
        case Op::Invalid:
            return "???";
    }
    return "???";
}

std::string disassemble_bytes(std::span<const std::uint8_t> bytes,
                              std::uint16_t base,
                              const std::map<std::uint16_t, std::string>& labels,
                              bool show_bytes,
                              const std::string& section_name) {
    std::ostringstream out;
    if (!section_name.empty()) {
        out << section_name << ":\n";
    }
    std::size_t offset = 0;
    while (offset + 1 < bytes.size()) {
        const auto address = static_cast<std::uint16_t>(base + offset);
        const auto it = labels.find(address);
        if (it != labels.end()) {
            out << it->second << ":\n";
        }
        const std::uint16_t first = static_cast<std::uint16_t>(
            bytes[offset] | (bytes[offset + 1] << 8));
        const bool is_32 = Instruction::first_word_is_32_bit(first);
        const std::size_t length = is_32 ? 4 : 2;
        if (offset + length > bytes.size()) {
            out << "  " << hex(address, 4) << "  " << hex(first, 4)
                << "  .word\n";
            break;
        }
        const std::uint16_t second =
            is_32 ? static_cast<std::uint16_t>(bytes[offset + 2] |
                                               (bytes[offset + 3] << 8))
                  : 0;
        const Instruction instruction = Instruction::decode(first, second);
        out << "  " << hex(address, 4) << "  ";
        if (show_bytes) {
            out << hex(first, 4);
            if (is_32) {
                out << " " << hex(second, 4);
            } else {
                out << "     ";
            }
            out << "  ";
        }
        if (instruction.op == Op::Invalid) {
            out << ".word " << hex(first, 4);
        } else {
            out << format_instruction(instruction, address, labels);
        }
        out << "\n";
        offset += length;
    }
    return out.str();
}

}  // namespace

std::string disassemble_raw(std::span<const std::uint8_t> bytes,
                            const DisassemblyOptions& options) {
    return disassemble_bytes(bytes, options.base, {}, options.show_bytes, "");
}

std::string disassemble_container(const Container& container,
                                  const DisassemblyOptions& options) {
    std::map<std::uint16_t, std::string> labels;
    for (const auto& symbol : container.symbols) {
        if (symbol.section == 0 || symbol.section > container.sections.size()) {
            continue;
        }
        const auto& section = container.sections[symbol.section - 1];
        // A linked image stores absolute symbol addresses; object symbols are
        // relative to their section, whose placement address is usually zero.
        const auto address =
            container.kind == ContainerKind::Exec
                ? symbol.value
                : static_cast<std::uint16_t>(section.address + symbol.value);
        labels.emplace(address, symbol.name);
    }

    std::ostringstream out;
    for (const auto& section : container.sections) {
        if ((section.flags & SectionExec) == 0 ||
            section.type == SectionType::Nobits) {
            continue;
        }
        const auto base = section.address != 0 ? section.address : options.base;
        out << disassemble_bytes(section.data, base, labels,
                                 options.show_bytes, section.name);
    }
    if (out.str().empty()) {
        return "";
    }
    return out.str();
}

}  // namespace kr16
