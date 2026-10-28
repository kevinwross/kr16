#include "help.hpp"

#include <cctype>
#include <cstddef>
#include <string>

namespace kr16::ui {
namespace {

std::string trim_copy(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool is_register_operand(const std::string& operand) {
    if (operand == "SP" || operand == "SR" || operand == "PC") {
        return true;
    }
    return operand.size() == 2 && operand[0] == 'R' &&
           operand[1] >= '0' && operand[1] <= '4';
}

bool is_branch_mnemonic(const std::string& mnemonic) {
    if (mnemonic == "JMP" || mnemonic == "CALL") {
        return true;
    }
    static const char* names[] = {
        "BEQ", "BNE", "BCS", "BCC", "BMI", "BPL", "BVS", "BVC",
        "BHI", "BLS", "BGE", "BLT", "BGT", "BLE", "BRA", "BNV"};
    for (const char* name : names) {
        if (mnemonic == name) {
            return true;
        }
    }
    return false;
}

std::string describe_operand(const std::string& raw,
                             const std::string& mnemonic) {
    const std::string operand = trim_copy(raw);
    if (operand.empty()) {
        return {};
    }
    if (operand.front() == '#') {
        return mnemonic == "SYSCALL"
                   ? "immediate host-service number"
                   : "immediate value (encoded in the instruction)";
    }
    if (operand.front() == '[' && operand.back() == ']') {
        const std::string inner =
            trim_copy(operand.substr(1, operand.size() - 2));
        if (is_register_operand(inner)) {
            return "register indirect (the register holds the address)";
        }
        const std::size_t sign = inner.find_first_of("+-");
        if (sign != std::string::npos &&
            is_register_operand(trim_copy(inner.substr(0, sign)))) {
            return "register indirect with displacement "
                   "(address = register +/- displacement)";
        }
        return "absolute memory address (constant address)";
    }
    if (is_register_operand(operand)) {
        if (operand == "SP" || operand == "SR" || operand == "PC") {
            return "special register (direct)";
        }
        if (mnemonic == "JMP" || mnemonic == "CALL") {
            return "register target (PC is loaded from the register)";
        }
        return "register direct (the register itself)";
    }
    if (operand.rfind("0x", 0) == 0 ||
        (operand.front() >= '0' && operand.front() <= '9')) {
        return is_branch_mnemonic(mnemonic)
                   ? "PC-relative target (offset from the next instruction)"
                   : "absolute value";
    }
    return "other operand";
}

}  // namespace

std::string instruction_help(const std::string& mnemonic) {
    struct Entry {
        const char* mnemonic;
        const char* text;
    };
    static const Entry kEntries[] = {
        {"MOV", "Copy the source value into the destination register."},
        {"LD", "Load a 16-bit word from memory into a register."},
        {"LDB", "Load an unsigned byte from memory into a register."},
        {"LDSB", "Load a signed byte from memory and sign-extend it."},
        {"ST", "Store a 16-bit register value to memory."},
        {"STB", "Store the low byte of a register to memory."},
        {"LEA", "Compute the address of a memory operand into a register."},
        {"PUSH", "Push a register onto the stack; SP decreases by 2."},
        {"POP", "Pop the top of the stack into a register; SP increases by 2."},
        {"SEXT", "Sign-extend the low byte of a register to 16 bits."},
        {"ZXTB", "Zero-extend the low byte of a register to 16 bits."},
        {"ADD", "Add the source to the destination and set the flags."},
        {"ADC", "Add the source and the carry flag to the destination."},
        {"SUB", "Subtract the source from the destination and set the flags."},
        {"SBC", "Subtract the source and the borrow from the destination."},
        {"CMP", "Compare by subtracting; set the flags and discard the result."},
        {"AND", "Bitwise AND the source into the destination."},
        {"OR", "Bitwise OR the source into the destination."},
        {"XOR", "Bitwise XOR the source into the destination."},
        {"TEST", "Bitwise AND to set the flags without changing the destination."},
        {"NOT", "Bitwise complement the register."},
        {"NEG", "Two's-complement negate the register."},
        {"INC", "Increment the register by 1."},
        {"DEC", "Decrement the register by 1."},
        {"MUL", "Multiply, keeping the low 16 bits of the result."},
        {"MULS", "Signed multiply, keeping the low 16 bits of the result."},
        {"MULU", "Unsigned multiply, keeping the low 16 bits of the result."},
        {"DIV", "Signed divide; the quotient goes in the destination."},
        {"DIVU", "Unsigned divide; the quotient goes in the destination."},
        {"MOD", "Signed remainder of a division."},
        {"MODU", "Unsigned remainder of a division."},
        {"SHL", "Shift the register left, filling with zero."},
        {"SHR", "Logical shift right, filling with zero."},
        {"SAR", "Arithmetic shift right, preserving the sign bit."},
        {"BTST", "Test a bit and set the Z flag."},
        {"BSET", "Set a bit in the destination."},
        {"BCLR", "Clear a bit in the destination."},
        {"BTOG", "Toggle a bit in the destination."},
        {"BEQ", "Branch if equal: Z = 1."},
        {"BNE", "Branch if not equal: Z = 0."},
        {"BCS", "Branch if carry set: C = 1."},
        {"BCC", "Branch if carry clear: C = 0."},
        {"BMI", "Branch if negative: N = 1."},
        {"BPL", "Branch if positive or zero: N = 0."},
        {"BVS", "Branch if signed overflow: V = 1."},
        {"BVC", "Branch if no signed overflow: V = 0."},
        {"BHI", "Branch if higher (unsigned): C = 1 and Z = 0."},
        {"BLS", "Branch if lower or same (unsigned): C = 0 or Z = 1."},
        {"BGE", "Branch if greater or equal (signed): N = V."},
        {"BLT", "Branch if less than (signed): N != V."},
        {"BGT", "Branch if greater than (signed): Z = 0 and N = V."},
        {"BLE", "Branch if less or equal (signed): Z = 1 or N != V."},
        {"BRA", "Branch unconditionally."},
        {"BNV", "Branch on condition code 15 (BNV); reserved in the teaching subset."},
        {"JMP", "Jump to the target without pushing a return address."},
        {"CALL", "Call a subroutine: push the return address, then jump."},
        {"RET", "Return from a subroutine by popping the return address."},
        {"RETI", "Return from an interrupt, restoring PC and SR."},
        {"TRAP", "Raise a software exception."},
        {"BRK", "Breakpoint: stop the debugger, otherwise take the vector."},
        {"HLT", "Halt the CPU."},
        {"NOP", "Do nothing."},
        {"EI", "Enable interrupts."},
        {"DI", "Disable interrupts."},
        {"WAIT", "Wait for an interrupt."},
        {"SYSCALL", "Call a host service."},
        {"???", "Unknown or invalid instruction."},
    };
    for (const auto& entry : kEntries) {
        if (mnemonic == entry.mnemonic) {
            return entry.text;
        }
    }
    return "No description is available for this instruction yet.";
}

std::string addressing_help(const std::string& instruction,
                            const std::string& mnemonic) {
    const std::size_t space = instruction.find(' ');
    if (space == std::string::npos) {
        return "This instruction has no operands.";
    }
    const std::string operands = instruction.substr(space + 1);
    std::string result;
    bool first = true;
    std::size_t begin = 0;
    while (begin <= operands.size()) {
        const std::size_t comma = operands.find(',', begin);
        const std::string piece = trim_copy(operands.substr(
            begin, comma == std::string::npos ? std::string::npos
                                              : comma - begin));
        if (!piece.empty()) {
            if (!first) {
                result += "; ";
            }
            result += piece + ": " + describe_operand(piece, mnemonic);
            first = false;
        }
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return first ? "This instruction has no operands." : result;
}

}  // namespace kr16::ui
