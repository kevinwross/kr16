#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "kr16/shell.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "kr16/build.hpp"
#include "kr16/cli.hpp"
#include "kr16/version.hpp"
#include "platform/terminal.hpp"

namespace kr16::cli {
namespace {

std::string trim_copy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string current_directory() {
    std::error_code ec;
    const auto path = std::filesystem::current_path(ec);
    return ec ? std::string(".") : path.string();
}

std::string prompt_text() {
    return "kr16 " + current_directory() + "> ";
}

std::optional<std::string> environment_value(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

int dispatch(const std::vector<std::string>& tokens) {
    std::vector<std::string> mutable_tokens = tokens;
    std::vector<char*> argv;
    argv.reserve(mutable_tokens.size() + 1);
    std::string program = "kr16";
    argv.push_back(program.data());
    for (auto& token : mutable_tokens) {
        argv.push_back(token.data());
    }
    return run(static_cast<int>(argv.size()), argv.data());
}

std::string shell_help() {
    return std::string(usage_text()) +
           "\nInteractive commands:\n"
           "  cd [dir]      Change directory (no argument: home)\n"
           "  pwd           Print the current directory\n"
           "  clear         Clear the terminal\n"
           "  history       Show command history\n"
           "  build file... Build .c, .asm, .s, or .o into a .kex (no run)\n"
           "  load file...  Build and run .c, .asm, .s, .o, or .kex\n"
           "  run file...   Run a program (alias for vm)\n"
           "  quit, exit    Leave the shell\n"
           "  help          Show this help\n";
}

bool contains_force_flag(const std::vector<std::string>& tokens,
                         std::vector<std::string>& stripped) {
    bool force = false;
    stripped.clear();
    for (const auto& token : tokens) {
        if (token == "--force" || token == "-f") {
            force = true;
        } else {
            stripped.push_back(token);
        }
    }
    return force;
}

bool has_help_flag(const std::vector<std::string>& tokens) {
    for (const auto& token : tokens) {
        if (token == "--help" || token == "-h") {
            return true;
        }
    }
    return false;
}

int run_interactive_command(const std::vector<std::string>& tokens,
                            std::ostream& error) {
    const std::string& command = tokens.front();
    if (command == "dap") {
        std::vector<std::string> stripped;
        const bool force = contains_force_flag(tokens, stripped);
        if (!force && !has_help_flag(tokens)) {
            error << "dap: refusing to block the interactive shell.\n"
                     "Use 'dap --force <program>' to run the adapter here, "
                     "or launch it from VS Code.\n";
            return 2;
        }
        if (force && !has_help_flag(tokens)) {
            error << "dap: running in the foreground until the client "
                     "disconnects; Ctrl-C to stop.\n";
        }
        return dispatch(stripped);
    }
    return dispatch(tokens);
}

int load_program(const std::vector<std::string>& tokens,
                 std::ostream& output, std::ostream& error) {
    if (tokens.size() != 2) {
        error << "load: usage: load <file.c|file.asm|file.s|file.o|file.kex>\n";
        return 2;
    }
    const std::filesystem::path source = tokens[1];
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) {
        error << "load: cannot find " << source.string() << '\n';
        return 1;
    }
    std::string extension = source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });

    if (extension == ".kex") {
        return dispatch({"vm", source.string()});
    }
    if (extension != ".c" && extension != ".asm" && extension != ".s" &&
        extension != ".o") {
        error << "load: unsupported file type '" << extension << "'\n";
        return 2;
    }

    BuildOptions options;
    options.input = source;
    options.command = "load";
    if (source.extension() == ".c" || source.extension() == ".C") {
        options.link_libc = true;
    }
    const auto result = build_program(options, error);
    if (!result.ok) {
        return 1;
    }
    output << "load: built " << result.executable.string() << '\n';
    return dispatch({"vm", result.executable.string()});
}

int run_shell_command(const std::vector<std::string>& tokens,
                      const ShellHistory& history, std::ostream& output,
                      std::ostream& error) {
    const std::string& command = tokens.front();
    if (command == "quit" || command == "exit") {
        return -1;
    }
    if (command == "help") {
        output << shell_help();
        return 0;
    }
    if (command == "pwd") {
        output << current_directory() << '\n';
        return 0;
    }
    if (command == "clear") {
        output << "\x1b[2J\x1b[H";
        output.flush();
        return 0;
    }
    if (command == "cd") {
        std::filesystem::path target;
        if (tokens.size() == 1) {
            auto home = environment_value("HOME");
            if (!home.has_value()) {
                home = environment_value("USERPROFILE");
            }
            if (!home.has_value()) {
                error << "cd: home directory is not set\n";
                return 1;
            }
            target = *home;
        } else if (tokens.size() == 2) {
            target = tokens[1];
        } else {
            error << "cd: too many arguments\n";
            return 2;
        }
        std::error_code ec;
        std::filesystem::current_path(target, ec);
        if (ec) {
            error << "cd: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (command == "history") {
        const auto& entries = history.entries();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            output << (i + 1) << "  " << entries[i] << '\n';
        }
        return 0;
    }
    if (command == "load") {
        return load_program(tokens, output, error);
    }
    if (command == "run") {
        std::vector<std::string> vm = tokens;
        vm[0] = "vm";
        return run_interactive_command(vm, error);
    }
    if (command == "version") {
        output << format_version() << '\n';
        return 0;
    }
    if (!is_known_command(command)) {
        error << "kr16: unknown command '" << command << "'\n";
        return 2;
    }
    return run_interactive_command(tokens, error);
}

std::optional<std::string> read_line_batch(std::istream& input) {
    std::string line;
    if (!std::getline(input, line)) {
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

}  // namespace

ShellTokenizeResult tokenize_shell_line(std::string_view line) {
    ShellTokenizeResult result;
    std::size_t index = 0;
    while (index < line.size()) {
        while (index < line.size() &&
               std::isspace(static_cast<unsigned char>(line[index])) != 0) {
            ++index;
        }
        if (index >= line.size()) {
            break;
        }
        std::string token;
        if (line[index] == '"') {
            ++index;
            bool closed = false;
            while (index < line.size()) {
                const char ch = line[index++];
                if (ch == '"') {
                    closed = true;
                    break;
                }
                if (ch == '\\' && index < line.size() &&
                    (line[index] == '"' || line[index] == '\\')) {
                    token.push_back(line[index++]);
                } else {
                    token.push_back(ch);
                }
            }
            if (!closed) {
                result.error = "unterminated quote";
                result.tokens.clear();
                return result;
            }
        } else {
            while (index < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[index])) == 0) {
                token.push_back(line[index++]);
            }
        }
        result.tokens.push_back(std::move(token));
    }
    return result;
}

ShellHistory::ShellHistory(std::size_t limit) : limit_(limit == 0 ? 1 : limit) {}

void ShellHistory::load(const std::string& path) {
    if (path.empty()) {
        return;
    }
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            entries_.push_back(line);
        }
    }
    if (entries_.size() > limit_) {
        entries_.erase(entries_.begin(),
                       entries_.begin() +
                           static_cast<std::ptrdiff_t>(entries_.size() - limit_));
    }
}

void ShellHistory::save(const std::string& path) const {
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream output(path, std::ios::trunc);
    for (const auto& entry : entries_) {
        output << entry << '\n';
    }
}

void ShellHistory::add(const std::string& line) {
    if (line.empty()) {
        return;
    }
    if (!entries_.empty() && entries_.back() == line) {
        return;
    }
    entries_.push_back(line);
    if (entries_.size() > limit_) {
        entries_.erase(entries_.begin());
    }
}

void ShellHistory::clear() {
    entries_.clear();
}

std::string default_history_path() {
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        home = std::getenv("USERPROFILE");
    }
    if (home == nullptr) {
        return {};
    }
    return (std::filesystem::path(home) / ".kr16_history").string();
}

int run_shell(std::istream& input, std::ostream& output, std::ostream& error,
              bool interactive, const std::string& history_path) {
    ShellHistory history;
    if (interactive && !history_path.empty()) {
        history.load(history_path);
    }
    if (interactive) {
        output << format_version() << " interactive\n"
               << "Type 'help' for commands; 'quit' or Ctrl-D to leave.\n";
    }

    for (;;) {
        std::optional<std::string> line;
        if (interactive) {
            line = platform::read_line(prompt_text(), output, history.entries());
        } else {
            line = read_line_batch(input);
        }
        if (!line.has_value()) {
            break;
        }
        const std::string trimmed = trim_copy(*line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        auto tokenized = tokenize_shell_line(trimmed);
        if (!tokenized.error.empty()) {
            error << "kr16: " << tokenized.error << '\n';
            continue;
        }
        if (tokenized.tokens.empty()) {
            continue;
        }
        const int result =
            run_shell_command(tokenized.tokens, history, output, error);
        if (result < 0) {
            break;
        }
        history.add(trimmed);
        if (interactive && !history_path.empty()) {
            history.save(history_path);
        }
    }
    if (interactive && !history_path.empty()) {
        history.save(history_path);
    }
    return 0;
}

}  // namespace kr16::cli


