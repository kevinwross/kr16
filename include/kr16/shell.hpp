#pragma once

#include <cstddef>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace kr16::cli {

constexpr std::size_t kShellHistoryLimit = 100;

struct ShellTokenizeResult {
    std::vector<std::string> tokens;
    std::string error;
};

ShellTokenizeResult tokenize_shell_line(std::string_view line);

class ShellHistory {
public:
    explicit ShellHistory(std::size_t limit = kShellHistoryLimit);

    void load(const std::string& path);
    void save(const std::string& path) const;
    void add(const std::string& line);
    void clear();

    const std::vector<std::string>& entries() const { return entries_; }
    std::size_t limit() const { return limit_; }

private:
    std::size_t limit_;
    std::vector<std::string> entries_;
};

std::string default_history_path();

// Runs the interactive command loop. When interactive is false, prompts and
// line editing are disabled and commands are read from `input` until EOF.
int run_shell(std::istream& input, std::ostream& output, std::ostream& error,
              bool interactive, const std::string& history_path = {});

}  // namespace kr16::cli
