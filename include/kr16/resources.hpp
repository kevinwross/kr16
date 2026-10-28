#pragma once

#include <cstdint>
#include <span>

namespace kr16 {

// Minimal reference BIOS image, generated from resources/bios.s.
std::span<const std::uint8_t> embedded_bios_rom();

}  // namespace kr16
