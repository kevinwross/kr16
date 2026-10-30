#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kr16 {

struct CommandLineParseResult {
    bool ok = false;
    std::vector<std::string> arguments;
    std::string error;
};

/// Split a command line into arguments using the Windows command-line rules.
///
/// Double quotes group whitespace. Backslashes are literal except immediately
/// before a double quote, where pairs produce a literal backslash and an odd
/// trailing backslash escapes the quote. This preserves ordinary Windows paths
/// such as C:\Users\kevin and C:\Program Files\KR16.
CommandLineParseResult parse_command_line(std::string_view text);

}  // namespace kr16
