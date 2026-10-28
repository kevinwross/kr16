#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "kr16/cpu.hpp"

namespace kr16 {

class Machine : public MemoryBus {
public:
    static constexpr std::uint16_t kIoBase = 0xF000;
    static constexpr std::uint16_t kRomBase = 0xF100;
    static constexpr std::size_t kRomSize = 3840;

    Machine();

    std::uint8_t read8(std::uint16_t address) override;
    void write8(std::uint16_t address, std::uint8_t value) override;

    void reset();
    void zero_ram();
    void load_rom(std::span<const std::uint8_t> rom);
    void load_bytes(std::uint16_t address, std::span<const std::uint8_t> bytes);

    void push_key(std::uint8_t value);
    const std::vector<std::uint8_t>& console_output() const noexcept {
        return console_output_;
    }
    void clear_console_output() { console_output_.clear(); }

    void tick(std::uint32_t cycles);
    std::optional<std::uint16_t> pending_interrupt() const;

    std::uint16_t timer_count() const noexcept { return timer_count_; }
    std::uint16_t timer_compare() const noexcept { return timer_compare_; }
    bool timer_match() const noexcept { return timer_match_; }
    bool keyboard_available() const noexcept { return !keyboard_.empty(); }
    bool keyboard_overflow() const noexcept { return keyboard_overflow_; }

private:
    std::uint8_t read_timer_byte(std::uint16_t address) const;
    void write_timer_byte(std::uint16_t address, std::uint8_t value);

    std::array<std::uint8_t, 65536> ram_{};
    std::array<std::uint8_t, kRomSize> rom_{};
    std::vector<std::uint8_t> console_output_;
    std::deque<std::uint8_t> keyboard_;
    bool keyboard_overflow_ = false;
    bool keyboard_irq_enable_ = false;
    bool console_echo_ = false;

    bool timer_enabled_ = false;
    bool timer_periodic_ = false;
    bool timer_irq_enable_ = false;
    bool timer_match_ = false;
    std::uint16_t timer_count_ = 0;
    std::uint16_t timer_compare_ = 0;
    std::uint16_t timer_prescale_ = 0;
    std::uint16_t timer_phase_ = 0;
};

}  // namespace kr16
