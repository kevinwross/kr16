#pragma once

#include <string>
#include <vector>

namespace kr16 {

struct CompileDiagnostic {
    int line = 0;
    int column = 0;
    std::string message;
};

struct CompileOptions {
    std::string source_name = "<input>";
    bool debug = false;
};

struct CompileResult {
    bool ok = false;
    std::string assembly;
    std::vector<CompileDiagnostic> diagnostics;
};

CompileResult compile_c(const std::string& source,
                        const CompileOptions& options = {});

}  // namespace kr16
