#pragma once

#include <cstdint>
#include <string>

#include "kr16/cpu.hpp"

namespace kr16 {

struct HostCallBlock {
    std::uint16_t status = 0;
    std::uint16_t results[3] = {0, 0, 0};
    std::uint16_t arguments[4] = {0, 0, 0, 0};
};

class HostServices {
public:
    static constexpr std::uint16_t HostAbiVersion = 1;
    static constexpr std::uint16_t ErrorNone = 0;
    static constexpr std::uint16_t ErrorInvalid = 3;
    static constexpr std::uint16_t ErrorFault = 5;
    static constexpr std::uint16_t ErrorNoSys = 8;

    explicit HostServices(bool trace = false) : trace_enabled_(trace) {}

    std::uint16_t call(std::uint8_t service, std::uint16_t block,
                       MemoryBus& bus);

    const std::string& trace() const noexcept { return trace_; }
    std::uint64_t call_count() const noexcept { return call_count_; }
    std::uint16_t last_status() const noexcept { return last_status_; }

private:
    bool valid_block(std::uint16_t block) const;
    bool trace_enabled_ = false;
    std::string trace_;
    std::uint64_t call_count_ = 0;
    std::uint16_t last_status_ = 0;
};

}  // namespace kr16
