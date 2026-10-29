#pragma once

#include <cstdint>
#include <span>

namespace kr16 {

std::span<const std::uint8_t> embedded_libc_source();
std::span<const std::uint8_t> embedded_fileio_source();
std::span<const std::uint8_t> embedded_systemservices_header();

}  // namespace kr16
