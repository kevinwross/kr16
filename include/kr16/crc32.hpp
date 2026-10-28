#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace kr16 {

std::uint32_t crc32(std::span<const std::uint8_t> data);

}  // namespace kr16
