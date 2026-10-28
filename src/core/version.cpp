#include "kr16/version.hpp"

#include <string>

#ifndef KR16_VERSION
#define KR16_VERSION "1.0.0"
#endif

namespace kr16 {

std::string format_version() {
    std::string text = "kr16 ";
    text += KR16_VERSION;
    text += " (ISA ";
    text += std::to_string(kIsaVersion);
    text += ", container ";
    text += std::to_string(kContainerVersion);
    text += ", host ABI ";
    text += std::to_string(kHostAbiVersion);
    text += ", debug ";
    text += std::to_string(kDebugFormatVersion);
    text += ")";
    return text;
}

}  // namespace kr16

