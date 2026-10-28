#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace kr16::platform {

bool stdin_is_terminal();

std::string executable_path();

// Reads one edited line when stdin is a terminal. The history vector is
// ordered oldest-to-newest. Returns nullopt on Ctrl-D/EOF.
std::optional<std::string> read_line(const std::string& prompt,
                                     std::ostream& output,
                                     const std::vector<std::string>& history);

}  // namespace kr16::platform

