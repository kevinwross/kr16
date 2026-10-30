#include "kr16/command_line.hpp"

#include <cctype>
#include <cstddef>
#include <utility>

namespace kr16 {
namespace {

bool is_space(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

}  // namespace

CommandLineParseResult parse_command_line(std::string_view text) {
    CommandLineParseResult result;
    result.ok = true;

    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && is_space(text[i])) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }

        std::string argument;
        bool in_quotes = false;
        bool started = false;

        while (i < text.size()) {
            const char ch = text[i];
            if (!in_quotes && is_space(ch)) {
                break;
            }

            if (ch == '\\') {
                std::size_t count = 0;
                while (i + count < text.size() &&
                       text[i + count] == '\\') {
                    ++count;
                }
                const bool before_quote =
                    i + count < text.size() && text[i + count] == '"';
                if (!before_quote) {
                    argument.append(count, '\\');
                    i += count;
                    started = true;
                    continue;
                }

                argument.append(count / 2, '\\');
                if (count % 2 != 0) {
                    argument.push_back('"');
                    i += count + 1;
                    started = true;
                } else {
                    i += count;
                }
                continue;
            }

            if (ch == '"') {
                if (in_quotes && i + 1 < text.size() &&
                    text[i + 1] == '"') {
                    argument.push_back('"');
                    i += 2;
                    started = true;
                    continue;
                }
                in_quotes = !in_quotes;
                started = true;
                ++i;
                continue;
            }

            argument.push_back(ch);
            started = true;
            ++i;
        }

        if (in_quotes) {
            result.ok = false;
            result.error = "unterminated double quote";
            result.arguments.clear();
            return result;
        }
        if (started) {
            result.arguments.push_back(std::move(argument));
        }
    }

    return result;
}

}  // namespace kr16
