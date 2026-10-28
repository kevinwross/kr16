#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kr16/container.hpp"

namespace kr16 {

struct AsmOptions {
    bool raw = false;
    std::uint16_t base = 0x0100;
    bool debug = false;
    std::vector<std::string> include_paths;
    std::string source_name = "<input>";
};

struct AsmDiagnostic {
    std::string file;
    int line = 0;
    int column = 0;
    std::string message;
};

struct AsmResult {
    bool ok = false;
    Container object;
    std::vector<std::uint8_t> raw;
    std::vector<AsmDiagnostic> diagnostics;
    std::string listing;
};

AsmResult assemble_text(std::string_view source, const AsmOptions& options);
AsmResult assemble_file(const std::string& path, AsmOptions options);

}  // namespace kr16
