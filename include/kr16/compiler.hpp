#pragma once

#include <string>
#include <vector>

namespace kr16 {

struct CompileDiagnostic {
    std::string file;
    int line = 0;
    int column = 0;
    std::string message;
};

struct CompileOptions {
    std::string source_name = "<input>";
    std::vector<std::string> include_paths;
    bool debug = false;
    bool optimize = true;  ///< M1/M2 folds and register-friendly codegen
};

struct CompileResult {
    bool ok = false;
    std::string assembly;
    std::vector<CompileDiagnostic> diagnostics;
};

CompileResult compile_c(const std::string& source,
                        const CompileOptions& options = {});

}  // namespace kr16

