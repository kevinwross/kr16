#include "kr16/crc32.hpp"

namespace kr16 {
namespace {

constexpr std::uint32_t kPolynomial = 0xEDB88320u;

std::uint32_t update(std::uint32_t crc, std::uint8_t byte) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        const std::uint32_t mask = 0u - (crc & 1u);
        crc = (crc >> 1) ^ (kPolynomial & mask);
    }
    return crc;
}

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t byte : data) {
        crc = update(crc, byte);
    }
    return ~crc;
}

}  // namespace kr16
