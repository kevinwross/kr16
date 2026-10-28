#pragma once

#include <string>

namespace kr16 {

inline constexpr int kIsaVersion = 1;
inline constexpr int kContainerVersion = 1;
inline constexpr int kHostAbiVersion = 1;
inline constexpr int kDebugFormatVersion = 1;

std::string format_version();

}  // namespace kr16
