#pragma once

#include <cstdint>
#include <span>

namespace kr16 {

std::span<const std::uint8_t> embedded_libc_source();

}  // namespace kr16
