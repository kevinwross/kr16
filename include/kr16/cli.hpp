#pragma once

#include <string>
#include <string_view>

namespace kr16::cli {

std::string usage_text();
bool is_known_command(std::string_view command);
int run(int argc, char** argv);

}  // namespace kr16::cli
