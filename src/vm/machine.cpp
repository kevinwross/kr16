#include "kr16/machine.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace kr16 {

Machine::Machine() { reset(); }

void Machine::reset() {
    console_output_.clear();
    keyboard_.clear();
    keyboard_overflow_ = false;
    keyboard_irq_enable_ = false;
    console_echo_ = false;
    timer_enabled_ = false;
    timer_periodic_ = false;
    timer_irq_enable_ = false;
    timer_match_ = false;
    timer_count_ = 0;
    timer_compare_ = 0;
    timer_prescale_ = 0;
    timer_phase_ = 0;
}

void Machine::zero_ram() { ram_.fill(0); }

void Machine::load_rom(std::span<const std::uint8_t> rom) {
    rom_.fill(0);
    const auto count = std::min(rom.size(), rom_.size());
    std::copy_n(rom.begin(), count, rom_.begin());
}

void Machine::load_bytes(std::uint16_t address,
                         std::span<const std::uint8_t> bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        write8(static_cast<std::uint16_t>(address + i), bytes[i]);
    }
}

void Machine::push_key(std::uint8_t value) {
    if (keyboard_.size() >= 64) {
        keyboard_overflow_ = true;
        return;
    }
    keyboard_.push_back(value);
    if (console_echo_) {
        console_output_.push_back(value);
    }
}

std::uint8_t Machine::read_timer_byte(std::uint16_t address) const {
    switch (address) {
        case 0xF008:
            return static_cast<std::uint8_t>(
                (timer_enabled_ ? 1u : 0u) |
                (timer_periodic_ ? 2u : 0u) |
                (timer_irq_enable_ ? 4u : 0u));
        case 0xF00A:
            return static_cast<std::uint8_t>(timer_count_ & 0xFF);
        case 0xF00B:
            return static_cast<std::uint8_t>((timer_count_ >> 8) & 0xFF);
        case 0xF00C:
            return static_cast<std::uint8_t>(timer_compare_ & 0xFF);
        case 0xF00D:
            return static_cast<std::uint8_t>((timer_compare_ >> 8) & 0xFF);
        case 0xF00E:
            return timer_match_ ? 1 : 0;
        case 0xF010:
            return static_cast<std::uint8_t>(timer_prescale_ & 0xFF);
        case 0xF011:
            return static_cast<std::uint8_t>((timer_prescale_ >> 8) & 0xFF);
        case 0xF012: {
            std::uint8_t status = 0;
            if (timer_match_ && timer_irq_enable_) {
                status |= 1;
            }
            if (!keyboard_.empty() && keyboard_irq_enable_) {
                status |= 2;
            }
            return status;
        }
        default:
            return 0;
    }
}

void Machine::write_timer_byte(std::uint16_t address, std::uint8_t value) {
    switch (address) {
        case 0xF008:
            timer_enabled_ = (value & 1) != 0;
            timer_periodic_ = (value & 2) != 0;
            timer_irq_enable_ = (value & 4) != 0;
            break;
        case 0xF00A:
            timer_count_ = static_cast<std::uint16_t>(
                (timer_count_ & 0xFF00) | value);
            timer_phase_ = 0;
            break;
        case 0xF00B:
            timer_count_ = static_cast<std::uint16_t>(
                (timer_count_ & 0x00FF) | (static_cast<std::uint16_t>(value) << 8));
            timer_phase_ = 0;
            break;
        case 0xF00C:
            timer_compare_ = static_cast<std::uint16_t>(
                (timer_compare_ & 0xFF00) | value);
            break;
        case 0xF00D:
            timer_compare_ = static_cast<std::uint16_t>(
                (timer_compare_ & 0x00FF) | (static_cast<std::uint16_t>(value) << 8));
            break;
        case 0xF00E:
            if ((value & 1) != 0) {
                timer_match_ = false;
            }
            break;
        case 0xF010:
            timer_prescale_ = static_cast<std::uint16_t>(
                (timer_prescale_ & 0xFF00) | value);
            timer_phase_ = 0;
            break;
        case 0xF011:
            timer_prescale_ = static_cast<std::uint16_t>(
                (timer_prescale_ & 0x00FF) |
                (static_cast<std::uint16_t>(value) << 8));
            timer_phase_ = 0;
            break;
        default:
            break;
    }
}

std::uint8_t Machine::read8(std::uint16_t address) {
    if (address < kIoBase) {
        return ram_[address];
    }
    if (address < kRomBase) {
        switch (address) {
            case 0xF000:
                return 0;
            case 0xF001:
                return 1;  // TX_READY
            case 0xF002:
                return console_echo_ ? 1 : 0;
            case 0xF004:
                if (keyboard_.empty() && keyboard_refill_) {
                    keyboard_refill_();
                }
                if (keyboard_.empty()) {
                    return 0;
                } else {
                    const std::uint8_t value = keyboard_.front();
                    keyboard_.pop_front();
                    return value;
                }
            case 0xF005:
                if (keyboard_.empty() && keyboard_refill_) {
                    keyboard_refill_();
                }
                return static_cast<std::uint8_t>(
                    (keyboard_.empty() ? 0u : 1u) |
                    (keyboard_overflow_ ? 2u : 0u));
            case 0xF006:
                return keyboard_irq_enable_ ? 1 : 0;
            default:
                return read_timer_byte(address);
        }
    }
    const auto offset = static_cast<std::size_t>(address - kRomBase);
    return offset < rom_.size() ? rom_[offset] : 0;
}

void Machine::write8(std::uint16_t address, std::uint8_t value) {
    if (address < kIoBase) {
        ram_[address] = value;
        return;
    }
    if (address < kRomBase) {
        switch (address) {
            case 0xF000:
                console_output_.push_back(value);
                break;
            case 0xF002:
                console_echo_ = (value & 1) != 0;
                break;
            case 0xF005:
                if ((value & 2) != 0) {
                    keyboard_overflow_ = false;
                }
                break;
            case 0xF006:
                keyboard_irq_enable_ = (value & 1) != 0;
                break;
            default:
                write_timer_byte(address, value);
                break;
        }
        return;
    }
    // ROM is read-only while the CPU runs.
}

void Machine::tick(std::uint32_t cycles) {
    if (!timer_enabled_) {
        return;
    }
    const std::uint32_t divisor = static_cast<std::uint32_t>(timer_prescale_) + 1;
    std::uint32_t total = static_cast<std::uint32_t>(timer_phase_) + cycles;
    const std::uint32_t ticks = total / divisor;
    timer_phase_ = static_cast<std::uint16_t>(total % divisor);

    for (std::uint32_t i = 0; i < ticks; ++i) {
        timer_count_ = static_cast<std::uint16_t>(timer_count_ + 1);
        if (timer_count_ == timer_compare_) {
            timer_match_ = true;
            if (timer_periodic_) {
                timer_count_ = 0;
            } else {
                timer_enabled_ = false;
                break;
            }
        }
    }
}

std::optional<std::uint16_t> Machine::pending_interrupt() const {
    if (!keyboard_.empty() && keyboard_irq_enable_) {
        return std::uint16_t{4};
    }
    if (timer_match_ && timer_irq_enable_) {
        return std::uint16_t{3};
    }
    return std::nullopt;
}

}  // namespace kr16
