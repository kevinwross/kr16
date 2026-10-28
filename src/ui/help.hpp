#pragma once

#include <string>

namespace kr16::ui {

std::string instruction_help(const std::string& mnemonic);
std::string addressing_help(const std::string& instruction,
                            const std::string& mnemonic);

}  // namespace kr16::ui
