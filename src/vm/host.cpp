#include "kr16/host.hpp"

#include <cstdint>
#include <sstream>

namespace kr16 {

bool HostServices::valid_block(std::uint16_t block) const {
    return (block & 1u) == 0 &&
           static_cast<std::uint32_t>(block) + 16 <= 0xF000;
}

std::uint16_t HostServices::call(std::uint8_t service, std::uint16_t block,
                                 MemoryBus& bus) {
    ++call_count_;
    if (!valid_block(block)) {
        last_status_ = ErrorFault;
        if (trace_enabled_) {
            std::ostringstream line;
            line << "SYSCALL #" << static_cast<unsigned>(service)
                 << " block=0x" << std::hex << block << std::dec
                 << " status=" << ErrorFault << " (invalid block)\n";
            trace_ += line.str();
        }
        return ErrorFault;
    }

    HostCallBlock values;
    values.status = bus.read16(block);
    for (std::size_t i = 0; i < 3; ++i) {
        values.results[i] =
            bus.read16(static_cast<std::uint16_t>(block + 2 + i * 2));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        values.arguments[i] =
            bus.read16(static_cast<std::uint16_t>(block + 8 + i * 2));
    }

    std::uint16_t status = ErrorNone;
    if (service == 0) {
        values.results[0] = HostAbiVersion;
        values.results[1] = 1u << 4;  // console file descriptors
        values.results[2] = 0;
    } else {
        status = ErrorNoSys;
        values.results[0] = 0;
        values.results[1] = 0;
        values.results[2] = 0;
    }
    values.status = status;
    bus.write16(block, status);
    for (std::size_t i = 0; i < 3; ++i) {
        bus.write16(static_cast<std::uint16_t>(block + 2 + i * 2),
                    values.results[i]);
    }
    last_status_ = status;

    if (trace_enabled_) {
        std::ostringstream line;
        line << "SYSCALL #" << static_cast<unsigned>(service)
             << " block=0x" << std::hex << block << std::dec
             << " status=" << status << " result0=" << values.results[0]
             << " result1=" << values.results[1] << "\n";
        trace_ += line.str();
    }
    return status;
}

}  // namespace kr16
