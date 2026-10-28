#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace kr16 {

class MemoryBus {
public:
    virtual ~MemoryBus() = default;
    virtual std::uint8_t read8(std::uint16_t address) = 0;
    virtual void write8(std::uint16_t address, std::uint8_t value) = 0;

    virtual std::uint16_t read16(std::uint16_t address) {
        const auto low = static_cast<std::uint16_t>(read8(address));
        const auto high = static_cast<std::uint16_t>(
            read8(static_cast<std::uint16_t>(address + 1)));
        return static_cast<std::uint16_t>(low | (high << 8));
    }

    virtual void write16(std::uint16_t address, std::uint16_t value) {
        write8(address, static_cast<std::uint8_t>(value & 0xFF));
        write8(static_cast<std::uint16_t>(address + 1),
               static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }
};

class FlatMemory : public MemoryBus {
public:
    FlatMemory() { bytes_.fill(0); }

    std::uint8_t read8(std::uint16_t address) override {
        return bytes_[address];
    }

    void write8(std::uint16_t address, std::uint8_t value) override {
        bytes_[address] = value;
    }

    void fill(std::uint8_t value) { bytes_.fill(value); }

private:
    std::array<std::uint8_t, 65536> bytes_{};
};

class Cpu {
public:
    static constexpr std::uint16_t FlagC = 1u << 0;
    static constexpr std::uint16_t FlagZ = 1u << 1;
    static constexpr std::uint16_t FlagN = 1u << 2;
    static constexpr std::uint16_t FlagV = 1u << 3;
    static constexpr std::uint16_t FlagI = 1u << 4;
    static constexpr std::uint16_t FlagMask = FlagC | FlagZ | FlagN | FlagV | FlagI;

    Cpu();

    void reset();

    std::uint16_t reg(std::uint8_t code) const;
    void set_reg(std::uint8_t code, std::uint16_t value);

    std::uint16_t pc() const noexcept { return pc_; }
    void set_pc(std::uint16_t value) noexcept { pc_ = value; }
    std::uint16_t sp() const noexcept { return sp_; }
    void set_sp(std::uint16_t value) noexcept { sp_ = value; }
    std::uint16_t sr() const noexcept { return sr_; }
    void set_sr(std::uint16_t value) noexcept { sr_ = value & FlagMask; }

    bool flag(std::uint16_t flag) const noexcept { return (sr_ & flag) != 0; }
    void set_flag(std::uint16_t flag, bool value) noexcept {
        if (value) {
            sr_ |= flag;
        } else {
            sr_ &= static_cast<std::uint16_t>(~flag);
        }
    }

    bool halted() const noexcept { return halted_; }
    const std::string& halt_reason() const noexcept { return halt_reason_; }
    std::uint64_t cycles() const noexcept { return cycles_; }

    void set_syscall_handler(std::function<void(std::uint8_t)> handler) {
        syscall_handler_ = std::move(handler);
    }

    // Returns the number of simulated cycles consumed by this instruction.
    std::uint32_t step(MemoryBus& bus);

private:
    void execute(const struct Instruction& instruction, MemoryBus& bus,
                 std::uint16_t next_pc, std::uint32_t& cycles);
    void raise_exception(std::uint16_t vector, std::uint16_t saved_pc,
                         MemoryBus& bus);
    void push16(std::uint16_t value, MemoryBus& bus);
    std::uint16_t pop16(MemoryBus& bus);
    bool condition(std::uint8_t code) const;

    std::uint16_t regs_[5]{};
    std::uint16_t pc_ = 0;
    std::uint16_t sp_ = 0xF000;
    std::uint16_t sr_ = 0;
    bool halted_ = false;
    std::string halt_reason_;
    std::uint64_t cycles_ = 0;
    std::function<void(std::uint8_t)> syscall_handler_;
};

}  // namespace kr16
