#include "kr16/compiler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kr16 {
namespace {

struct CompileError : std::runtime_error {
    CompileError(int line, int column, std::string message)
        : std::runtime_error(std::move(message)), line(line), column(column) {}
    int line;
    int column;
};

std::string escape_debug_path(std::string_view path) {
    std::string result;
    result.reserve(path.size() + 8);
    for (const char ch : path) {
        if (ch == '\\' || ch == '"') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

enum class TypeKind { Void, Char, Int, Pointer, Array, Struct, Union };

struct Type;
struct Member {
    std::string name;
    std::shared_ptr<Type> type;
    int offset = 0;
};

struct Type {
    TypeKind kind = TypeKind::Int;
    int count = 0;
    std::shared_ptr<Type> base;
    std::vector<Member> members;
    std::string name;

    int size() const {
        switch (kind) {
            case TypeKind::Void: return 0;
            case TypeKind::Char: return 1;
            case TypeKind::Int: return 2;
            case TypeKind::Pointer: return 2;
            case TypeKind::Array: return count * (base ? base->size() : 2);
            case TypeKind::Struct: {
                int total = 0;
                for (const auto& member : members) {
                    total = std::max(total, member.offset + member.type->size());
                }
                return total;
            }
            case TypeKind::Union: {
                int total = 0;
                for (const auto& member : members) {
                    total = std::max(total, member.type->size());
                }
                return total;
            }
        }
        return 2;
    }
    bool is_char() const { return kind == TypeKind::Char; }
    bool is_array() const { return kind == TypeKind::Array; }
};

std::shared_ptr<Type> make_type(TypeKind kind,
                                std::shared_ptr<Type> base = nullptr,
                                int count = 0) {
    auto type = std::make_shared<Type>();
    type->kind = kind;
    type->base = std::move(base);
    type->count = count;
    return type;
}

enum class TokenKind { Eof, Identifier, Number, Character, String, Punct };

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    std::int64_t value = 0;
    int line = 1;
    int column = 1;
};

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

struct Macro {
    bool function = false;
    std::vector<std::string> params;
    std::string body;
};

std::vector<std::string> split_macro_args(const std::string& text) {
    std::vector<std::string> args;
    std::string current;
    int depth = 0;
    for (const char ch : text) {
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
        }
        if (ch == ',' && depth == 0) {
            args.push_back(trim_copy(current));
            current.clear();
        } else {
            current += ch;
        }
    }
    if (!trim_copy(current).empty() || !args.empty()) {
        args.push_back(trim_copy(current));
    }
    return args;
}

std::string substitute_macro(const std::string& body,
                             const std::vector<std::string>& params,
                             const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0; i < body.size();) {
        const char ch = body[i];
        if (ch == '"' || ch == '\'') {
            const char quote = ch;
            out += ch;
            ++i;
            while (i < body.size()) {
                out += body[i];
                if (body[i] == '\\' && i + 1 < body.size()) {
                    out += body[i + 1];
                    i += 2;
                    continue;
                }
                if (body[i] == quote) {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        if (ch == '#' && i + 1 < body.size() && body[i + 1] == '#') {
            while (!out.empty() &&
                   std::isspace(static_cast<unsigned char>(out.back())) != 0) {
                out.pop_back();
            }
            i += 2;
            while (i < body.size() &&
                   std::isspace(static_cast<unsigned char>(body[i])) != 0) {
                ++i;
            }
            continue;
        }
        if (ch == '#' && (i + 1 >= body.size() || body[i + 1] != '#')) {
            std::size_t next = i + 1;
            while (next < body.size() &&
                   std::isspace(static_cast<unsigned char>(body[next])) != 0) {
                ++next;
            }
            const std::size_t start = next;
            while (next < body.size() &&
                   (std::isalnum(static_cast<unsigned char>(body[next])) != 0 ||
                    body[next] == '_')) {
                ++next;
            }
            const std::string name = body.substr(start, next - start);
            bool replaced = false;
            for (std::size_t p = 0; p < params.size(); ++p) {
                if (params[p] == name && p < args.size()) {
                    out += '"';
                    for (const char arg_ch : args[p]) {
                        if (arg_ch == '\\' || arg_ch == '"') {
                            out += '\\';
                        }
                        out += arg_ch;
                    }
                    out += '"';
                    i = next;
                    replaced = true;
                    break;
                }
            }
            if (replaced) {
                continue;
            }
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            const std::size_t start = i;
            while (i < body.size() &&
                   (std::isalnum(static_cast<unsigned char>(body[i])) != 0 ||
                    body[i] == '_')) {
                ++i;
            }
            const std::string name = body.substr(start, i - start);
            bool replaced = false;
            for (std::size_t p = 0; p < params.size(); ++p) {
                if (params[p] == name && p < args.size()) {
                    out += args[p];
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                out += name;
            }
            continue;
        }
        out += ch;
        ++i;
    }
    return out;
}

std::string expand_macros(const std::string& line,
                          const std::map<std::string, Macro>& macros,
                          int depth) {
    if (depth > 20) {
        return line;
    }
    std::string out;
    for (std::size_t i = 0; i < line.size();) {
        const char ch = line[i];
        if (ch == '"' || ch == '\'') {
            const char quote = ch;
            out += ch;
            ++i;
            while (i < line.size()) {
                out += line[i];
                if (line[i] == '\\' && i + 1 < line.size()) {
                    out += line[i + 1];
                    i += 2;
                    continue;
                }
                if (line[i] == quote) {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        if (ch == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            out += line.substr(i);
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            const std::size_t start = i;
            while (i < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[i])) != 0 ||
                    line[i] == '_')) {
                ++i;
            }
            const std::string name = line.substr(start, i - start);
            const auto macro = macros.find(name);
            if (macro == macros.end()) {
                out += name;
                continue;
            }
            if (!macro->second.function) {
                out += expand_macros(macro->second.body, macros, depth + 1);
                continue;
            }
            std::size_t open = i;
            while (open < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[open])) != 0) {
                ++open;
            }
            if (open >= line.size() || line[open] != '(') {
                out += name;
                continue;
            }
            std::size_t close = open + 1;
            int parens = 1;
            while (close < line.size() && parens > 0) {
                if (line[close] == '(') {
                    ++parens;
                } else if (line[close] == ')') {
                    --parens;
                }
                ++close;
            }
            if (parens != 0) {
                out += name;
                continue;
            }
            const auto args =
                split_macro_args(line.substr(open + 1, close - open - 2));
            const auto substituted = substitute_macro(
                macro->second.body, macro->second.params, args);
            out += expand_macros(substituted, macros, depth + 1);
            i = close;
            continue;
        }
        out += ch;
        ++i;
    }
    return out;
}

std::string builtin_header(std::string_view name) {
    if (name == "stdio.h") {
        return "typedef struct FILE FILE;\n"
               "#define SEEK_SET 0\n"
               "#define SEEK_CUR 1\n"
               "#define SEEK_END 2\n"
               "int putchar(int c);\n"
               "int puts(char *s);\n"
               "int getchar(void);\n"
               "int printf(char *fmt, ...);\n"
               "FILE *fopen(const char *path, const char *mode);\n"
               "int fclose(FILE *stream);\n"
               "int fread(void *buffer, int size, int count, FILE *stream);\n"
               "int fwrite(const void *buffer, int size, int count, FILE *stream);\n"
               "int fgetc(FILE *stream);\n"
               "int fputc(int value, FILE *stream);\n"
               "char *fgets(char *buffer, int size, FILE *stream);\n"
               "int fputs(const char *text, FILE *stream);\n"
               "int fseek(FILE *stream, int offset, int whence);\n"
               "int ftell(FILE *stream);\n"
               "void rewind(FILE *stream);\n"
               "int fflush(FILE *stream);\n"
               "int feof(FILE *stream);\n"
               "int ferror(FILE *stream);\n"
               "void clearerr(FILE *stream);\n"
               "int remove(const char *path);\n"
               "int rename(const char *old_path, const char *new_path);\n";
    }
    if (name == "stdlib.h") {
        return "typedef int size_t;\n"
               "void exit(int code);\n"
               "int atoi(char *s);\n"
               "int abs(int value);\n"
               "void *malloc(size_t size);\n"
               "void *calloc(size_t count, size_t size);\n"
               "void *realloc(void *ptr, size_t size);\n"
               "void free(void *ptr);\n";
    }
    if (name == "stdint.h") {
        return "typedef char int8_t;\n"
               "typedef unsigned char uint8_t;\n"
               "typedef int int16_t;\n"
               "typedef unsigned int uint16_t;\n";
    }
    if (name == "stddef.h") {
        return "typedef int size_t;\n";
    }
    if (name == "string.h") {
        return "typedef int size_t;\n"
               "size_t strlen(char *s);\n"
               "char *strcpy(char *d, char *s);\n"
               "char *strcat(char *d, char *s);\n"
               "int strcmp(char *a, char *b);\n"
               "void *memset(void *d, int v, size_t n);\n"
               "void *memcpy(void *d, void *s, size_t n);\n";
    }
    if (name == "stdbool.h") {
        return "#define bool int\n#define true 1\n#define false 0\n";
    }
    if (name == "stdarg.h") {
        return "typedef int *va_list;\n"
               "#define va_start(ap, last) __builtin_va_start(&(ap))\n"
               "#define va_arg(ap, type) "
               "__builtin_va_arg(&(ap), sizeof(type))\n"
               "#define va_end(ap)\n"
               "#define va_copy(dest, src) ((dest) = (src))\n";
    }
    if (name == "limits.h") {
        return "#define INT_MAX 32767\n#define INT_MIN (-32768)\n";
    }
    return {};
}

std::string preprocess_source_impl(
    const std::string& source, const std::string& base_dir,
    const std::vector<std::string>& include_paths,
    std::map<std::string, Macro>& macros, int depth) {
    std::istringstream input(source);
    std::string line;
    std::string out;
    int source_line = 0;
    struct Conditional {
        bool parent = true;
        bool condition = true;
        bool else_seen = false;
        bool taken = false;
    };
    std::vector<Conditional> conditionals;
    auto active = [&]() {
        if (conditionals.empty()) {
            return true;
        }
        const auto& top = conditionals.back();
        return top.parent && top.condition;
    };
    while (std::getline(input, line)) {
        ++source_line;
        const std::string trimmed = trim_copy(line);
        if (!trimmed.empty() && trimmed.front() == '#') {
            std::string rest = trim_copy(trimmed.substr(1));
            const auto space = rest.find_first_of(" \t");
            const std::string directive =
                space == std::string::npos ? rest : rest.substr(0, space);
            const std::string argument =
                space == std::string::npos ? "" : trim_copy(rest.substr(space));
            if (directive == "include" && active() && depth < 10) {
                std::string file;
                bool system_header = false;
                if (argument.size() >= 2 && argument.front() == '"' &&
                    argument.back() == '"') {
                    file = argument.substr(1, argument.size() - 2);
                } else if (argument.size() >= 2 && argument.front() == '<' &&
                           argument.back() == '>') {
                    file = argument.substr(1, argument.size() - 2);
                    system_header = true;
                }
                bool included = false;
                bool emitted_include = false;
                auto include_file = [&](const std::filesystem::path& path) {
                    std::ifstream include(path, std::ios::binary);
                    if (!include.good()) {
                        return false;
                    }
                    std::string included_text{
                        std::istreambuf_iterator<char>(include),
                        std::istreambuf_iterator<char>()};
                    out += preprocess_source_impl(
                        included_text, path.parent_path().string(),
                        include_paths, macros, depth + 1);
                    emitted_include = true;
                    return true;
                };
                if (!system_header && !file.empty()) {
                    included = include_file(
                        std::filesystem::path(base_dir) / file);
                }
                if (!included && !file.empty()) {
                    for (const auto& dir : include_paths) {
                        if (include_file(std::filesystem::path(dir) / file)) {
                            included = true;
                            break;
                        }
                    }
                }
                if (!included && !file.empty()) {
                    const std::string header = builtin_header(file);
                    if (!header.empty()) {
                        std::string guard = "__KR16_HEADER_";
                        for (const char ch : file) {
                            guard +=
                                (std::isalnum(static_cast<unsigned char>(ch)) != 0)
                                    ? static_cast<char>(std::toupper(
                                          static_cast<unsigned char>(ch)))
                                    : '_';
                        }
                        if (macros.count(guard) == 0) {
                            macros[guard] = Macro{};
                            out += preprocess_source_impl(
                                header, base_dir, include_paths, macros,
                                depth + 1);
                            emitted_include = true;
                        }
                    }
                }
                if (emitted_include) {
                    out += "#line " + std::to_string(source_line + 1) + "\n";
                } else {
                    out += '\n';
                }
                continue;
            }
            if (directive == "define") {
                if (active()) {
                    std::size_t name_end = 0;
                    while (name_end < argument.size() &&
                           (std::isalnum(
                                static_cast<unsigned char>(argument[name_end])) !=
                                0 ||
                            argument[name_end] == '_')) {
                        ++name_end;
                    }
                    const std::string name = argument.substr(0, name_end);
                    if (!name.empty()) {
                        Macro macro;
                        std::size_t body_start = name_end;
                        if (name_end < argument.size() &&
                            argument[name_end] == '(') {
                            std::size_t close = name_end + 1;
                            int parens = 1;
                            while (close < argument.size() && parens > 0) {
                                if (argument[close] == '(') {
                                    ++parens;
                                } else if (argument[close] == ')') {
                                    --parens;
                                }
                                ++close;
                            }
                            macro.function = true;
                            macro.params = split_macro_args(
                                argument.substr(name_end + 1,
                                                close - name_end - 2));
                            body_start = close;
                        }
                        macro.body = trim_copy(argument.substr(body_start));
                        macros[name] = std::move(macro);
                    }
                }
            } else if (directive == "undef") {
                if (active()) {
                    macros.erase(argument);
                }
            } else if (directive == "ifdef" || directive == "ifndef" ||
                       directive == "if") {
                const bool parent = active();
                bool condition = false;
                if (directive == "ifdef") {
                    condition = macros.count(argument) != 0;
                } else if (directive == "ifndef") {
                    condition = macros.count(argument) == 0;
                } else {
                    condition = argument != "0";
                }
                conditionals.push_back(
                    Conditional{parent, condition, false, condition});
            } else if (directive == "elif") {
                if (!conditionals.empty() && !conditionals.back().else_seen) {
                    auto& top = conditionals.back();
                    const bool condition = !top.taken && argument != "0";
                    top.condition = condition;
                    if (condition) {
                        top.taken = true;
                    }
                }
            } else if (directive == "else") {
                if (!conditionals.empty()) {
                    auto& top = conditionals.back();
                    top.else_seen = true;
                    top.condition = !top.taken;
                    top.taken = true;
                }
            } else if (directive == "endif") {
                if (!conditionals.empty()) {
                    conditionals.pop_back();
                }
            }
            // Directive lines are removed but their line number is preserved.
            out += '\n';
            continue;
        }
        if (active()) {
            out += expand_macros(line, macros, 0);
        }
        out += '\n';
    }
    return out;
}

std::string preprocess_source(const std::string& source,
                              const std::string& base_dir,
                              const std::vector<std::string>& include_paths) {
    std::map<std::string, Macro> macros;
    return preprocess_source_impl(source, base_dir, include_paths, macros, 0);
}

class Lexer {
public:
    Lexer(std::string_view source) : source_(source) {}

    std::vector<Token> run() {
        std::vector<Token> tokens;
        for (;;) {
            skip_space_and_comments();
            if (position_ >= source_.size()) {
                tokens.push_back(Token{TokenKind::Eof, "", 0, line_, column_});
                return tokens;
            }
            if (source_[position_] == '#') {
                const std::size_t directive_start = position_;
                while (position_ < source_.size() &&
                       source_[position_] != '\n') {
                    advance();
                }
                const std::string directive(
                    source_.substr(directive_start,
                                   position_ - directive_start));
                if (directive.rfind("#line ", 0) == 0) {
                    std::size_t cursor = 6;
                    while (cursor < directive.size() &&
                           std::isspace(static_cast<unsigned char>(
                               directive[cursor])) != 0) {
                        ++cursor;
                    }
                    try {
                        const int number =
                            std::stoi(directive.substr(cursor));
                        line_ = std::max(1, number) - 1;
                    } catch (const std::exception&) {
                    }
                }
                if (position_ < source_.size()) {
                    advance();
                }
                continue;
            }
            const char ch = source_[position_];
            if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
                tokens.push_back(identifier());
            } else if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                tokens.push_back(number());
            } else if (ch == '\'') {
                tokens.push_back(character());
            } else if (ch == '"') {
                tokens.push_back(string_literal());
            } else {
                tokens.push_back(punctuation());
            }
        }
    }

private:
    void advance() {
        if (source_[position_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++position_;
    }

    void skip_space_and_comments() {
        for (;;) {
            while (position_ < source_.size() &&
                   std::isspace(static_cast<unsigned char>(source_[position_])) != 0) {
                advance();
            }
            if (position_ + 1 < source_.size() &&
                source_[position_] == '/' && source_[position_ + 1] == '/') {
                while (position_ < source_.size() && source_[position_] != '\n') {
                    advance();
                }
                continue;
            }
            if (position_ + 1 < source_.size() &&
                source_[position_] == '/' && source_[position_ + 1] == '*') {
                advance();
                advance();
                while (position_ + 1 < source_.size() &&
                       !(source_[position_] == '*' &&
                         source_[position_ + 1] == '/')) {
                    advance();
                }
                if (position_ + 1 < source_.size()) {
                    advance();
                    advance();
                }
                continue;
            }
            break;
        }
    }

    Token identifier() {
        const int line = line_;
        const int column = column_;
        const std::size_t start = position_;
        while (position_ < source_.size()) {
            const char ch = source_[position_];
            if (std::isalnum(static_cast<unsigned char>(ch)) == 0 && ch != '_') {
                break;
            }
            advance();
        }
        return Token{TokenKind::Identifier,
                     std::string(source_.substr(start, position_ - start)), 0,
                     line, column};
    }

    Token number() {
        const int line = line_;
        const int column = column_;
        std::size_t start = position_;
        int base = 10;
        if (position_ + 1 < source_.size() && source_[position_] == '0' &&
            (source_[position_ + 1] == 'x' || source_[position_ + 1] == 'X')) {
            base = 16;
            advance();
            advance();
            start = position_;
        }
        while (position_ < source_.size()) {
            const char ch = source_[position_];
            const bool digit =
                base == 16 ? std::isxdigit(static_cast<unsigned char>(ch)) != 0
                           : std::isdigit(static_cast<unsigned char>(ch)) != 0;
            if (!digit) {
                break;
            }
            advance();
        }
        const std::string text(source_.substr(start, position_ - start));
        return Token{TokenKind::Number, text,
                     static_cast<std::int64_t>(std::stoll(text, nullptr, base)),
                     line, column};
    }

    Token character() {
        const int line = line_;
        const int column = column_;
        advance();
        char value = 0;
        if (position_ < source_.size()) {
            value = source_[position_];
            advance();
            if (value == '\\' && position_ < source_.size()) {
                const char escape = source_[position_];
                advance();
                switch (escape) {
                    case 'n': value = '\n'; break;
                    case 't': value = '\t'; break;
                    case 'r': value = '\r'; break;
                    case '0': value = '\0'; break;
                    case '\\': value = '\\'; break;
                    case '\'': value = '\''; break;
                    case '"': value = '"'; break;
                    default: value = escape; break;
                }
            }
        }
        if (position_ < source_.size() && source_[position_] == '\'') {
            advance();
        }
        return Token{TokenKind::Character, "", static_cast<std::int64_t>(
                                               static_cast<unsigned char>(value)),
                     line, column};
    }

    Token string_literal() {
        const int line = line_;
        const int column = column_;
        advance();
        std::string value;
        while (position_ < source_.size() && source_[position_] != '"') {
            char ch = source_[position_];
            advance();
            if (ch == '\\' && position_ < source_.size()) {
                const char escape = source_[position_];
                advance();
                switch (escape) {
                    case 'n': ch = '\n'; break;
                    case 't': ch = '\t'; break;
                    case 'r': ch = '\r'; break;
                    case '0': ch = '\0'; break;
                    case '\\': ch = '\\'; break;
                    case '"': ch = '"'; break;
                    default: ch = escape; break;
                }
            }
            value.push_back(ch);
        }
        if (position_ < source_.size()) {
            advance();
        }
        return Token{TokenKind::String, std::move(value), 0, line, column};
    }

    Token punctuation() {
        static const char* operators[] = {
            "<<=", ">>=", "...", "==", "!=", "<=", ">=", "&&", "||",
            "<<",  ">>",  "++",  "--", "+=", "-=", "*=", "/=", "%=",
            "&=",  "|=",  "^=",  "->", "##"};
        const int line = line_;
        const int column = column_;
        for (const char* op : operators) {
            const std::size_t length = std::string(op).size();
            if (source_.substr(position_, length) == op) {
                std::string text(op);
                for (std::size_t i = 0; i < length; ++i) {
                    advance();
                }
                return Token{TokenKind::Punct, std::move(text), 0, line, column};
            }
        }
        const char ch = source_[position_];
        advance();
        return Token{TokenKind::Punct, std::string(1, ch), 0, line, column};
    }

    std::string_view source_;
    std::size_t position_ = 0;
    int line_ = 1;
    int column_ = 1;
};

struct Expr {
    enum class Kind {
        Number, String, Identifier, Call, Binary, Unary, Assign, Index,
        Conditional, Sizeof, Member, InitList, Cast
    };
    Kind kind = Kind::Number;
    std::int64_t value = 0;
    std::string text;
    std::string op;
    std::shared_ptr<Type> type;
    std::vector<std::unique_ptr<Expr>> children;
    int line = 0;
};

struct Decl {
    std::string name;
    std::shared_ptr<Type> type;
    std::unique_ptr<Expr> init;
    bool is_static = false;
    int line = 0;
};

struct Stmt {
    enum class Kind {
        Empty, Expr, Return, Compound, If, While, DoWhile, For, Decl, Break,
        Continue, Switch, Goto, Label
    };
    Kind kind = Kind::Empty;
    std::string label;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> expr2;
    std::unique_ptr<Expr> expr3;
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Stmt> else_body;
    std::unique_ptr<Stmt> init;
    std::vector<std::unique_ptr<Stmt>> statements;
    std::vector<Decl> declarations;
    std::vector<std::unique_ptr<Stmt>> cases;
    std::vector<std::int64_t> case_values;
    std::vector<bool> case_defaults;
    int line = 0;
};

struct Function {
    std::string name;
    std::shared_ptr<Type> return_type;
    std::vector<Decl> parameters;
    std::unique_ptr<Stmt> body;
    bool variadic = false;
    int line = 0;
};

struct Global {
    std::string name;
    std::shared_ptr<Type> type;
    std::unique_ptr<Expr> init;
    int line = 0;
};

struct Program {
    std::vector<Global> globals;
    std::vector<Function> functions;
};

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string source_name,
           bool optimize = true)
        : tokens_(std::move(tokens)),
          source_name_(std::move(source_name)),
          optimize_(optimize) {}

    Program parse() {
        Program program;
        while (!at_end()) {
            if (is_type(peek().text)) {
                parse_external(program);
            } else {
                error("expected a declaration");
            }
        }
        return program;
    }

private:
    const Token& peek() const { return tokens_[position_]; }
    const Token& previous() const { return tokens_[position_ - 1]; }
    bool at_end() const { return peek().kind == TokenKind::Eof; }

    bool check(std::string_view text) const {
        return peek().text == text;
    }
    bool check_word(std::string_view text) const {
        return peek().kind == TokenKind::Identifier && peek().text == text;
    }
    bool accept(std::string_view text) {
        if (check(text)) {
            ++position_;
            return true;
        }
        return false;
    }
    void expect(std::string_view text) {
        if (!accept(text)) {
            error("expected '" + std::string(text) + "'");
        }
    }

    void error(const std::string& message) const {
        throw CompileError(peek().line, peek().column, message);
    }

    bool is_type(std::string_view text) const {
        return text == "int" || text == "char" || text == "void" ||
               text == "long" || text == "short" || text == "unsigned" ||
               text == "signed" || text == "const" || text == "static" ||
               text == "enum" || text == "typedef" || text == "struct" ||
               text == "union" || text == "_Bool" || text == "volatile" ||
               text == "restrict" || text == "auto" || text == "register" ||
               text == "inline" ||
               typedefs_.count(std::string(text)) != 0;
    }

    std::shared_ptr<Type> parse_base_type() {
        while (is_type(peek().text) &&
               (peek().text == "const" || peek().text == "static" ||
                peek().text == "unsigned" || peek().text == "signed" ||
                peek().text == "volatile" || peek().text == "restrict" ||
                peek().text == "auto" || peek().text == "register" ||
                peek().text == "inline")) {
            if (peek().text == "static") {
                pending_static_ = true;
            }
            ++position_;
        }
        if (peek().kind == TokenKind::Identifier) {
            const auto alias = typedefs_.find(peek().text);
            if (alias != typedefs_.end()) {
                const auto type = alias->second;
                ++position_;
                return type;
            }
        }
        if (accept("struct")) {
            std::string tag;
            if (peek().kind == TokenKind::Identifier) {
                tag = tokens_[position_++].text;
            }
            std::shared_ptr<Type> type;
            if (!tag.empty()) {
                const auto found = structs_.find(tag);
                if (found != structs_.end()) {
                    type = found->second;
                } else {
                    type = make_type(TypeKind::Struct);
                    structs_[tag] = type;
                }
            }
            if (accept("{")) {
                if (!type) {
                    type = make_type(TypeKind::Struct);
                }
                type->kind = TypeKind::Struct;
                type->members.clear();
                if (!tag.empty()) {
                    type->name = "struct " + tag;
                }
                while (!check("}") && !at_end()) {
                    auto member_type = parse_type();
                    const std::string member_name = parse_name();
                    member_type = parse_array_suffix(member_type);
                    expect(";");
                    const int align = member_type->size() > 1 ? 2 : 1;
                    int offset = type->size();
                    offset = (offset + align - 1) / align * align;
                    type->members.push_back(
                        Member{member_name, member_type, offset});
                }
                expect("}");
                return type;
            }
            if (!type) {
                error("expected a struct tag or body");
            }
            return type;
        }
        if (accept("union")) {
            std::string tag;
            if (peek().kind == TokenKind::Identifier) {
                tag = tokens_[position_++].text;
            }
            std::shared_ptr<Type> type;
            if (!tag.empty()) {
                const auto found = unions_.find(tag);
                if (found != unions_.end()) {
                    type = found->second;
                } else {
                    type = make_type(TypeKind::Union);
                    unions_[tag] = type;
                }
            }
            if (accept("{")) {
                if (!type) {
                    type = make_type(TypeKind::Union);
                }
                type->kind = TypeKind::Union;
                type->members.clear();
                if (!tag.empty()) {
                    type->name = "union " + tag;
                }
                while (!check("}") && !at_end()) {
                    auto member_type = parse_type();
                    const std::string member_name = parse_name();
                    member_type = parse_array_suffix(member_type);
                    expect(";");
                    type->members.push_back(Member{member_name, member_type, 0});
                }
                expect("}");
                return type;
            }
            if (!type) {
                error("expected a union tag or body");
            }
            return type;
        }
        if (accept("enum")) {
            std::string tag;
            if (peek().kind == TokenKind::Identifier) {
                tag = tokens_[position_++].text;
            }
            if (accept("{")) {
                std::int64_t next = 0;
                while (!check("}") && !at_end()) {
                    const std::string name = parse_name();
                    if (accept("=")) {
                        auto value = parse_expression();
                        if (value->kind != Expr::Kind::Number) {
                            error("enum value must be an integer constant");
                        }
                        next = value->value;
                    }
                    enum_constants_[name] = next++;
                    if (!accept(",")) {
                        break;
                    }
                }
                expect("}");
            }
            auto type = make_type(TypeKind::Int);
            if (!tag.empty()) {
                type->name = "enum " + tag;
            }
            return type;
        }
        if (accept("void")) return make_type(TypeKind::Void);
        if (accept("_Bool")) return make_type(TypeKind::Char);
        if (accept("char")) return make_type(TypeKind::Char);
        if (accept("int")) return make_type(TypeKind::Int);
        if (accept("short")) {
            accept("int");
            return make_type(TypeKind::Int);
        }
        if (accept("long")) {
            accept("long");
            accept("int");
            return make_type(TypeKind::Int);
        }
        error("expected a type");
        return make_type(TypeKind::Int);
    }

    std::shared_ptr<Type> parse_type() {
        auto type = parse_base_type();
        while (accept("*")) {
            type = make_type(TypeKind::Pointer, type);
            accept("restrict");
            accept("const");
        }
        return type;
    }

    std::shared_ptr<Type> parse_array_suffix(std::shared_ptr<Type> type) {
        while (accept("[")) {
            int count = 0;
            if (peek().kind == TokenKind::Number) {
                count = static_cast<int>(tokens_[position_++].value);
            } else {
                error("expected an array size");
            }
            expect("]");
            type = make_type(TypeKind::Array, type, count);
        }
        return type;
    }

    std::shared_ptr<Type> infer_array_size(std::shared_ptr<Type> type,
                                           const Expr* init) {
        if (type && type->kind == TypeKind::Array && type->count == 0 &&
            init != nullptr && init->kind == Expr::Kind::InitList) {
            return make_type(TypeKind::Array, type->base,
                             static_cast<int>(init->children.size()));
        }
        return type;
    }

    std::string parse_name() {
        if (peek().kind != TokenKind::Identifier) {
            error("expected an identifier");
        }
        return tokens_[position_++].text;
    }

    void parse_external(Program& program) {
        const int line = peek().line;
        if (accept("typedef")) {
            auto type = parse_type();
            const std::string alias = parse_name();
            expect(";");
            if (type->name.empty()) {
                type->name = alias;
            }
            typedefs_[alias] = std::move(type);
            return;
        }
        auto type = parse_type();
        if (accept(";")) {
            return;
        }
        const std::string name = parse_name();
        if (accept("(")) {
            Function function;
            function.name = name;
            function.return_type = type;
            function.line = line;
            if (!check(")")) {
                if (peek().text == "void" && tokens_[position_ + 1].text == ")") {
                    ++position_;
                } else {
                    for (;;) {
                        if (peek().text == "...") {
                            function.variadic = true;
                            ++position_;
                            break;
                        }
                        auto param_type = parse_type();
                        Decl parameter;
                        parameter.type = param_type;
                        if (peek().kind == TokenKind::Identifier) {
                            parameter.name = parse_name();
                            while (accept("[")) {
                                expect("]");
                                parameter.type = make_type(
                                    TypeKind::Pointer, parameter.type);
                            }
                        }
                        function.parameters.push_back(std::move(parameter));
                        if (!accept(",")) {
                            break;
                        }
                    }
                }
            }
            expect(")");
            if (accept(";")) {
                return;
            }
            function.body = parse_block();
            program.functions.push_back(std::move(function));
            return;
        }

        Global global;
        global.name = name;
        global.type = type;
        global.line = line;
        while (accept("[")) {
            int count = 0;
            if (!check("]")) {
                const auto value = parse_expression();
                if (value->kind != Expr::Kind::Number) {
                    error("array size must be constant");
                }
                count = static_cast<int>(value->value);
            }
            expect("]");
            global.type = make_type(TypeKind::Array, global.type, count);
        }
        if (accept("=")) {
            global.init = parse_initializer();
            global.type = infer_array_size(global.type, global.init.get());
        }
        expect(";");
        program.globals.push_back(std::move(global));
    }

    std::unique_ptr<Stmt> parse_block() {
        auto block = std::make_unique<Stmt>();
        block->kind = Stmt::Kind::Compound;
        block->line = peek().line;
        expect("{");
        while (!check("}") && !at_end()) {
            block->statements.push_back(parse_statement());
        }
        expect("}");
        return block;
    }

    std::unique_ptr<Stmt> parse_statement() {
        if (check("{")) {
            return parse_block();
        }
        if (accept(";")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Empty;
            return stmt;
        }
        if (peek().kind == TokenKind::Identifier &&
            position_ + 1 < tokens_.size() &&
            tokens_[position_ + 1].text == ":") {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Label;
            stmt->label = parse_name();
            expect(":");
            stmt->body = parse_statement();
            return stmt;
        }
        if (accept("goto")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Goto;
            stmt->line = peek().line;
            stmt->label = parse_name();
            expect(";");
            return stmt;
        }
        if (is_type(peek().text)) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Decl;
            stmt->line = peek().line;
            pending_static_ = false;
            auto type = parse_type();
            const bool is_static = pending_static_;
            pending_static_ = false;
            if (accept(";")) {
                stmt->kind = Stmt::Kind::Empty;
                return stmt;
            }
            for (;;) {
                Decl decl;
                decl.name = parse_name();
                decl.type = type;
                decl.is_static = is_static;
                decl.line = peek().line;
                if (accept("[")) {
                    int count = 0;
                    if (!check("]")) {
                        const auto value = parse_expression();
                        if (value->kind != Expr::Kind::Number) {
                            error("array size must be constant");
                        }
                        count = static_cast<int>(value->value);
                    }
                    expect("]");
                    decl.type = make_type(TypeKind::Array, decl.type, count);
                }
                if (accept("=")) {
                    decl.init = parse_initializer();
                    decl.type = infer_array_size(decl.type, decl.init.get());
                }
                stmt->declarations.push_back(std::move(decl));
                if (!accept(",")) {
                    break;
                }
            }
            expect(";");
            return stmt;
        }
        if (accept("return")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Return;
            stmt->line = peek().line;
            if (!check(";")) {
                stmt->expr = parse_expression();
            }
            expect(";");
            return stmt;
        }
        if (accept("if")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::If;
            stmt->line = peek().line;
            expect("(");
            stmt->expr = parse_expression();
            expect(")");
            stmt->body = parse_statement();
            if (accept("else")) {
                stmt->else_body = parse_statement();
            }
            return stmt;
        }
        if (accept("switch")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Switch;
            stmt->line = peek().line;
            expect("(");
            stmt->expr = parse_expression();
            expect(")");
            expect("{");
            while (!check("}") && !at_end()) {
                bool is_default = false;
                std::int64_t value = 0;
                if (accept("case")) {
                    auto label = parse_expression();
                    if (label->kind != Expr::Kind::Number) {
                        error("case label must be an integer constant");
                    }
                    value = label->value;
                    expect(":");
                } else if (accept("default")) {
                    is_default = true;
                    expect(":");
                } else {
                    error("expected case or default");
                }
                auto body = std::make_unique<Stmt>();
                body->kind = Stmt::Kind::Compound;
                body->line = peek().line;
                while (!check("case") && !check("default") &&
                       !check("}") && !at_end()) {
                    body->statements.push_back(parse_statement());
                }
                stmt->cases.push_back(std::move(body));
                stmt->case_values.push_back(value);
                stmt->case_defaults.push_back(is_default);
            }
            expect("}");
            return stmt;
        }
        if (accept("while")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::While;
            stmt->line = peek().line;
            expect("(");
            stmt->expr = parse_expression();
            expect(")");
            stmt->body = parse_statement();
            return stmt;
        }
        if (accept("do")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::DoWhile;
            stmt->line = peek().line;
            stmt->body = parse_statement();
            expect("while");
            expect("(");
            stmt->expr = parse_expression();
            expect(")");
            expect(";");
            return stmt;
        }
        if (accept("for")) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::For;
            stmt->line = peek().line;
            expect("(");
            if (!check(";")) {
                if (is_type(peek().text)) {
                    auto init = std::make_unique<Stmt>();
                    init->kind = Stmt::Kind::Decl;
                    auto type = parse_type();
                    Decl decl;
                    decl.name = parse_name();
                    decl.type = type;
                    if (accept("=")) {
                        decl.init = parse_initializer();
                    decl.type = infer_array_size(decl.type, decl.init.get());
                    }
                    init->declarations.push_back(std::move(decl));
                    expect(";");
                    stmt->init = std::move(init);
                } else {
                    auto init = std::make_unique<Stmt>();
                    init->kind = Stmt::Kind::Expr;
                    init->expr = parse_expression();
                    expect(";");
                    stmt->init = std::move(init);
                }
            } else {
                expect(";");
            }
            if (!check(";")) {
                stmt->expr = parse_expression();
            }
            expect(";");
            if (!check(")")) {
                stmt->expr2 = parse_expression();
            }
            expect(")");
            stmt->body = parse_statement();
            return stmt;
        }
        if (accept("break")) {
            expect(";");
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Break;
            stmt->line = peek().line;
            return stmt;
        }
        if (accept("continue")) {
            expect(";");
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Continue;
            stmt->line = peek().line;
            return stmt;
        }
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = Stmt::Kind::Expr;
        stmt->line = peek().line;
        stmt->expr = parse_expression();
        expect(";");
        return stmt;
    }

    std::unique_ptr<Expr> parse_initializer() {
        if (!accept("{")) {
            return parse_expression();
        }
        auto list = std::make_unique<Expr>();
        list->kind = Expr::Kind::InitList;
        list->line = peek().line;
        while (!check("}") && !at_end()) {
            list->children.push_back(parse_initializer());
            if (!accept(",")) {
                break;
            }
        }
        expect("}");
        return list;
    }

    std::unique_ptr<Expr> parse_expression() { return parse_assignment(); }

    std::unique_ptr<Expr> make_number(std::int64_t value, int line) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Number;
        expr->value = value;
        expr->line = line;
        return expr;
    }

    static bool fold_binary_values(const std::string& op, std::int64_t left,
                                   std::int64_t right,
                                   std::int64_t& result) {
        const auto a = static_cast<std::uint16_t>(left);
        const auto b = static_cast<std::uint16_t>(right);
        const auto sa = static_cast<std::int16_t>(a);
        const auto sb = static_cast<std::int16_t>(b);
        if (op == "+") {
            result = static_cast<std::uint16_t>(a + b);
        } else if (op == "-") {
            result = static_cast<std::uint16_t>(a - b);
        } else if (op == "*") {
            result = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(a) *
                static_cast<std::uint32_t>(b));
        } else if (op == "/") {
            if (b == 0) {
                return false;
            }
            result = static_cast<std::uint16_t>(
                static_cast<std::int32_t>(sa) /
                static_cast<std::int32_t>(sb));
        } else if (op == "%") {
            if (b == 0) {
                return false;
            }
            result = static_cast<std::uint16_t>(
                static_cast<std::int32_t>(sa) %
                static_cast<std::int32_t>(sb));
        } else if (op == "&") {
            result = static_cast<std::uint16_t>(a & b);
        } else if (op == "|") {
            result = static_cast<std::uint16_t>(a | b);
        } else if (op == "^") {
            result = static_cast<std::uint16_t>(a ^ b);
        } else if (op == "<<") {
            result = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(a << (b & 0x0F)));
        } else if (op == ">>") {
            result = static_cast<std::uint16_t>(sa >> (b & 0x0F));
        } else if (op == "==") {
            result = a == b ? 1 : 0;
        } else if (op == "!=") {
            result = a != b ? 1 : 0;
        } else if (op == "<") {
            result = sa < sb ? 1 : 0;
        } else if (op == ">") {
            result = sa > sb ? 1 : 0;
        } else if (op == "<=") {
            result = sa <= sb ? 1 : 0;
        } else if (op == ">=") {
            result = sa >= sb ? 1 : 0;
        } else if (op == "&&") {
            result = a != 0 && b != 0 ? 1 : 0;
        } else if (op == "||") {
            result = a != 0 || b != 0 ? 1 : 0;
        } else {
            return false;
        }
        return true;
    }

    std::unique_ptr<Expr> make_binary(std::string op, std::unique_ptr<Expr> left,
                                      std::unique_ptr<Expr> right, int line) {
        if (optimize_ && left && right &&
            left->kind == Expr::Kind::Number &&
            right->kind == Expr::Kind::Number) {
            std::int64_t value = 0;
            if (fold_binary_values(op, left->value, right->value, value)) {
                return make_number(value, line);
            }
        }
        if (optimize_ && right && right->kind == Expr::Kind::Number &&
            right->value == 0) {
            if (op == "+" || op == "-" || op == "|" || op == "^" ||
                op == "<<" || op == ">>") {
                return left;
            }
        }
        if (optimize_ && left && left->kind == Expr::Kind::Number &&
            left->value == 0 && (op == "+" || op == "|" || op == "^")) {
            return right;
        }
        if (optimize_ && right && right->kind == Expr::Kind::Number &&
            right->value == 1 && (op == "*" || op == "/")) {
            return left;
        }
        if (optimize_ && left && left->kind == Expr::Kind::Number &&
            left->value == 1 && op == "*") {
            return right;
        }
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Binary;
        expr->op = std::move(op);
        expr->line = line;
        expr->children.push_back(std::move(left));
        expr->children.push_back(std::move(right));
        return expr;
    }

    std::unique_ptr<Expr> clone_expr(const Expr* expr) {
        if (expr == nullptr) {
            return nullptr;
        }
        auto copy = std::make_unique<Expr>();
        copy->kind = expr->kind;
        copy->value = expr->value;
        copy->text = expr->text;
        copy->op = expr->op;
        copy->line = expr->line;
        for (const auto& child : expr->children) {
            copy->children.push_back(clone_expr(child.get()));
        }
        return copy;
    }

    std::unique_ptr<Expr> parse_assignment() {
        auto left = parse_conditional();
        static const char* compounds[] = {"+=", "-=", "*=", "/=", "%=",
                                          "&=", "|=", "^=", "<<=", ">>="};
        for (const char* compound : compounds) {
            if (!check(compound)) {
                continue;
            }
            const int line = peek().line;
            ++position_;
            auto right = parse_assignment();
            auto binary = make_binary(std::string(compound).substr(
                                          0, std::string(compound).size() - 1),
                                      clone_expr(left.get()), std::move(right),
                                      line);
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Assign;
            expr->line = line;
            expr->children.push_back(std::move(left));
            expr->children.push_back(std::move(binary));
            return expr;
        }
        if (check("=")) {
            const int line = peek().line;
            ++position_;
            auto right = parse_assignment();
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Assign;
            expr->line = line;
            expr->children.push_back(std::move(left));
            expr->children.push_back(std::move(right));
            return expr;
        }
        return left;
    }

    std::unique_ptr<Expr> parse_conditional() {
        auto condition = parse_logical_or();
        if (accept("?")) {
            auto yes = parse_expression();
            expect(":");
            auto no = parse_conditional();
            if (optimize_ && condition->kind == Expr::Kind::Number) {
                return condition->value != 0 ? std::move(yes) : std::move(no);
            }
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Conditional;
            expr->line = condition->line;
            expr->children.push_back(std::move(condition));
            expr->children.push_back(std::move(yes));
            expr->children.push_back(std::move(no));
            return expr;
        }
        return condition;
    }

    std::unique_ptr<Expr> parse_logical_or() {
        auto expr = parse_logical_and();
        while (check("||")) {
            const int line = peek().line;
            ++position_;
            expr = make_binary("||", std::move(expr), parse_logical_and(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_logical_and() {
        auto expr = parse_bitwise_or();
        while (check("&&")) {
            const int line = peek().line;
            ++position_;
            expr = make_binary("&&", std::move(expr), parse_bitwise_or(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_bitwise_or() {
        auto expr = parse_bitwise_xor();
        while (check("|")) {
            const int line = peek().line;
            ++position_;
            expr = make_binary("|", std::move(expr), parse_bitwise_xor(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_bitwise_xor() {
        auto expr = parse_bitwise_and();
        while (check("^")) {
            const int line = peek().line;
            ++position_;
            expr = make_binary("^", std::move(expr), parse_bitwise_and(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_bitwise_and() {
        auto expr = parse_equality();
        while (check("&")) {
            const int line = peek().line;
            ++position_;
            expr = make_binary("&", std::move(expr), parse_equality(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_equality() {
        auto expr = parse_relational();
        while (check("==") || check("!=")) {
            const std::string op = peek().text;
            const int line = peek().line;
            ++position_;
            expr = make_binary(op, std::move(expr), parse_relational(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_relational() {
        auto expr = parse_shift();
        while (check("<") || check(">") || check("<=") || check(">=")) {
            const std::string op = peek().text;
            const int line = peek().line;
            ++position_;
            expr = make_binary(op, std::move(expr), parse_shift(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_shift() {
        auto expr = parse_additive();
        while (check("<<") || check(">>")) {
            const std::string op = peek().text;
            const int line = peek().line;
            ++position_;
            expr = make_binary(op, std::move(expr), parse_additive(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_additive() {
        auto expr = parse_multiplicative();
        while (check("+") || check("-")) {
            const std::string op = peek().text;
            const int line = peek().line;
            ++position_;
            expr = make_binary(op, std::move(expr), parse_multiplicative(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_multiplicative() {
        auto expr = parse_unary();
        while (check("*") || check("/") || check("%")) {
            const std::string op = peek().text;
            const int line = peek().line;
            ++position_;
            expr = make_binary(op, std::move(expr), parse_unary(), line);
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_unary() {
        if (check("(") && position_ + 1 < tokens_.size() &&
            is_type(tokens_[position_ + 1].text)) {
            const int line = peek().line;
            ++position_;
            auto type = parse_type();
            expect(")");
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Cast;
            expr->type = std::move(type);
            expr->line = line;
            expr->children.push_back(parse_unary());
            if (optimize_ && expr->children[0]->kind == Expr::Kind::Number &&
                expr->type && expr->type->kind == TypeKind::Char) {
                return make_number(
                    static_cast<std::uint16_t>(expr->children[0]->value) &
                        0xFF,
                    line);
            }
            return expr;
        }
        if (check("+") || check("-") || check("!") || check("~") ||
            check("*") || check("&") || check("++") || check("--") ||
            check_word("sizeof")) {
            const std::string op = peek().text;
            const int line = peek().line;
            ++position_;
            if (op == "sizeof") {
                auto expr = std::make_unique<Expr>();
                expr->kind = Expr::Kind::Sizeof;
                expr->line = line;
                if (check("(") && is_type(tokens_[position_ + 1].text)) {
                    ++position_;
                    auto type = parse_type();
                    expect(")");
                    expr->value = type->size();
                } else {
                    expr->children.push_back(parse_unary());
                }
                return expr;
            }
            auto operand = parse_unary();
            if (optimize_ && operand->kind == Expr::Kind::Number) {
                const auto value = static_cast<std::uint16_t>(operand->value);
                if (op == "+") {
                    return operand;
                }
                if (op == "-") {
                    return make_number(
                        static_cast<std::uint16_t>(0u - value), line);
                }
                if (op == "~") {
                    return make_number(
                        static_cast<std::uint16_t>(~value), line);
                }
                if (op == "!") {
                    return make_number(value == 0 ? 1 : 0, line);
                }
            }
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Unary;
            expr->op = op;
            expr->line = line;
            expr->children.push_back(std::move(operand));
            return expr;
        }
        return parse_postfix();
    }

    std::unique_ptr<Expr> parse_postfix() {
        auto expr = parse_primary();
        for (;;) {
            if (accept("(")) {
                auto call = std::make_unique<Expr>();
                call->kind = Expr::Kind::Call;
                call->line = expr->line;
                call->text = expr->text;
                if (!check(")")) {
                    for (;;) {
                        call->children.push_back(parse_expression());
                        if (!accept(",")) {
                            break;
                        }
                    }
                }
                expect(")");
                expr = std::move(call);
            } else if (accept("[")) {
                auto index = std::make_unique<Expr>();
                index->kind = Expr::Kind::Index;
                index->line = expr->line;
                index->children.push_back(std::move(expr));
                index->children.push_back(parse_expression());
                expect("]");
                expr = std::move(index);
            } else if (accept(".")) {
                auto member = std::make_unique<Expr>();
                member->kind = Expr::Kind::Member;
                member->op = ".";
                member->line = expr->line;
                member->text = parse_name();
                member->children.push_back(std::move(expr));
                expr = std::move(member);
            } else if (accept("->")) {
                auto member = std::make_unique<Expr>();
                member->kind = Expr::Kind::Member;
                member->op = "->";
                member->line = expr->line;
                member->text = parse_name();
                member->children.push_back(std::move(expr));
                expr = std::move(member);
            } else if (check("++") || check("--")) {
                const std::string op = peek().text;
                ++position_;
                auto unary = std::make_unique<Expr>();
                unary->kind = Expr::Kind::Unary;
                unary->op = op;
                unary->line = expr->line;
                unary->children.push_back(std::move(expr));
                expr = std::move(unary);
            } else {
                break;
            }
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_primary() {
        if (peek().kind == TokenKind::Number ||
            peek().kind == TokenKind::Character) {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Number;
            expr->value = peek().value;
            expr->line = peek().line;
            ++position_;
            return expr;
        }
        if (peek().kind == TokenKind::String) {
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::String;
            expr->text = peek().text;
            expr->line = peek().line;
            ++position_;
            return expr;
        }
        if (peek().kind == TokenKind::Identifier) {
            const auto constant = enum_constants_.find(peek().text);
            if (constant != enum_constants_.end()) {
                auto expr = std::make_unique<Expr>();
                expr->kind = Expr::Kind::Number;
                expr->value = constant->second;
                expr->line = peek().line;
                ++position_;
                return expr;
            }
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Identifier;
            expr->text = peek().text;
            expr->line = peek().line;
            ++position_;
            return expr;
        }
        if (accept("(")) {
            auto expr = parse_expression();
            expect(")");
            return expr;
        }
        error("expected an expression");
        return nullptr;
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::string source_name_;
    bool optimize_ = true;
    bool pending_static_ = false;
    std::unordered_map<std::string, std::int64_t> enum_constants_;
    std::unordered_map<std::string, std::shared_ptr<Type>> typedefs_;
    std::unordered_map<std::string, std::shared_ptr<Type>> structs_;
    std::unordered_map<std::string, std::shared_ptr<Type>> unions_;
};

class Codegen {
public:
    Codegen(Program program, std::string source_name, bool debug,
            bool optimize = true)
        : program_(std::move(program)),
          source_name_(std::move(source_name)),
          debug_(debug),
          optimize_(optimize) {}

    std::string run() {
        collect_globals();
        std::ostringstream out;
        out << "; generated by kr16 cc from " << source_name_ << "\n";
        if (debug_) {
            out << ".file 0 \"" << escape_debug_path(source_name_)
                << "\"\n";
        }
        out << ".text\n";
        for (auto& function : program_.functions) {
            compile_function(function, out);
        }
        if (function_map_.count("main") != 0 &&
            function_map_.count("_start") == 0) {
            out << ".crt0\n";
            out << ".global _start\n";
            out << "_start:\n";
            out << "  CALL main\n";
            out << "  HLT\n";
            out << ".text\n";
        }
        for (const auto& name : external_calls_) {
            if (function_map_.count(name) == 0 && global_map_.count(name) == 0) {
                out << ".extern " << name << "\n";
            }
        }
        emit_globals(out);
        emit_strings(out);
        if (debug_ && !debug_vars_.empty()) {
            out << ".section .debug_vars\n";
            for (const auto& variable : debug_vars_) {
                out << ".asciz \"" << variable.function << "|"
                    << variable.name << "|" << variable.offset << "|"
                    << variable.size << "|" << variable.type << "\"\n";
            }
            out << ".text\n";
        }
        if (debug_ && !program_.globals.empty()) {
            out << ".section .debug_globals\n";
            for (const auto& global : program_.globals) {
                out << ".asciz \"" << global.name << "|"
                    << type_text(global.type) << "\"\n";
            }
            out << ".text\n";
        }
        if (debug_ && !debug_frames_.empty()) {
            out << ".section .debug_frames\n";
            for (const auto& frame : debug_frames_) {
                out << ".asciz \"" << frame.function << "|" << frame.size
                    << "\"\n";
            }
            out << ".text\n";
        }
        if (debug_ && !debug_types_.empty()) {
            out << ".section .debug_types\n";
            for (const auto& type : debug_types_) {
                out << ".asciz \"" << type.name << "|" << type.size << "|"
                    << type.fields << "\"\n";
            }
            out << ".text\n";
        }
        return out.str();
    }

private:
    struct Variable {
        std::shared_ptr<Type> type;
        int offset = 0;
        bool global = false;
        bool indirect = false;
        std::string name;
        std::string symbol;
    };
    struct DebugVar {
        std::string function;
        std::string name;
        int offset = 0;
        int size = 2;
        std::string type;
    };
    struct DebugFrame {
        std::string function;
        int size = 0;
    };
    struct DebugType {
        std::string name;
        int size = 0;
        std::string fields;
    };

    std::string type_text(const std::shared_ptr<Type>& type) const {
        if (!type) {
            return "?";
        }
        switch (type->kind) {
            case TypeKind::Void:
                return "void";
            case TypeKind::Char:
                return "char";
            case TypeKind::Int:
                return "int";
            case TypeKind::Pointer:
                return type_text(type->base) + " *";
            case TypeKind::Array:
                return type_text(type->base) + "[" +
                       std::to_string(type->count) + "]";
            case TypeKind::Struct:
                if (type->name.rfind("struct ", 0) == 0) {
                    return type->name;
                }
                return "struct " +
                       (type->name.empty() ? std::string("?") : type->name);
            case TypeKind::Union:
                if (type->name.rfind("union ", 0) == 0) {
                    return type->name;
                }
                return "union " +
                       (type->name.empty() ? std::string("?") : type->name);
        }
        return "?";
    }

    void record_type(const std::shared_ptr<Type>& type) {
        if (!type) {
            return;
        }
        if (type->kind == TypeKind::Pointer ||
            type->kind == TypeKind::Array) {
            record_type(type->base);
            return;
        }
        if (type->name.empty()) {
            return;
        }
        for (const auto& existing : debug_types_) {
            if (existing.name == type->name) {
                return;
            }
        }
        std::string fields;
        if (type->kind == TypeKind::Struct ||
            type->kind == TypeKind::Union) {
            for (const auto& member : type->members) {
                if (!member.type) {
                    continue;
                }
                fields += member.name + "@" + std::to_string(member.offset) +
                          ":" + type_text(member.type) + ":" +
                          std::to_string(member.type->size()) + ";";
            }
        }
        debug_types_.push_back(
            DebugType{type->name, type->size(), std::move(fields)});
        for (const auto& member : type->members) {
            record_type(member.type);
        }
    }

    void collect_globals() {
        for (auto& global : program_.globals) {
            global_map_[global.name] = &global;
            if (debug_) {
                record_type(global.type);
            }
        }
        for (auto& function : program_.functions) {
            function_map_[function.name] = &function;
        }
    }

    int emit_initializer(const Type* type, const Expr* list, int base,
                         int cursor, std::ostringstream& out) {
        for (std::size_t i = 0; i < list->children.size(); ++i) {
            int offset = 0;
            const Type* element_type = type;
            if (type->kind == TypeKind::Struct ||
                type->kind == TypeKind::Union) {
                if (i >= type->members.size()) {
                    break;
                }
                offset = type->members[i].offset;
                element_type = type->members[i].type.get();
            } else {
                offset = static_cast<int>(i) *
                         (type->base ? type->base->size() : 2);
                element_type = type->base.get();
            }
            const int target = base + offset;
            if (target > cursor) {
                out << "  .space " << (target - cursor) << "\n";
                cursor = target;
            }
            const auto* child = list->children[i].get();
            if (child->kind == Expr::Kind::InitList && element_type != nullptr &&
                (element_type->kind == TypeKind::Array ||
                 element_type->kind == TypeKind::Struct ||
                 element_type->kind == TypeKind::Union)) {
                cursor = emit_initializer(element_type, child, target, cursor,
                                          out);
                continue;
            }
            if (child->kind == Expr::Kind::Number) {
                out << (element_type != nullptr && element_type->is_char()
                            ? "  .byte "
                            : "  .word ")
                    << child->value << "\n";
            } else {
                out << "  .word 0\n";
            }
            cursor = target + (element_type != nullptr
                                   ? element_type->size()
                                   : 2);
        }
        return cursor;
    }

    void emit_globals(std::ostringstream& out) {
        for (const auto& global : program_.globals) {
            if (global.init && global.init->kind == Expr::Kind::Number) {
                out << ".data\n";
                out << ".align 2\n";
                out << ".global " << global.name << "\n";
                out << global.name << ":\n";
                if (global.type->is_char()) {
                    out << "  .byte " << global.init->value << "\n";
                } else {
                    out << "  .word " << global.init->value << "\n";
                }
            } else if (global.init &&
                       global.init->kind == Expr::Kind::InitList &&
                       (global.type->kind == TypeKind::Struct ||
                        global.type->kind == TypeKind::Union ||
                        global.type->kind == TypeKind::Array)) {
                out << ".data\n";
                out << ".align 2\n";
                out << ".global " << global.name << "\n";
                out << global.name << ":\n";
                int cursor =
                    emit_initializer(global.type.get(), global.init.get(), 0,
                                     0, out);
                if (cursor < global.type->size()) {
                    out << "  .space " << (global.type->size() - cursor) << "\n";
                }
            } else if (global.init &&
                       global.init->kind == Expr::Kind::String &&
                       global.type->size() == 2) {
                out << ".data\n";
                out << ".align 2\n";
                out << ".global " << global.name << "\n";
                out << global.name << ":\n";
                out << "  .word " << string_label(global.init->text) << "\n";
            } else {
                out << ".bss\n";
                out << ".align 2\n";
                out << ".global " << global.name << "\n";
                out << global.name << ":\n";
                out << "  .space " << global.type->size() << "\n";
            }
        }
    }

    void emit_strings(std::ostringstream& out) {
        if (string_literals_.empty()) {
            return;
        }
        out << ".rodata\n";
        for (const auto& [value, label] : string_literals_) {
            out << label << ":\n";
            out << "  .asciz \"";
            for (const char ch : value) {
                switch (ch) {
                    case '\n': out << "\\n"; break;
                    case '\t': out << "\\t"; break;
                    case '\r': out << "\\r"; break;
                    case '\\': out << "\\\\"; break;
                    case '"': out << "\\\""; break;
                    default: out << ch; break;
                }
            }
            out << "\"\n";
        }
    }

    std::string string_label(const std::string& value) {
        const auto it = string_literals_.find(value);
        if (it != string_literals_.end()) {
            return it->second;
        }
        const std::string label = ".Lstr" + std::to_string(string_literals_.size());
        string_literals_.emplace(value, label);
        return label;
    }

    std::string new_label(const std::string& prefix) {
        return ".L" + prefix + std::to_string(label_counter_++);
    }

    void collect_locals(const Stmt* stmt, std::vector<const Decl*>& declarations) {
        if (stmt == nullptr) {
            return;
        }
        if (stmt->kind == Stmt::Kind::Decl) {
            for (const auto& decl : stmt->declarations) {
                declarations.push_back(&decl);
            }
        }
        for (const auto& child : stmt->statements) {
            collect_locals(child.get(), declarations);
        }
        collect_locals(stmt->body.get(), declarations);
        collect_locals(stmt->else_body.get(), declarations);
        collect_locals(stmt->init.get(), declarations);
    }

    enum class IrOp {
        Const, Move, Binary, Unary, Char, Assign, Param, BranchZero,
        BranchCompare, Jump, Return
    };

    struct IrValue {
        bool variable = false;
        bool character = false;
        std::string name;
        int reg = -1;
    };

    struct IrInst {
        IrOp op = IrOp::Move;
        int dst = -1;
        int lhs = -1;
        int rhs = -1;
        std::int64_t imm = 0;
        bool immediate = false;
        std::string text;
        int target = -1;
        int line = 0;
    };

    struct IrBlock {
        std::vector<IrInst> insts;
        std::vector<int> succ;
        std::set<int> use;
        std::set<int> def;
        std::set<int> live_in;
        std::set<int> live_out;
    };

    static bool ir_scalar_type(const std::shared_ptr<Type>& type) {
        return type && (type->kind == TypeKind::Void ||
                        type->kind == TypeKind::Char ||
                        type->kind == TypeKind::Int);
    }

    int ir_new_value(bool variable, std::string name, bool character) {
        IrValue value;
        value.variable = variable;
        value.character = character;
        value.name = std::move(name);
        ir_values_.push_back(std::move(value));
        return static_cast<int>(ir_values_.size()) - 1;
    }

    int ir_temp(bool character = false) {
        return ir_new_value(false, {}, character);
    }

    int ir_value_for_name(const std::string& name) {
        const auto it = ir_vars_.find(name);
        return it == ir_vars_.end() ? -1 : it->second;
    }

    int ir_const(std::int64_t value, int line) {
        const int dst = ir_temp();
        IrInst inst;
        inst.op = IrOp::Const;
        inst.dst = dst;
        inst.imm = value;
        inst.line = line;
        ir_inst(inst);
        return dst;
    }

    void ir_inst(IrInst inst) {
        ir_blocks_[static_cast<std::size_t>(ir_current_)].insts.push_back(
            std::move(inst));
    }

    int ir_add_block() {
        ir_blocks_.push_back(IrBlock{});
        return static_cast<int>(ir_blocks_.size()) - 1;
    }

    void ir_switch_block(int block) {
        ir_current_ = block;
        ir_terminated_ = false;
    }

    void ir_jump(int target) {
        IrInst inst;
        inst.op = IrOp::Jump;
        inst.target = target;
        ir_inst(std::move(inst));
        ir_terminated_ = true;
    }

    bool can_ir_expr(const Expr* expr) const {
        if (expr == nullptr) {
            return false;
        }
        switch (expr->kind) {
            case Expr::Kind::Number:
                return true;
            case Expr::Kind::Sizeof:
                return true;
            case Expr::Kind::Identifier: {
                const auto it = locals_.find(expr->text);
                return it != locals_.end() && ir_scalar_type(it->second.type) &&
                       !it->second.global && !it->second.indirect &&
                       it->second.symbol.empty();
            }
            case Expr::Kind::Cast:
                return expr->children.size() == 1 &&
                       ir_scalar_type(expr->type) &&
                       can_ir_expr(expr->children[0].get());
            case Expr::Kind::Unary:
                return expr->children.size() == 1 &&
                       (expr->op == "+" || expr->op == "-" || expr->op == "~") &&
                       can_ir_expr(expr->children[0].get());
            case Expr::Kind::Binary:
                return expr->children.size() == 2 &&
                       expr->op != "&&" && expr->op != "||" &&
                       can_ir_expr(expr->children[0].get()) &&
                       can_ir_expr(expr->children[1].get());
            default:
                return false;
        }
    }

    bool can_ir_stmt(const Stmt* stmt) const {
        if (stmt == nullptr) {
            return true;
        }
        switch (stmt->kind) {
            case Stmt::Kind::Empty:
                return true;
            case Stmt::Kind::Compound:
                for (const auto& child : stmt->statements) {
                    if (!can_ir_stmt(child.get())) {
                        return false;
                    }
                }
                return true;
            case Stmt::Kind::Decl:
                for (const auto& decl : stmt->declarations) {
                    if (decl.is_static || !ir_scalar_type(decl.type)) {
                        return false;
                    }
                    if (decl.init && !can_ir_expr(decl.init.get())) {
                        return false;
                    }
                }
                return true;
            case Stmt::Kind::Expr: {
                if (stmt->expr == nullptr) {
                    return false;
                }
                if (stmt->expr->kind == Expr::Kind::Assign) {
                    if (stmt->expr->children.size() != 2 ||
                        stmt->expr->children[0]->kind !=
                            Expr::Kind::Identifier) {
                        return false;
                    }
                    const auto it =
                        locals_.find(stmt->expr->children[0]->text);
                    return it != locals_.end() &&
                           ir_scalar_type(it->second.type) &&
                           !it->second.global && !it->second.indirect &&
                           it->second.symbol.empty() &&
                           can_ir_expr(stmt->expr->children[1].get());
                }
                if (stmt->expr->kind == Expr::Kind::Unary &&
                    (stmt->expr->op == "++" || stmt->expr->op == "--") &&
                    stmt->expr->children.size() == 1 &&
                    stmt->expr->children[0]->kind ==
                        Expr::Kind::Identifier) {
                    const auto it =
                        locals_.find(stmt->expr->children[0]->text);
                    return it != locals_.end() &&
                           ir_scalar_type(it->second.type) &&
                           !it->second.global && !it->second.indirect &&
                           it->second.symbol.empty();
                }
                return false;
            }
            case Stmt::Kind::Return:
                return stmt->expr == nullptr || can_ir_expr(stmt->expr.get());
            case Stmt::Kind::If:
                return can_ir_expr(stmt->expr.get()) &&
                       can_ir_stmt(stmt->body.get()) &&
                       can_ir_stmt(stmt->else_body.get());
            case Stmt::Kind::While:
                return can_ir_expr(stmt->expr.get()) &&
                       can_ir_stmt(stmt->body.get());
            case Stmt::Kind::DoWhile:
                return can_ir_expr(stmt->expr.get()) &&
                       can_ir_stmt(stmt->body.get());
            case Stmt::Kind::For:
                return can_ir_stmt(stmt->init.get()) &&
                       (stmt->expr == nullptr ||
                        can_ir_expr(stmt->expr.get())) &&
                       (stmt->expr2 == nullptr ||
                        can_ir_stmt_expr(stmt->expr2.get())) &&
                       can_ir_stmt(stmt->body.get());
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue:
                return true;
            default:
                return false;
        }
    }

    bool can_ir_stmt_expr(const Expr* expr) const {
        if (expr == nullptr) {
            return true;
        }
        if (expr->kind == Expr::Kind::Assign) {
            if (expr->children.size() != 2 ||
                expr->children[0]->kind != Expr::Kind::Identifier) {
                return false;
            }
            const auto it = locals_.find(expr->children[0]->text);
            return it != locals_.end() &&
                   ir_scalar_type(it->second.type) && !it->second.global &&
                   !it->second.indirect && it->second.symbol.empty() &&
                   can_ir_expr(expr->children[1].get());
        }
        if (expr->kind == Expr::Kind::Unary &&
            (expr->op == "++" || expr->op == "--") &&
            expr->children.size() == 1 &&
            expr->children[0]->kind == Expr::Kind::Identifier) {
            const auto it = locals_.find(expr->children[0]->text);
            return it != locals_.end() &&
                   ir_scalar_type(it->second.type) && !it->second.global &&
                   !it->second.indirect && it->second.symbol.empty();
        }
        return false;
    }

    bool can_ir_function(const Function& function) const {
        if (debug_ || function.variadic || function.parameters.size() > 3 ||
            !ir_scalar_type(function.return_type)) {
            return false;
        }
        for (const auto& parameter : function.parameters) {
            if (!ir_scalar_type(parameter.type)) {
                return false;
            }
        }
        for (const auto& [name, variable] : locals_) {
            (void)name;
            if (!ir_scalar_type(variable.type) || variable.global ||
                variable.indirect || !variable.symbol.empty()) {
                return false;
            }
        }
        return can_ir_stmt(function.body.get());
    }

    void ir_coalesce_assignments() {
        std::vector<int> uses(ir_values_.size(), 0);
        for (const auto& block : ir_blocks_) {
            for (const auto& inst : block.insts) {
                auto [inst_uses, inst_defs] = ir_inst_uses_defs(inst);
                (void)inst_defs;
                for (const int value : inst_uses) {
                    if (value >= 0 &&
                        value < static_cast<int>(uses.size())) {
                        ++uses[static_cast<std::size_t>(value)];
                    }
                }
            }
        }
        for (auto& block : ir_blocks_) {
            for (std::size_t i = 0; i + 1 < block.insts.size();) {
                auto& def = block.insts[i];
                auto& assign = block.insts[i + 1];
                if (assign.op != IrOp::Assign || assign.dst < 0 ||
                    assign.lhs != def.dst || def.dst < 0 ||
                    uses[static_cast<std::size_t>(def.dst)] != 1 ||
                    ir_values_[static_cast<std::size_t>(assign.dst)]
                        .character ||
                    (def.op != IrOp::Const && def.op != IrOp::Binary &&
                     def.op != IrOp::Unary && def.op != IrOp::Char)) {
                    ++i;
                    continue;
                }
                def.dst = assign.dst;
                block.insts.erase(block.insts.begin() +
                                  static_cast<std::ptrdiff_t>(i + 1));
            }
        }
    }

    void ir_fuse_compare_branches() {
        std::vector<int> uses(ir_values_.size(), 0);
        for (const auto& block : ir_blocks_) {
            for (const auto& inst : block.insts) {
                auto [inst_uses, inst_defs] = ir_inst_uses_defs(inst);
                (void)inst_defs;
                for (const int value : inst_uses) {
                    if (value >= 0 &&
                        value < static_cast<int>(uses.size())) {
                        ++uses[static_cast<std::size_t>(value)];
                    }
                }
            }
        }
        const auto is_compare = [](const std::string& op) {
            return op == "==" || op == "!=" || op == "<" || op == ">" ||
                   op == "<=" || op == ">=";
        };
        for (auto& block : ir_blocks_) {
            for (std::size_t i = 0; i + 1 < block.insts.size();) {
                const auto& compare = block.insts[i];
                const auto& branch = block.insts[i + 1];
                if (compare.op != IrOp::Binary || compare.dst < 0 ||
                    !is_compare(compare.text) || branch.op != IrOp::BranchZero ||
                    branch.lhs != compare.dst ||
                    uses[static_cast<std::size_t>(compare.dst)] != 1) {
                    ++i;
                    continue;
                }
                IrInst fused;
                fused.op = IrOp::BranchCompare;
                fused.lhs = compare.lhs;
                fused.rhs = compare.rhs;
                fused.text = compare.text;
                fused.target = branch.target;
                fused.line = compare.line;
                block.insts.erase(
                    block.insts.begin() + static_cast<std::ptrdiff_t>(i),
                    block.insts.begin() +
                        static_cast<std::ptrdiff_t>(i + 2));
                block.insts.insert(
                    block.insts.begin() + static_cast<std::ptrdiff_t>(i),
                    std::move(fused));
            }
        }
    }

    void ir_fold_immediates() {
        const auto supports_immediate = [](const std::string& op) {
            return op == "+" || op == "-" || op == "&" || op == "|" ||
                   op == "^" || op == "<<" || op == ">>" || op == "==" ||
                   op == "!=" || op == "<" || op == ">" || op == "<=" ||
                   op == ">=";
        };
        const auto find_const = [&](int value) -> const IrInst* {
            for (const auto& block : ir_blocks_) {
                for (const auto& inst : block.insts) {
                    if (inst.op == IrOp::Const && inst.dst == value) {
                        return &inst;
                    }
                }
            }
            return nullptr;
        };
        std::vector<int> uses(ir_values_.size(), 0);
        for (const auto& block : ir_blocks_) {
            for (const auto& inst : block.insts) {
                auto [inst_uses, inst_defs] = ir_inst_uses_defs(inst);
                (void)inst_defs;
                for (const int value : inst_uses) {
                    if (value >= 0 &&
                        value < static_cast<int>(uses.size())) {
                        ++uses[static_cast<std::size_t>(value)];
                    }
                }
            }
        }
        std::unordered_set<int> removable;
        for (auto& block : ir_blocks_) {
            for (auto& inst : block.insts) {
                if (inst.op != IrOp::Binary &&
                    inst.op != IrOp::BranchCompare) {
                    continue;
                }
                if (inst.rhs < 0 || uses[static_cast<std::size_t>(inst.rhs)] !=
                                         1) {
                    continue;
                }
                const IrInst* constant = find_const(inst.rhs);
                if (constant == nullptr) {
                    continue;
                }
                bool strength_reduced = false;
                if (inst.op == IrOp::Binary && inst.text == "*" &&
                    constant->imm > 0 && constant->imm <= 32768 &&
                    (constant->imm & (constant->imm - 1)) == 0) {
                    int shift = 0;
                    std::int64_t value = constant->imm;
                    while (value > 1) {
                        value >>= 1;
                        ++shift;
                    }
                    inst.text = "<<";
                    inst.imm = shift;
                    inst.immediate = true;
                    inst.rhs = -1;
                    removable.insert(constant->dst);
                    strength_reduced = true;
                }
                if (!strength_reduced) {
                    if (!supports_immediate(inst.text) ||
                        ((inst.text == "<<" || inst.text == ">>") &&
                         (constant->imm < 0 || constant->imm > 15))) {
                        continue;
                    }
                    inst.imm = constant->imm;
                    inst.immediate = true;
                    inst.rhs = -1;
                    removable.insert(constant->dst);
                }
            }
        }
        if (removable.empty()) {
            return;
        }
        for (auto& block : ir_blocks_) {
            block.insts.erase(
                std::remove_if(
                    block.insts.begin(), block.insts.end(),
                    [&removable](const IrInst& inst) {
                        return inst.op == IrOp::Const &&
                               removable.count(inst.dst) != 0;
                    }),
                block.insts.end());
        }
    }

    void ir_fold_constant_branches() {
        std::unordered_map<int, std::int64_t> constants;
        for (const auto& block : ir_blocks_) {
            for (const auto& inst : block.insts) {
                if (inst.op == IrOp::Const) {
                    constants[inst.dst] = inst.imm;
                }
            }
        }
        for (auto& block : ir_blocks_) {
            for (std::size_t i = 0; i < block.insts.size();) {
                auto& inst = block.insts[i];
                if (inst.op != IrOp::BranchZero) {
                    ++i;
                    continue;
                }
                if (inst.lhs < 0 ||
                    ir_values_[static_cast<std::size_t>(inst.lhs)].variable) {
                    ++i;
                    continue;
                }
                const auto constant = constants.find(inst.lhs);
                if (constant == constants.end()) {
                    ++i;
                    continue;
                }
                if (constant->second != 0) {
                    block.insts.erase(block.insts.begin() +
                                      static_cast<std::ptrdiff_t>(i));
                } else {
                    inst.op = IrOp::Jump;
                    inst.lhs = -1;
                    ++i;
                }
            }
        }
    }

    void ir_dead_code_elimination() {
        const auto is_pure = [](const IrInst& inst) {
            if (inst.op == IrOp::Const || inst.op == IrOp::Move ||
                inst.op == IrOp::Unary || inst.op == IrOp::Char ||
                inst.op == IrOp::Assign) {
                return true;
            }
            if (inst.op == IrOp::Binary) {
                return inst.text != "/" && inst.text != "%";
            }
            return false;
        };
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 10000) {
            changed = false;
            std::vector<int> uses(ir_values_.size(), 0);
            for (const auto& block : ir_blocks_) {
                for (const auto& inst : block.insts) {
                    auto [inst_uses, inst_defs] = ir_inst_uses_defs(inst);
                    (void)inst_defs;
                    for (const int value : inst_uses) {
                        if (value >= 0 &&
                            value < static_cast<int>(uses.size())) {
                            ++uses[static_cast<std::size_t>(value)];
                        }
                    }
                }
            }
            for (auto& block : ir_blocks_) {
                for (std::size_t i = 0; i < block.insts.size();) {
                    const auto& inst = block.insts[i];
                    auto [inst_uses, inst_defs] = ir_inst_uses_defs(inst);
                    bool dead = inst_defs.size() == 1 && is_pure(inst);
                    if (dead) {
                        for (const int def : inst_defs) {
                            if (def >= 0 &&
                                def < static_cast<int>(uses.size()) &&
                                uses[static_cast<std::size_t>(def)] != 0) {
                                dead = false;
                            }
                        }
                    }
                    if (!dead) {
                        ++i;
                        continue;
                    }
                    for (const int use : inst_uses) {
                        if (use >= 0 &&
                            use < static_cast<int>(uses.size()) &&
                            uses[static_cast<std::size_t>(use)] > 0) {
                            --uses[static_cast<std::size_t>(use)];
                        }
                    }
                    block.insts.erase(block.insts.begin() +
                                      static_cast<std::ptrdiff_t>(i));
                    changed = true;
                }
            }
        }
    }

    void ir_remove_fallthrough_jumps() {        for (std::size_t i = 0; i < ir_blocks_.size(); ++i) {
            auto& block = ir_blocks_[i];
            if (!block.insts.empty() &&
                block.insts.back().op == IrOp::Jump &&
                block.insts.back().target == static_cast<int>(i + 1)) {
                block.insts.pop_back();
            }
        }
    }

    int ir_expr(const Expr* expr) {
        if (expr == nullptr) {
            return ir_const(0, 0);
        }
        switch (expr->kind) {
            case Expr::Kind::Number:
                return ir_const(expr->value, expr->line);
            case Expr::Kind::Sizeof: {
                const int size = expr->children.empty()
                                     ? static_cast<int>(expr->value)
                                     : expression_type(expr->children[0].get())
                                           ->size();
                return ir_const(size, expr->line);
            }
            case Expr::Kind::Identifier:
                return ir_value_for_name(expr->text);
            case Expr::Kind::Cast: {
                const int value = ir_expr(expr->children[0].get());
                if (expr->type && expr->type->kind == TypeKind::Char) {
                    const int dst = ir_temp(true);
                    IrInst inst;
                    inst.op = IrOp::Char;
                    inst.dst = dst;
                    inst.lhs = value;
                    inst.line = expr->line;
                    ir_inst(std::move(inst));
                    return dst;
                }
                return value;
            }
            case Expr::Kind::Unary: {
                const int value = ir_expr(expr->children[0].get());
                if (expr->op == "+") {
                    return value;
                }
                const int dst = ir_temp();
                IrInst inst;
                inst.op = IrOp::Unary;
                inst.dst = dst;
                inst.lhs = value;
                inst.text = expr->op;
                inst.line = expr->line;
                ir_inst(std::move(inst));
                return dst;
            }
            case Expr::Kind::Binary: {
                const int lhs = ir_expr(expr->children[0].get());
                const int rhs = ir_expr(expr->children[1].get());
                const int dst = ir_temp();
                IrInst inst;
                inst.op = IrOp::Binary;
                inst.dst = dst;
                inst.lhs = lhs;
                inst.rhs = rhs;
                inst.text = expr->op;
                inst.line = expr->line;
                ir_inst(std::move(inst));
                return dst;
            }
            default:
                return ir_const(0, expr->line);
        }
    }

    void ir_assign(int var, int value, int line) {
        IrInst inst;
        inst.op = IrOp::Assign;
        inst.dst = var;
        inst.lhs = value;
        inst.line = line;
        ir_inst(std::move(inst));
    }

    void ir_lower_expr_statement(const Expr* expr) {
        if (expr->kind == Expr::Kind::Assign) {
            const int var = ir_value_for_name(expr->children[0]->text);
            const int value = ir_expr(expr->children[1].get());
            ir_assign(var, value, expr->line);
            return;
        }
        if (expr->kind == Expr::Kind::Unary &&
            (expr->op == "++" || expr->op == "--")) {
            const int var = ir_value_for_name(expr->children[0]->text);
            const int one = ir_const(1, expr->line);
            const int value = ir_temp();
            IrInst inst;
            inst.op = IrOp::Binary;
            inst.dst = value;
            inst.lhs = var;
            inst.rhs = one;
            inst.text = expr->op == "++" ? "+" : "-";
            inst.line = expr->line;
            ir_inst(std::move(inst));
            ir_assign(var, value, expr->line);
        }
    }

    void ir_lower_stmt(const Stmt* stmt) {
        if (stmt == nullptr || ir_terminated_ || ir_failed_) {
            return;
        }
        switch (stmt->kind) {
            case Stmt::Kind::Empty:
                return;
            case Stmt::Kind::Compound:
                for (const auto& child : stmt->statements) {
                    if (ir_terminated_) {
                        break;
                    }
                    ir_lower_stmt(child.get());
                }
                return;
            case Stmt::Kind::Decl:
                for (const auto& decl : stmt->declarations) {
                    if (!decl.init) {
                        continue;
                    }
                    const int var = ir_value_for_name(decl.name);
                    ir_assign(var, ir_expr(decl.init.get()), decl.line);
                }
                return;
            case Stmt::Kind::Expr:
                ir_lower_expr_statement(stmt->expr.get());
                return;
            case Stmt::Kind::Return: {
                const int value =
                    stmt->expr ? ir_expr(stmt->expr.get())
                               : ir_const(0, stmt->line);
                IrInst inst;
                inst.op = IrOp::Return;
                inst.lhs = value;
                inst.line = stmt->line;
                ir_inst(std::move(inst));
                ir_terminated_ = true;
                return;
            }
            case Stmt::Kind::If: {
                const int condition = ir_expr(stmt->expr.get());
                const int then_block = ir_add_block();
                const int else_block =
                    stmt->else_body ? ir_add_block() : -1;
                const int end_block = ir_add_block();
                IrInst branch;
                branch.op = IrOp::BranchZero;
                branch.lhs = condition;
                branch.target = else_block >= 0 ? else_block : end_block;
                branch.line = stmt->line;
                ir_inst(std::move(branch));
                ir_switch_block(then_block);
                ir_lower_stmt(stmt->body.get());
                ir_jump(end_block);
                if (else_block >= 0) {
                    ir_switch_block(else_block);
                    ir_lower_stmt(stmt->else_body.get());
                    ir_jump(end_block);
                }
                ir_switch_block(end_block);
                return;
            }
            case Stmt::Kind::While: {
                const int cond_block = ir_add_block();
                const int body_block = ir_add_block();
                const int end_block = ir_add_block();
                ir_jump(cond_block);
                ir_switch_block(cond_block);
                const int condition = ir_expr(stmt->expr.get());
                IrInst branch;
                branch.op = IrOp::BranchZero;
                branch.lhs = condition;
                branch.target = end_block;
                branch.line = stmt->line;
                ir_inst(std::move(branch));
                ir_switch_block(body_block);
                ir_break_.push_back(end_block);
                ir_continue_.push_back(cond_block);
                ir_lower_stmt(stmt->body.get());
                ir_break_.pop_back();
                ir_continue_.pop_back();
                ir_jump(cond_block);
                ir_switch_block(end_block);
                return;
            }
            case Stmt::Kind::DoWhile: {
                const int body_block = ir_add_block();
                const int cond_block = ir_add_block();
                const int end_block = ir_add_block();
                ir_jump(body_block);
                ir_switch_block(body_block);
                ir_break_.push_back(end_block);
                ir_continue_.push_back(cond_block);
                ir_lower_stmt(stmt->body.get());
                ir_break_.pop_back();
                ir_continue_.pop_back();
                ir_jump(cond_block);
                ir_switch_block(cond_block);
                const int condition = ir_expr(stmt->expr.get());
                IrInst branch;
                branch.op = IrOp::BranchZero;
                branch.lhs = condition;
                branch.target = end_block;
                branch.line = stmt->line;
                ir_inst(std::move(branch));
                ir_jump(body_block);
                ir_switch_block(end_block);
                return;
            }
            case Stmt::Kind::For: {
                ir_lower_stmt(stmt->init.get());
                if (ir_terminated_) {
                    return;
                }
                const int cond_block = ir_add_block();
                const int body_block = ir_add_block();
                const int next_block = ir_add_block();
                const int end_block = ir_add_block();
                ir_jump(cond_block);
                ir_switch_block(cond_block);
                if (stmt->expr) {
                    const int condition = ir_expr(stmt->expr.get());
                    IrInst branch;
                    branch.op = IrOp::BranchZero;
                    branch.lhs = condition;
                    branch.target = end_block;
                    branch.line = stmt->line;
                    ir_inst(std::move(branch));
                }
                ir_switch_block(body_block);
                ir_break_.push_back(end_block);
                ir_continue_.push_back(next_block);
                ir_lower_stmt(stmt->body.get());
                ir_break_.pop_back();
                ir_continue_.pop_back();
                ir_jump(next_block);
                ir_switch_block(next_block);
                if (stmt->expr2) {
                    ir_lower_expr_statement(stmt->expr2.get());
                }
                ir_jump(cond_block);
                ir_switch_block(end_block);
                return;
            }
            case Stmt::Kind::Break:
                if (ir_break_.empty()) {
                    ir_failed_ = true;
                } else {
                    ir_jump(ir_break_.back());
                }
                return;
            case Stmt::Kind::Continue:
                if (ir_continue_.empty()) {
                    ir_failed_ = true;
                } else {
                    ir_jump(ir_continue_.back());
                }
                return;
            default:
                ir_failed_ = true;
                return;
        }
    }

    void ir_successors() {
        for (std::size_t i = 0; i < ir_blocks_.size(); ++i) {
            auto& block = ir_blocks_[i];
            block.succ.clear();
            if (block.insts.empty()) {
                if (i + 1 < ir_blocks_.size()) {
                    block.succ.push_back(static_cast<int>(i + 1));
                }
                continue;
            }
            const auto& last = block.insts.back();
            if (last.op == IrOp::Jump) {
                block.succ.push_back(last.target);
            } else if (last.op == IrOp::BranchZero ||
                       last.op == IrOp::BranchCompare) {
                block.succ.push_back(last.target);
                if (i + 1 < ir_blocks_.size()) {
                    block.succ.push_back(static_cast<int>(i + 1));
                }
            } else if (last.op != IrOp::Return &&
                       i + 1 < ir_blocks_.size()) {
                block.succ.push_back(static_cast<int>(i + 1));
            }
        }
    }

    std::pair<std::set<int>, std::set<int>> ir_inst_uses_defs(
        const IrInst& inst) const {
        std::set<int> uses;
        std::set<int> defs;
        switch (inst.op) {
            case IrOp::Const:
            case IrOp::Param:
                defs.insert(inst.dst);
                break;
            case IrOp::Move:
            case IrOp::Unary:
            case IrOp::Char:
            case IrOp::Assign:
                defs.insert(inst.dst);
                uses.insert(inst.lhs);
                break;
            case IrOp::Binary:
                defs.insert(inst.dst);
                uses.insert(inst.lhs);
                if (inst.rhs >= 0) {
                    uses.insert(inst.rhs);
                }
                break;
            case IrOp::BranchZero:
            case IrOp::Return:
                uses.insert(inst.lhs);
                break;
            case IrOp::BranchCompare:
                uses.insert(inst.lhs);
                if (inst.rhs >= 0) {
                    uses.insert(inst.rhs);
                }
                break;
            case IrOp::Jump:
                break;
        }
        return {uses, defs};
    }

    void ir_compute_liveness() {
        const int count = static_cast<int>(ir_values_.size());
        (void)count;
        for (auto& block : ir_blocks_) {
            block.use.clear();
            block.def.clear();
            block.live_in.clear();
            block.live_out.clear();
            for (const auto& inst : block.insts) {
                auto [uses, defs] = ir_inst_uses_defs(inst);
                for (const int value : uses) {
                    if (block.def.count(value) == 0) {
                        block.use.insert(value);
                    }
                }
                for (const int value : defs) {
                    block.def.insert(value);
                }
            }
        }
        bool changed = true;
        int rounds = 0;
        while (changed && rounds++ < 10000) {
            changed = false;
            for (std::size_t index = ir_blocks_.size(); index-- > 0;) {
                auto& block = ir_blocks_[index];
                std::set<int> out;
                for (const int successor : block.succ) {
                    for (const int value : ir_blocks_[successor].live_in) {
                        out.insert(value);
                    }
                }
                std::set<int> in = block.use;
                for (const int value : out) {
                    if (block.def.count(value) == 0) {
                        in.insert(value);
                    }
                }
                if (out != block.live_out || in != block.live_in) {
                    block.live_out = std::move(out);
                    block.live_in = std::move(in);
                    changed = true;
                }
            }
        }
    }

    bool ir_dead_store_elimination() {
        const auto is_pure = [](const IrInst& inst) {
            if (inst.op == IrOp::Const || inst.op == IrOp::Move ||
                inst.op == IrOp::Unary || inst.op == IrOp::Char ||
                inst.op == IrOp::Assign) {
                return true;
            }
            return inst.op == IrOp::Binary && inst.text != "/" &&
                   inst.text != "%";
        };
        bool changed = false;
        for (auto& block : ir_blocks_) {
            std::set<int> live = block.live_out;
            for (std::size_t i = block.insts.size(); i-- > 0;) {
                const auto& inst = block.insts[i];
                auto [uses, defs] = ir_inst_uses_defs(inst);
                bool dead = is_pure(inst);
                if (dead) {
                    for (const int def : defs) {
                        if (live.count(def) != 0) {
                            dead = false;
                        }
                    }
                }
                if (dead) {
                    block.insts.erase(block.insts.begin() +
                                      static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    continue;
                }
                for (const int def : defs) {
                    live.erase(def);
                }
                for (const int use : uses) {
                    live.insert(use);
                }
            }
        }
        return changed;
    }

    bool ir_color() {
        const int count = static_cast<int>(ir_values_.size());
        for (auto& value : ir_values_) {
            value.reg = -1;
        }
        std::vector<std::unordered_set<int>> graph(
            static_cast<std::size_t>(count));
        auto add_edge = [&graph](int a, int b) {
            if (a >= 0 && b >= 0 && a != b) {
                graph[static_cast<std::size_t>(a)].insert(b);
                graph[static_cast<std::size_t>(b)].insert(a);
            }
        };
        for (const auto& block : ir_blocks_) {
            std::set<int> live = block.live_out;
            for (std::size_t i = block.insts.size(); i-- > 0;) {
                const auto& inst = block.insts[i];
                auto [uses, defs] = ir_inst_uses_defs(inst);
                for (const int def : defs) {
                    for (const int value : live) {
                        add_edge(def, value);
                    }
                    // Assignment and masks can safely use the source register
                    // when the source is dead after the instruction.
                    const bool can_overlap =
                        inst.op == IrOp::Assign ||
                        inst.op == IrOp::Move ||
                        inst.op == IrOp::Unary ||
                        inst.op == IrOp::Char;
                    if (!can_overlap) {
                        for (const int use : uses) {
                            add_edge(def, use);
                        }
                    }
                }
                for (const int def : defs) {
                    live.erase(def);
                }
                for (const int use : uses) {
                    live.insert(use);
                }
            }
        }
        std::vector<int> order(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            order[static_cast<std::size_t>(i)] = i;
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return graph[static_cast<std::size_t>(a)].size() >
                   graph[static_cast<std::size_t>(b)].size();
        });
        for (const int value : order) {
            bool used[5] = {false, false, false, false, false};
            for (const int neighbor : graph[static_cast<std::size_t>(value)]) {
                if (neighbor >= 0 &&
                    neighbor < static_cast<int>(ir_values_.size()) &&
                    ir_values_[static_cast<std::size_t>(neighbor)].reg >= 0) {
                    used[ir_values_[static_cast<std::size_t>(neighbor)].reg] =
                        true;
                }
            }
            int color = -1;
            for (int candidate = 0; candidate < 5; ++candidate) {
                if (!used[candidate]) {
                    color = candidate;
                    break;
                }
            }
            if (color < 0) {
                return false;
            }
            ir_values_[static_cast<std::size_t>(value)].reg = color;
        }
        return true;
    }

    void ir_emit_block_label(std::ostringstream& out, int block) const {
        out << ".Lir_" << function_label_ << "_" << block << ":\n";
    }

    void ir_emit(const Function& function, std::ostringstream& out) {
        out << ".global " << function.name << "\n";
        out << function.name << ":\n";
        out << "  PUSH R4\n";
        for (std::size_t block = 0; block < ir_blocks_.size(); ++block) {
            ir_emit_block_label(out, static_cast<int>(block));
            for (const auto& inst : ir_blocks_[block].insts) {
                const auto reg_of = [&](int value) {
                    return value >= 0
                               ? ir_values_[static_cast<std::size_t>(value)].reg
                               : -1;
                };
                switch (inst.op) {
                    case IrOp::Const:
                        out << "  MOV R" << reg_of(inst.dst) << ", #"
                            << inst.imm << "\n";
                        break;
                    case IrOp::Param: {
                        const int dst = reg_of(inst.dst);
                        const int source = static_cast<int>(inst.imm) + 1;
                        if (dst != source) {
                            out << "  MOV R" << dst << ", R" << source
                                << "\n";
                        }
                        if (ir_values_[static_cast<std::size_t>(inst.dst)]
                                .character) {
                            out << "  AND R" << dst << ", #0xFF\n";
                        }
                        break;
                    }
                    case IrOp::Move:
                        if (reg_of(inst.dst) != reg_of(inst.lhs)) {
                            out << "  MOV R" << reg_of(inst.dst) << ", R"
                                << reg_of(inst.lhs) << "\n";
                        }
                        break;
                    case IrOp::Char:
                        if (reg_of(inst.dst) != reg_of(inst.lhs)) {
                            out << "  MOV R" << reg_of(inst.dst) << ", R"
                                << reg_of(inst.lhs) << "\n";
                        }
                        out << "  AND R" << reg_of(inst.dst) << ", #0xFF\n";
                        break;
                    case IrOp::Unary:
                        if (reg_of(inst.dst) != reg_of(inst.lhs)) {
                            out << "  MOV R" << reg_of(inst.dst) << ", R"
                                << reg_of(inst.lhs) << "\n";
                        }
                        out << (inst.text == "-" ? "  NEG R" : "  NOT R")
                            << reg_of(inst.dst) << "\n";
                        break;
                    case IrOp::Assign:
                        if (reg_of(inst.dst) != reg_of(inst.lhs)) {
                            out << "  MOV R" << reg_of(inst.dst) << ", R"
                                << reg_of(inst.lhs) << "\n";
                        }
                        if (ir_values_[static_cast<std::size_t>(inst.dst)]
                                .character) {
                            out << "  AND R" << reg_of(inst.dst)
                                << ", #0xFF\n";
                        }
                        break;
                    case IrOp::Binary: {
                        const int dst = reg_of(inst.dst);
                        const int lhs = reg_of(inst.lhs);
                        const std::string rhs =
                            inst.immediate
                                ? "#" + std::to_string(inst.imm)
                                : "R" + std::to_string(reg_of(inst.rhs));
                        if (dst != lhs) {
                            out << "  MOV R" << dst << ", R" << lhs << "\n";
                        }
                        if (inst.text == "+") {
                            out << "  ADD R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "-") {
                            out << "  SUB R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "*") {
                            out << "  MUL R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "/") {
                            out << "  DIV R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "%") {
                            out << "  MOD R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "&") {
                            out << "  AND R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "|") {
                            out << "  OR R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "^") {
                            out << "  XOR R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == "<<") {
                            out << "  SHL R" << dst << ", " << rhs << "\n";
                        } else if (inst.text == ">>") {
                            out << "  SAR R" << dst << ", " << rhs << "\n";
                        } else {
                            out << "  CMP R" << dst << ", " << rhs << "\n";
                            const std::string yes =
                                new_label("ircmp");
                            const std::string end =
                                new_label("ircmpend");
                            std::string branch = "BEQ";
                            if (inst.text == "!=") {
                                branch = "BNE";
                            } else if (inst.text == "<") {
                                branch = "BLT";
                            } else if (inst.text == ">") {
                                branch = "BGT";
                            } else if (inst.text == "<=") {
                                branch = "BLE";
                            } else if (inst.text == ">=") {
                                branch = "BGE";
                            }
                            out << "  " << branch << " " << yes << "\n";
                            out << "  MOV R" << dst << ", #0\n";
                            out << "  BRA " << end << "\n";
                            out << yes << ":\n";
                            out << "  MOV R" << dst << ", #1\n";
                            out << end << ":\n";
                        }
                        break;
                    }
                    case IrOp::BranchZero:
                        out << "  CMP R" << reg_of(inst.lhs) << ", #0\n";
                        out << "  BEQ .Lir_" << function_label_ << "_"
                            << inst.target << "\n";
                        break;
                    case IrOp::BranchCompare: {
                        const std::string rhs =
                            inst.immediate
                                ? "#" + std::to_string(inst.imm)
                                : "R" + std::to_string(reg_of(inst.rhs));
                        out << "  CMP R" << reg_of(inst.lhs) << ", " << rhs
                            << "\n";
                        std::string branch = "BEQ";
                        if (inst.text == "==") {
                            branch = "BNE";
                        } else if (inst.text == "!=") {
                            branch = "BEQ";
                        } else if (inst.text == "<") {
                            branch = "BGE";
                        } else if (inst.text == ">") {
                            branch = "BLE";
                        } else if (inst.text == "<=") {
                            branch = "BGT";
                        } else if (inst.text == ">=") {
                            branch = "BLT";
                        }
                        out << "  " << branch << " .Lir_" << function_label_
                            << "_" << inst.target << "\n";
                        break;
                    }
                    case IrOp::Jump:
                        out << "  BRA .Lir_" << function_label_ << "_"
                            << inst.target << "\n";
                        break;
                    case IrOp::Return:
                        if (reg_of(inst.lhs) != 0) {
                            out << "  MOV R0, R" << reg_of(inst.lhs)
                                << "\n";
                        }
                        out << "  BRA .Lir_ret_" << function_label_ << "\n";
                        break;
                }
            }
        }
        out << ".Lir_ret_" << function_label_ << ":\n";
        out << "  POP R4\n";
        out << "  RET\n";
    }

    bool try_compile_ir(const Function& function, std::ostringstream& out) {
        if (!optimize_ || !can_ir_function(function)) {
            return false;
        }
        ir_values_.clear();
        ir_vars_.clear();
        ir_blocks_.clear();
        ir_break_.clear();
        ir_continue_.clear();
        ir_failed_ = false;
        ir_terminated_ = false;
        ir_current_ = 0;
        ir_add_block();
        for (const auto& [name, variable] : locals_) {
            if (!variable.type ||
                (variable.type->kind != TypeKind::Char &&
                 variable.type->kind != TypeKind::Int)) {
                return false;
            }
            const int value =
                ir_new_value(true, name, variable.type->is_char());
            ir_vars_[name] = value;
        }
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const int value = ir_value_for_name(function.parameters[i].name);
            IrInst inst;
            inst.op = IrOp::Param;
            inst.dst = value;
            inst.imm = static_cast<std::int64_t>(i);
            ir_inst(std::move(inst));
        }
        ir_lower_stmt(function.body.get());
        if (!ir_terminated_) {
            IrInst inst;
            inst.op = IrOp::Return;
            inst.lhs = ir_const(0, 0);
            ir_inst(std::move(inst));
        }
        if (ir_failed_) {
            return false;
        }
        ir_coalesce_assignments();
        ir_fuse_compare_branches();
        ir_fold_immediates();
        ir_fold_constant_branches();
        ir_dead_code_elimination();
        ir_remove_fallthrough_jumps();
        ir_successors();
        ir_compute_liveness();
        if (ir_dead_store_elimination()) {
            ir_dead_code_elimination();
            ir_successors();
            ir_compute_liveness();
        }
        if (!ir_color()) {
            return false;
        }
        ir_emit(function, out);
        return true;
    }

    void compile_function(Function& function, std::ostringstream& out) {
        locals_.clear();
        frame_size_ = 0;
        current_named_params_ = static_cast<int>(function.parameters.size());
        return_label_ = new_label("return");

        int frame = 0;
        for (auto& parameter : function.parameters) {
            frame += 2;
            Variable variable;
            variable.type = parameter.type;
            variable.offset = frame;
            variable.name = parameter.name;
            locals_[parameter.name] = variable;
        }
        std::vector<const Decl*> declarations;
        collect_locals(function.body.get(), declarations);
        for (const auto* declaration : declarations) {
            if (locals_.count(declaration->name) != 0) {
                continue;
            }
            if (declaration->is_static) {
                const std::string symbol =
                    ".Lstatic_" + function.name + "_" + declaration->name;
                Variable variable;
                variable.type = declaration->type;
                variable.global = true;
                variable.name = declaration->name;
                variable.symbol = symbol;
                locals_[declaration->name] = variable;
                if (declaration->init &&
                    declaration->init->kind == Expr::Kind::InitList &&
                    (declaration->type->kind == TypeKind::Array ||
                     declaration->type->kind == TypeKind::Struct ||
                     declaration->type->kind == TypeKind::Union)) {
                    out << ".data\n.align 2\n" << symbol << ":\n";
                    int cursor = emit_initializer(
                        declaration->type.get(), declaration->init.get(), 0,
                        0, out);
                    if (cursor < declaration->type->size()) {
                        out << "  .space "
                            << (declaration->type->size() - cursor) << "\n";
                    }
                } else if (declaration->init &&
                           declaration->init->kind == Expr::Kind::Number) {
                    out << ".data\n.align 2\n" << symbol << ":\n";
                    out << (declaration->type->is_char() ? "  .byte "
                                                        : "  .word ")
                        << declaration->init->value << "\n";
                } else {
                    out << ".bss\n.align 2\n" << symbol << ":\n  .space "
                        << declaration->type->size() << "\n";
                }
                out << ".text\n";
                continue;
            }
            frame += align(declaration->type->size(), 2);
            Variable variable;
            variable.type = declaration->type;
            variable.offset = frame;
            variable.name = declaration->name;
            locals_[declaration->name] = variable;
        }
        frame_size_ = align(frame, 2);
        function_label_ = function.name;
        current_return_type_ = function.return_type;
        current_return_symbol_.clear();
        if (current_return_type_ &&
            (current_return_type_->kind == TypeKind::Struct ||
             current_return_type_->kind == TypeKind::Union)) {
            current_return_symbol_ = ".Lret_" + function.name;
            out << ".bss\n.align 2\n" << current_return_symbol_
                << ":\n  .space " << current_return_type_->size() << "\n";
            out << ".text\n";
        }
        if (debug_) {
            debug_frames_.push_back(DebugFrame{function.name, frame_size_});
            for (const auto& parameter : function.parameters) {
                record_type(parameter.type);
            }
            for (const auto& [name, variable] : locals_) {
                record_type(variable.type);
                if (!variable.global && !variable.indirect) {
                    debug_vars_.push_back(DebugVar{
                        function.name, name, variable.offset,
                        variable.type ? variable.type->size() : 2,
                        type_text(variable.type)});
                }
            }
        }

        if (try_compile_ir(function, out)) {
            return;
        }

        out << ".global " << function.name << "\n";
        out << function.name << ":\n";
        out << "  PUSH R4\n";
        out << "  MOV R4, SP\n";
        if (frame_size_ > 0) {
            out << "  SUB SP, #" << frame_size_ << "\n";
        }
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const auto& parameter = function.parameters[i];
            const auto it = locals_.find(parameter.name);
            if (it == locals_.end()) {
                continue;
            }
            if (parameter.type->kind == TypeKind::Struct ||
                parameter.type->kind == TypeKind::Union) {
                if (i < 3) {
                    out << "  ST [R4-" << it->second.offset << "], R"
                        << (i + 1) << "\n";
                } else {
                    out << "  LD R0, [R4+" << (4 + (i - 3) * 2) << "]\n";
                    out << "  ST [R4-" << it->second.offset << "], R0\n";
                }
                locals_[parameter.name].indirect = true;
                continue;
            }
            if (function.variadic) {
                out << "  LD R0, [R4+" << (4 + i * 2) << "]\n";
                out << (parameter.type->is_char() ? "  STB " : "  ST ")
                    << "[R4-" << it->second.offset << "], R0\n";
                continue;
            }
            if (i < 3) {
                out << (parameter.type->is_char() ? "  STB " : "  ST ")
                    << "[R4-" << it->second.offset << "], R" << (i + 1)
                    << "\n";
            } else {
                out << "  LD R0, [R4+" << (4 + (i - 3) * 2) << "]\n";
                out << (parameter.type->is_char() ? "  STB " : "  ST ")
                    << "[R4-" << it->second.offset << "], R0\n";
            }
        }
        gen_stmt(function.body.get(), out);
        out << "  MOV R0, #0\n";
        out << return_label_ << ":\n";
        out << "  MOV SP, R4\n";
        out << "  POP R4\n";
        out << "  RET\n";
    }

    static int align(int value, int alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    std::shared_ptr<Type> expression_type(const Expr* expr) {
        if (expr == nullptr) {
            return make_type(TypeKind::Int);
        }
        if (expr->kind == Expr::Kind::Identifier) {
            const auto local = locals_.find(expr->text);
            if (local != locals_.end()) {
                return local->second.type;
            }
            const auto global = global_map_.find(expr->text);
            if (global != global_map_.end()) {
                return global->second->type;
            }
            return make_type(TypeKind::Int);
        }
        if (expr->kind == Expr::Kind::Member) {
            auto base = expression_type(expr->children[0].get());
            if (expr->op == "->" && base->kind == TypeKind::Pointer) {
                base = base->base;
            }
            if (base && (base->kind == TypeKind::Struct ||
                    base->kind == TypeKind::Union)) {
                for (const auto& member : base->members) {
                    if (member.name == expr->text) {
                        return member.type;
                    }
                }
            }
            return make_type(TypeKind::Int);
        }
        if (expr->kind == Expr::Kind::Index) {
            const auto base = expression_type(expr->children[0].get());
            if (base->kind == TypeKind::Array) {
                return base->base;
            }
            if (base->kind == TypeKind::Pointer) {
                return base->base;
            }
        }
        if (expr->kind == Expr::Kind::Unary && expr->op == "*") {
            const auto base = expression_type(expr->children[0].get());
            if (base->kind == TypeKind::Pointer || base->kind == TypeKind::Array) {
                return base->base;
            }
        }
        if (expr->kind == Expr::Kind::Call) {
            const auto function = function_map_.find(expr->text);
            if (function != function_map_.end()) {
                return function->second->return_type;
            }
        }
        if (expr->kind == Expr::Kind::String) {
            return make_type(TypeKind::Pointer, make_type(TypeKind::Char));
        }
        if (expr->kind == Expr::Kind::Cast && expr->type) {
            return expr->type;
        }
        return make_type(TypeKind::Int);
    }

    void gen_stmt(const Stmt* stmt, std::ostringstream& out) {
        if (stmt == nullptr) {
            return;
        }
        if (debug_ && stmt->line > 0) {
            out << ".loc 0 " << stmt->line << " 0\n";
        }
        switch (stmt->kind) {
            case Stmt::Kind::Empty:
                return;
            case Stmt::Kind::Expr:
                gen_expr(stmt->expr.get(), out);
                return;
            case Stmt::Kind::Return:
                if (current_return_symbol_.empty()) {
                    if (stmt->expr) {
                        gen_expr(stmt->expr.get(), out);
                    } else {
                        out << "  MOV R0, #0\n";
                    }
                } else {
                    if (stmt->expr) {
                        gen_addr(stmt->expr.get(), out);
                        out << "  MOV R1, R0\n";
                        out << "  MOV R2, #" << current_return_symbol_
                            << "\n";
                        const int size = current_return_type_->size();
                        for (int offset = 0; offset < size;) {
                            if (size - offset == 1) {
                                out << "  LDB R3, [R1+" << offset << "]\n";
                                out << "  STB [R2+" << offset << "], R3\n";
                                ++offset;
                            } else {
                                out << "  LD R3, [R1+" << offset << "]\n";
                                out << "  ST [R2+" << offset << "], R3\n";
                                offset += 2;
                            }
                        }
                    }
                    out << "  MOV R0, #" << current_return_symbol_ << "\n";
                }
                out << "  BRA " << return_label_ << "\n";
                return;
            case Stmt::Kind::Compound:
                {
                    bool reachable = true;
                    for (const auto& child : stmt->statements) {
                        if (optimize_ && !reachable &&
                            child->kind != Stmt::Kind::Label) {
                            continue;
                        }
                        gen_stmt(child.get(), out);
                        if (optimize_ &&
                            (child->kind == Stmt::Kind::Return ||
                             child->kind == Stmt::Kind::Break ||
                             child->kind == Stmt::Kind::Continue ||
                             child->kind == Stmt::Kind::Goto)) {
                            reachable = false;
                        } else if (optimize_ &&
                                   child->kind == Stmt::Kind::Label) {
                            reachable = true;
                        }
                    }
                }
                return;
            case Stmt::Kind::Decl:
                for (const auto& decl : stmt->declarations) {
                    if (decl.is_static) {
                        continue;
                    }
                    if (decl.init) {
                        const auto it = locals_.find(decl.name);
                        if (it == locals_.end()) {
                            continue;
                        }
                        if (decl.init &&
                            (decl.type->kind == TypeKind::Struct ||
                             decl.type->kind == TypeKind::Union) &&
                            expression_type(decl.init.get())->kind ==
                                decl.type->kind) {
                            gen_addr(decl.init.get(), out);
                            out << "  MOV R1, R0\n";
                            out << "  LEA R2, [R4-" << it->second.offset
                                << "]\n";
                            const int size = decl.type->size();
                            for (int offset = 0; offset < size;) {
                                if (size - offset == 1) {
                                    out << "  LDB R3, [R1+" << offset
                                        << "]\n";
                                    out << "  STB [R2+" << offset
                                        << "], R3\n";
                                    ++offset;
                                } else {
                                    out << "  LD R3, [R1+" << offset << "]\n";
                                    out << "  ST [R2+" << offset
                                        << "], R3\n";
                                    offset += 2;
                                }
                            }
                            continue;
                        }
                        if (decl.init->kind == Expr::Kind::InitList &&
                            (decl.type->kind == TypeKind::Struct ||
                             decl.type->kind == TypeKind::Union ||
                             decl.type->kind == TypeKind::Array)) {
                            std::function<void(std::shared_ptr<Type>,
                                               const Expr*, int)>
                                store_aggregate;
                            store_aggregate =
                                [&](std::shared_ptr<Type> type,
                                    const Expr* list, int base_offset) {
                                    for (std::size_t i = 0;
                                         i < list->children.size(); ++i) {
                                        int offset = 0;
                                        std::shared_ptr<Type> element_type =
                                            type;
                                        if (type->kind == TypeKind::Struct ||
                                            type->kind == TypeKind::Union) {
                                            if (i >= type->members.size()) {
                                                break;
                                            }
                                            offset =
                                                type->members[i].offset;
                                            element_type =
                                                type->members[i].type;
                                        } else {
                                            offset = static_cast<int>(i) *
                                                     (type->base
                                                          ? type->base->size()
                                                          : 2);
                                            element_type = type->base;
                                        }
                                        const auto* element =
                                            list->children[i].get();
                                        if (element->kind ==
                                                Expr::Kind::InitList &&
                                            element_type &&
                                            (element_type->kind ==
                                                 TypeKind::Array ||
                                             element_type->kind ==
                                                 TypeKind::Struct ||
                                             element_type->kind ==
                                                 TypeKind::Union)) {
                                            store_aggregate(
                                                element_type, element,
                                                base_offset + offset);
                                            continue;
                                        }
                                        gen_expr(element, out);
                                        const int address_offset =
                                            it->second.offset - base_offset -
                                            offset;
                                        out << "  LEA R1, [R4-"
                                            << address_offset << "]\n";
                                        out << (element_type &&
                                                        element_type->is_char()
                                                    ? "  STB [R1], R0\n"
                                                    : "  ST [R1], R0\n");
                                    }
                                };
                            store_aggregate(decl.type, decl.init.get(), 0);
                            continue;
                        }
                        gen_expr(decl.init.get(), out);
                        if (optimize_) {
                            out << (decl.type->is_char() ? "  STB [R4-"
                                                         : "  ST [R4-")
                                << it->second.offset << "], R0\n";
                        } else {
                            out << "  LEA R1, [R4-" << it->second.offset
                                << "]\n";
                            out << (decl.type->is_char() ? "  STB [R1], R0\n"
                                                        : "  ST [R1], R0\n");
                        }
                    }
                }
                return;
            case Stmt::Kind::If: {
                const auto condition =
                    optimize_ ? constant_expr_value(stmt->expr.get())
                              : std::nullopt;
                if (condition) {
                    if (*condition != 0) {
                        gen_stmt(stmt->body.get(), out);
                    } else if (stmt->else_body) {
                        gen_stmt(stmt->else_body.get(), out);
                    }
                    return;
                }
                const auto else_label = new_label("else");
                const auto end_label = new_label("endif");
                const auto false_target =
                    stmt->else_body ? else_label : end_label;
                if (!optimize_ ||
                    !emit_simple_condition(stmt->expr.get(), false_target,
                                           false, out)) {
                    gen_expr(stmt->expr.get(), out);
                    out << "  CMP R0, #0\n";
                    out << "  BEQ " << false_target << "\n";
                }
                gen_stmt(stmt->body.get(), out);
                if (stmt->else_body) {
                    out << "  BRA " << end_label << "\n";
                    out << else_label << ":\n";
                    gen_stmt(stmt->else_body.get(), out);
                }
                out << end_label << ":\n";
                return;
            }
            case Stmt::Kind::Switch: {
                const auto end = new_label("endswitch");
                std::vector<std::string> labels;
                labels.reserve(stmt->cases.size());
                for (std::size_t i = 0; i < stmt->cases.size(); ++i) {
                    labels.push_back(new_label("case"));
                }
                gen_expr(stmt->expr.get(), out);
                std::optional<std::size_t> default_index;
                for (std::size_t i = 0; i < stmt->cases.size(); ++i) {
                    if (stmt->case_defaults[i]) {
                        default_index = i;
                        continue;
                    }
                    out << "  CMP R0, #" << stmt->case_values[i] << "\n";
                    out << "  BEQ " << labels[i] << "\n";
                }
                out << "  BRA "
                    << (default_index ? labels[*default_index] : end) << "\n";
                break_labels_.push_back(end);
                for (std::size_t i = 0; i < stmt->cases.size(); ++i) {
                    out << labels[i] << ":\n";
                    gen_stmt(stmt->cases[i].get(), out);
                }
                out << end << ":\n";
                break_labels_.pop_back();
                return;
            }
            case Stmt::Kind::While: {
                const auto condition =
                    optimize_ ? constant_expr_value(stmt->expr.get())
                              : std::nullopt;
                if (condition && *condition == 0) {
                    return;
                }
                const auto start = new_label("while");
                const auto end = new_label("endwhile");
                break_labels_.push_back(end);
                continue_labels_.push_back(start);
                out << start << ":\n";
                if (!condition) {
                    if (!optimize_ ||
                        !emit_simple_condition(stmt->expr.get(), end, false,
                                               out)) {
                        gen_expr(stmt->expr.get(), out);
                        out << "  CMP R0, #0\n";
                        out << "  BEQ " << end << "\n";
                    }
                }
                gen_stmt(stmt->body.get(), out);
                out << "  BRA " << start << "\n";
                out << end << ":\n";
                break_labels_.pop_back();
                continue_labels_.pop_back();
                return;
            }
            case Stmt::Kind::DoWhile: {
                const auto start = new_label("do");
                const auto cond = new_label("docond");
                const auto end = new_label("enddo");
                break_labels_.push_back(end);
                continue_labels_.push_back(cond);
                out << start << ":\n";
                gen_stmt(stmt->body.get(), out);
                out << cond << ":\n";
                if (!optimize_ ||
                    !emit_simple_condition(stmt->expr.get(), start, true,
                                           out)) {
                    gen_expr(stmt->expr.get(), out);
                    out << "  CMP R0, #0\n";
                    out << "  BNE " << start << "\n";
                }
                out << end << ":\n";
                break_labels_.pop_back();
                continue_labels_.pop_back();
                return;
            }
            case Stmt::Kind::For: {
                const auto condition =
                    optimize_ ? constant_expr_value(stmt->expr.get())
                              : std::nullopt;
                if (condition && *condition == 0) {
                    if (stmt->init) {
                        gen_stmt(stmt->init.get(), out);
                    }
                    return;
                }
                const auto start = new_label("for");
                const auto next = new_label("fornext");
                const auto end = new_label("endfor");
                if (stmt->init) {
                    gen_stmt(stmt->init.get(), out);
                }
                break_labels_.push_back(end);
                continue_labels_.push_back(next);
                out << start << ":\n";
                if (stmt->expr && !condition) {
                    if (!optimize_ ||
                        !emit_simple_condition(stmt->expr.get(), end, false,
                                               out)) {
                        gen_expr(stmt->expr.get(), out);
                        out << "  CMP R0, #0\n";
                        out << "  BEQ " << end << "\n";
                    }
                }
                gen_stmt(stmt->body.get(), out);
                out << next << ":\n";
                if (stmt->expr2) {
                    gen_expr(stmt->expr2.get(), out);
                }
                out << "  BRA " << start << "\n";
                out << end << ":\n";
                break_labels_.pop_back();
                continue_labels_.pop_back();
                return;
            }
            case Stmt::Kind::Break:
                if (!break_labels_.empty()) {
                    out << "  BRA " << break_labels_.back() << "\n";
                }
                return;
            case Stmt::Kind::Continue:
                if (!continue_labels_.empty()) {
                    out << "  BRA " << continue_labels_.back() << "\n";
                }
                return;
            case Stmt::Kind::Goto:
                out << "  BRA .Luser_" << function_label_ << "_" << stmt->label
                    << "\n";
                return;
            case Stmt::Kind::Label:
                out << ".Luser_" << function_label_ << "_" << stmt->label
                    << ":\n";
                gen_stmt(stmt->body.get(), out);
                return;
        }
    }

    bool is_simple_leaf(const Expr* expr) const {
        if (expr == nullptr) {
            return false;
        }
        if (expr->kind == Expr::Kind::Number ||
            expr->kind == Expr::Kind::String ||
            expr->kind == Expr::Kind::Identifier ||
            expr->kind == Expr::Kind::Sizeof) {
            return true;
        }
        if (expr->kind == Expr::Kind::Cast) {
            return expr->children.size() == 1 &&
                   is_simple_leaf(expr->children[0].get());
        }
        if (expr->kind == Expr::Kind::Unary &&
            (expr->op == "+" || expr->op == "-" || expr->op == "~")) {
            return expr->children.size() == 1 &&
                   is_simple_leaf(expr->children[0].get());
        }
        return false;
    }

    bool is_simple_binary(const Expr* expr) const {
        if (expr == nullptr || expr->kind != Expr::Kind::Binary ||
            expr->children.size() != 2) {
            return false;
        }
        if (expr->op == "&&" || expr->op == "||") {
            return false;
        }
        return is_simple_leaf(expr->children[0].get()) &&
               is_simple_leaf(expr->children[1].get());
    }

    bool emit_simple_leaf(const Expr* expr, int target,
                          std::ostringstream& out) {
        if (expr == nullptr) {
            return false;
        }
        if (expr->kind == Expr::Kind::Number) {
            out << "  MOV R" << target << ", #" << expr->value << "\n";
            return true;
        }
        if (expr->kind == Expr::Kind::String) {
            out << "  MOV R" << target << ", #" << string_label(expr->text)
                << "\n";
            return true;
        }
        if (expr->kind == Expr::Kind::Sizeof) {
            const int size = expr->children.empty()
                                 ? static_cast<int>(expr->value)
                                 : expression_type(expr->children[0].get())
                                       ->size();
            out << "  MOV R" << target << ", #" << size << "\n";
            return true;
        }
        if (expr->kind == Expr::Kind::Identifier) {
            gen_load_identifier(expr->text, out, target);
            return true;
        }
        if (expr->kind == Expr::Kind::Cast && expr->children.size() == 1) {
            if (!emit_simple_leaf(expr->children[0].get(), target, out)) {
                return false;
            }
            if (expr->type && expr->type->kind == TypeKind::Char) {
                out << "  AND R" << target << ", #0xFF\n";
            }
            return true;
        }
        if (expr->kind == Expr::Kind::Unary && expr->children.size() == 1) {
            if (!emit_simple_leaf(expr->children[0].get(), target, out)) {
                return false;
            }
            if (expr->op == "-") {
                out << "  NEG R" << target << "\n";
            } else if (expr->op == "~") {
                out << "  NOT R" << target << "\n";
            } else if (expr->op != "+") {
                return false;
            }
            return true;
        }
        return false;
    }

    bool is_pure_expr(const Expr* expr) const {
        if (expr == nullptr) {
            return false;
        }
        switch (expr->kind) {
            case Expr::Kind::Number:
            case Expr::Kind::String:
            case Expr::Kind::Sizeof:
                return true;
            case Expr::Kind::Identifier:
                return true;
            case Expr::Kind::Cast:
                return expr->children.size() == 1 &&
                       is_pure_expr(expr->children[0].get());
            case Expr::Kind::Unary:
                return expr->children.size() == 1 &&
                       (expr->op == "+" || expr->op == "-" ||
                        expr->op == "~") &&
                       is_pure_expr(expr->children[0].get());
            case Expr::Kind::Binary:
                return expr->children.size() == 2 &&
                       expr->op != "&&" && expr->op != "||" &&
                       is_pure_expr(expr->children[0].get()) &&
                       is_pure_expr(expr->children[1].get());
            default:
                return false;
        }
    }

    int pure_register_need(const Expr* expr) const {
        if (expr == nullptr || is_simple_leaf(expr)) {
            return 1;
        }
        if (expr->kind == Expr::Kind::Cast ||
            expr->kind == Expr::Kind::Unary) {
            return pure_register_need(expr->children[0].get());
        }
        if (expr->kind == Expr::Kind::Binary &&
            expr->children.size() == 2) {
            const int left = pure_register_need(expr->children[0].get());
            const int right = pure_register_need(expr->children[1].get());
            return std::max(left, right + 1);
        }
        return 1;
    }

    static int pure_popcount(unsigned mask) {
        int count = 0;
        while (mask != 0) {
            count += static_cast<int>(mask & 1u);
            mask >>= 1;
        }
        return count;
    }

    static int pure_other_register(unsigned mask, int target) {
        for (int reg = 0; reg < 4; ++reg) {
            if (reg != target && (mask & (1u << reg)) != 0) {
                return reg;
            }
        }
        return -1;
    }

    bool emit_pure_operation(const std::string& op, int target,
                             const std::string& rhs,
                             std::ostringstream& out) {
        if (op == "+") {
            out << "  ADD R" << target << ", " << rhs << "\n";
        } else if (op == "-") {
            out << "  SUB R" << target << ", " << rhs << "\n";
        } else if (op == "*") {
            out << "  MUL R" << target << ", " << rhs << "\n";
        } else if (op == "/") {
            out << "  DIV R" << target << ", " << rhs << "\n";
        } else if (op == "%") {
            out << "  MOD R" << target << ", " << rhs << "\n";
        } else if (op == "&") {
            out << "  AND R" << target << ", " << rhs << "\n";
        } else if (op == "|") {
            out << "  OR R" << target << ", " << rhs << "\n";
        } else if (op == "^") {
            out << "  XOR R" << target << ", " << rhs << "\n";
        } else if (op == "<<") {
            out << "  SHL R" << target << ", " << rhs << "\n";
        } else if (op == ">>") {
            out << "  SAR R" << target << ", " << rhs << "\n";
        } else if (op == "==" || op == "!=" || op == "<" || op == ">" ||
                   op == "<=" || op == ">=") {
            out << "  CMP R" << target << ", " << rhs << "\n";
            const auto yes = new_label("purecmp");
            const auto end = new_label("purecmpend");
            std::string branch = "BEQ";
            if (op == "!=") branch = "BNE";
            if (op == "<") branch = "BLT";
            if (op == ">") branch = "BGT";
            if (op == "<=") branch = "BLE";
            if (op == ">=") branch = "BGE";
            out << "  " << branch << " " << yes << "\n";
            out << "  MOV R" << target << ", #0\n";
            out << "  BRA " << end << "\n";
            out << yes << ":\n";
            out << "  MOV R" << target << ", #1\n";
            out << end << ":\n";
        } else {
            return false;
        }
        return true;
    }

    bool emit_pure_expr(const Expr* expr, int target, unsigned mask,
                        std::ostringstream& out) {
        if (expr == nullptr || target < 0 || target > 3 ||
            (mask & (1u << target)) == 0) {
            return false;
        }
        if (is_simple_leaf(expr)) {
            return emit_simple_leaf(expr, target, out);
        }
        if (expr->kind == Expr::Kind::Cast) {
            if (!emit_pure_expr(expr->children[0].get(), target, mask, out)) {
                return false;
            }
            if (expr->type && expr->type->kind == TypeKind::Char) {
                out << "  AND R" << target << ", #0xFF\n";
            }
            return true;
        }
        if (expr->kind == Expr::Kind::Unary) {
            if (!emit_pure_expr(expr->children[0].get(), target, mask, out)) {
                return false;
            }
            if (expr->op == "-") {
                out << "  NEG R" << target << "\n";
            } else if (expr->op == "~") {
                out << "  NOT R" << target << "\n";
            }
            return true;
        }
        if (expr->kind != Expr::Kind::Binary ||
            !is_pure_expr(expr)) {
            return false;
        }
        const auto right_constant =
            constant_expr_value(expr->children[1].get());
        const std::string& op = expr->op;
        bool immediate = false;
        bool shift_strength = false;
        std::int64_t immediate_value = 0;
        if (right_constant) {
            immediate_value = *right_constant;
            if (op == "+" || op == "-" || op == "&" || op == "|" ||
                op == "^") {
                immediate = true;
            } else if ((op == "<<" || op == ">>") &&
                       immediate_value >= 0 && immediate_value <= 15) {
                immediate = true;
            } else if (op == "*" && immediate_value > 0 &&
                       immediate_value <= 32768 &&
                       (immediate_value & (immediate_value - 1)) == 0) {
                int shift = 0;
                while ((std::int64_t{1} << shift) < immediate_value) {
                    ++shift;
                }
                immediate_value = shift;
                immediate = true;
                shift_strength = true;
            }
        }
        if (immediate) {
            if (!emit_pure_expr(expr->children[0].get(), target, mask, out)) {
                return false;
            }
            const std::string rhs = "#" + std::to_string(immediate_value);
            return emit_pure_operation(shift_strength ? "<<" : op, target,
                                       rhs, out);
        }
        const int scratch = pure_other_register(mask, target);
        if (scratch < 0) {
            return false;
        }
        const unsigned right_mask = mask & ~(1u << target);
        if (right_mask != 0 &&
            pure_register_need(expr->children[1].get()) <=
                pure_popcount(right_mask)) {
            if (!emit_pure_expr(expr->children[0].get(), target, mask, out) ||
                !emit_pure_expr(expr->children[1].get(), scratch,
                                right_mask, out)) {
                return false;
            }
            return emit_pure_operation(op, target,
                                       "R" + std::to_string(scratch), out);
        }
        if (!emit_pure_expr(expr->children[0].get(), target, mask, out)) {
            return false;
        }
        out << "  PUSH R" << target << "\n";
        if (!emit_pure_expr(expr->children[1].get(), target, mask, out)) {
            return false;
        }
        out << "  MOV R" << scratch << ", R" << target << "\n";
        out << "  POP R" << target << "\n";
        return emit_pure_operation(op, target,
                                   "R" + std::to_string(scratch), out);
    }

    bool emit_simple_expr_into(const Expr* expr, int target,
                               std::ostringstream& out) {
        if (is_simple_leaf(expr)) {
            return emit_simple_leaf(expr, target, out);
        }
        if (!is_simple_binary(expr)) {
            return false;
        }
        if (!emit_simple_leaf(expr->children[0].get(), target, out)) {
            return false;
        }
        const auto right_constant =
            constant_expr_value(expr->children[1].get());
        const std::string& op = expr->op;
        bool immediate = false;
        std::int64_t immediate_value = 0;
        bool strength_shift = false;
        if (right_constant) {
            immediate_value = *right_constant;
            if (op == "+" || op == "-" || op == "&" || op == "|" ||
                op == "^") {
                immediate = true;
            } else if ((op == "<<" || op == ">>") &&
                       immediate_value >= 0 && immediate_value <= 15) {
                immediate = true;
            } else if (op == "*" && immediate_value > 0 &&
                       immediate_value <= 32768 &&
                       (immediate_value & (immediate_value - 1)) == 0) {
                int shift = 0;
                while ((std::int64_t{1} << shift) < immediate_value) {
                    ++shift;
                }
                immediate_value = shift;
                immediate = true;
                strength_shift = true;
            }
        }
        int scratch = target == 0 ? 1 : 0;
        std::string rhs;
        if (immediate) {
            rhs = "#" + std::to_string(immediate_value);
        } else {
            if (!emit_simple_leaf(expr->children[1].get(), scratch, out)) {
                return false;
            }
            rhs = "R" + std::to_string(scratch);
        }
        if (strength_shift) {
            out << "  SHL R" << target << ", " << rhs << "\n";
        } else if (op == "+") {
            out << "  ADD R" << target << ", " << rhs << "\n";
        } else if (op == "-") {
            out << "  SUB R" << target << ", " << rhs << "\n";
        } else if (op == "*") {
            out << "  MUL R" << target << ", " << rhs << "\n";
        } else if (op == "/") {
            out << "  DIV R" << target << ", " << rhs << "\n";
        } else if (op == "%") {
            out << "  MOD R" << target << ", " << rhs << "\n";
        } else if (op == "&") {
            out << "  AND R" << target << ", " << rhs << "\n";
        } else if (op == "|") {
            out << "  OR R" << target << ", " << rhs << "\n";
        } else if (op == "^") {
            out << "  XOR R" << target << ", " << rhs << "\n";
        } else if (op == "<<") {
            out << "  SHL R" << target << ", " << rhs << "\n";
        } else if (op == ">>") {
            out << "  SAR R" << target << ", " << rhs << "\n";
        } else if (op == "==" || op == "!=" || op == "<" || op == ">" ||
                   op == "<=" || op == ">=") {
            out << "  CMP R" << target << ", " << rhs << "\n";
            const auto yes = new_label("fastcmp");
            const auto end = new_label("fastcmpend");
            std::string branch = "BEQ";
            if (op == "!=") branch = "BNE";
            if (op == "<") branch = "BLT";
            if (op == ">") branch = "BGT";
            if (op == "<=") branch = "BLE";
            if (op == ">=") branch = "BGE";
            out << "  " << branch << " " << yes << "\n";
            out << "  MOV R" << target << ", #0\n";
            out << "  BRA " << end << "\n";
            out << yes << ":\n";
            out << "  MOV R" << target << ", #1\n";
            out << end << ":\n";
        } else {
            return false;
        }
        return true;
    }

    bool emit_simple_expr(const Expr* expr, std::ostringstream& out) {
        return emit_simple_expr_into(expr, 0, out);
    }

    bool emit_simple_condition(const Expr* expr, const std::string& target,
                               bool branch_when_true,
                               std::ostringstream& out) {
        if (expr == nullptr || expr->kind != Expr::Kind::Binary ||
            !is_simple_binary(expr)) {
            return false;
        }
        const std::string& op = expr->op;
        if (op != "==" && op != "!=" && op != "<" && op != ">" &&
            op != "<=" && op != ">=") {
            return false;
        }
        const auto right_constant =
            constant_expr_value(expr->children[1].get());
        if (!emit_simple_leaf(expr->children[0].get(), 0, out)) {
            return false;
        }
        if (right_constant) {
            out << "  CMP R0, #" << *right_constant << "\n";
        } else {
            if (!emit_simple_leaf(expr->children[1].get(), 1, out)) {
                return false;
            }
            out << "  CMP R0, R1\n";
        }
        std::string branch = "BEQ";
        if (op == "!=") branch = "BNE";
        if (op == "<") branch = "BLT";
        if (op == ">") branch = "BGT";
        if (op == "<=") branch = "BLE";
        if (op == ">=") branch = "BGE";
        if (!branch_when_true) {
            if (branch == "BEQ") branch = "BNE";
            else if (branch == "BNE") branch = "BEQ";
            else if (branch == "BLT") branch = "BGE";
            else if (branch == "BGT") branch = "BLE";
            else if (branch == "BLE") branch = "BGT";
            else if (branch == "BGE") branch = "BLT";
        }
        out << "  " << branch << " " << target << "\n";
        return true;
    }

    bool simple_assignment_target(const Expr* expr, std::string& address,
                                  bool& character) const {
        if (expr == nullptr || expr->kind != Expr::Kind::Identifier) {
            return false;
        }
        const auto local = locals_.find(expr->text);
        if (local != locals_.end()) {
            if (local->second.indirect || local->second.type->is_array() ||
                local->second.type->kind == TypeKind::Struct ||
                local->second.type->kind == TypeKind::Union) {
                return false;
            }
            character = local->second.type->is_char();
            if (!local->second.symbol.empty()) {
                address = local->second.symbol;
            } else {
                address = "R4-" + std::to_string(local->second.offset);
            }
            return true;
        }
        const auto global = global_map_.find(expr->text);
        if (global != global_map_.end()) {
            if (global->second->type->is_array() ||
                global->second->type->kind == TypeKind::Struct ||
                global->second->type->kind == TypeKind::Union) {
                return false;
            }
            character = global->second->type->is_char();
            address = expr->text;
            return true;
        }
        return false;
    }

    bool emit_simple_assignment(const Expr* expr, std::ostringstream& out) {
        if (expr == nullptr || expr->kind != Expr::Kind::Assign ||
            expr->children.size() != 2) {
            return false;
        }
        std::string address;
        bool character = false;
        if (!simple_assignment_target(expr->children[0].get(), address,
                                      character)) {
            return false;
        }
        gen_expr(expr->children[1].get(), out);
        const std::string bracketed = "[" + address + "]";
        out << (character ? "  STB " : "  ST ") << bracketed << ", R0\n";
        return true;
    }

    std::optional<std::int64_t> constant_expr_value(const Expr* expr) {
        if (expr == nullptr) {
            return std::nullopt;
        }
        switch (expr->kind) {
            case Expr::Kind::Number:
                return expr->value;
            case Expr::Kind::Sizeof:
                return expr->children.empty()
                           ? expr->value
                           : expression_type(expr->children[0].get())->size();
            case Expr::Kind::Cast: {
                auto value = constant_expr_value(expr->children[0].get());
                if (!value) {
                    return std::nullopt;
                }
                if (expr->type && expr->type->kind == TypeKind::Char) {
                    return static_cast<std::uint16_t>(*value) & 0xFF;
                }
                return value;
            }
            case Expr::Kind::Unary: {
                auto value = constant_expr_value(expr->children[0].get());
                if (!value) {
                    return std::nullopt;
                }
                const auto bits = static_cast<std::uint16_t>(*value);
                if (expr->op == "+") return *value;
                if (expr->op == "-") {
                    return static_cast<std::uint16_t>(0u - bits);
                }
                if (expr->op == "~") {
                    return static_cast<std::uint16_t>(~bits);
                }
                if (expr->op == "!") return bits == 0 ? 1 : 0;
                return std::nullopt;
            }
            case Expr::Kind::Binary: {
                auto left = constant_expr_value(expr->children[0].get());
                auto right = constant_expr_value(expr->children[1].get());
                if (!left || !right) {
                    return std::nullopt;
                }
                const auto a = static_cast<std::uint16_t>(*left);
                const auto b = static_cast<std::uint16_t>(*right);
                const auto sa = static_cast<std::int16_t>(a);
                const auto sb = static_cast<std::int16_t>(b);
                if (expr->op == "+") {
                    return static_cast<std::uint16_t>(a + b);
                }
                if (expr->op == "-") {
                    return static_cast<std::uint16_t>(a - b);
                }
                if (expr->op == "*") {
                    return static_cast<std::uint16_t>(
                        static_cast<std::uint32_t>(a) *
                        static_cast<std::uint32_t>(b));
                }
                if (expr->op == "/") {
                    if (b == 0) return std::nullopt;
                    return static_cast<std::uint16_t>(
                        static_cast<std::int32_t>(sa) /
                        static_cast<std::int32_t>(sb));
                }
                if (expr->op == "%") {
                    if (b == 0) return std::nullopt;
                    return static_cast<std::uint16_t>(
                        static_cast<std::int32_t>(sa) %
                        static_cast<std::int32_t>(sb));
                }
                if (expr->op == "&") return static_cast<std::uint16_t>(a & b);
                if (expr->op == "|") return static_cast<std::uint16_t>(a | b);
                if (expr->op == "^") return static_cast<std::uint16_t>(a ^ b);
                if (expr->op == "<<") {
                    return static_cast<std::uint16_t>(a << (b & 0x0F));
                }
                if (expr->op == ">>") {
                    return static_cast<std::uint16_t>(sa >> (b & 0x0F));
                }
                if (expr->op == "==") return a == b ? 1 : 0;
                if (expr->op == "!=") return a != b ? 1 : 0;
                if (expr->op == "<") return sa < sb ? 1 : 0;
                if (expr->op == ">") return sa > sb ? 1 : 0;
                if (expr->op == "<=") return sa <= sb ? 1 : 0;
                if (expr->op == ">=") return sa >= sb ? 1 : 0;
                if (expr->op == "&&") return a != 0 && b != 0 ? 1 : 0;
                if (expr->op == "||") return a != 0 || b != 0 ? 1 : 0;
                return std::nullopt;
            }
            case Expr::Kind::Conditional: {
                auto condition =
                    constant_expr_value(expr->children[0].get());
                if (!condition || expr->children.size() != 3) {
                    return std::nullopt;
                }
                return constant_expr_value(
                    expr->children[*condition != 0 ? 1 : 2].get());
            }
            default:
                return std::nullopt;
        }
    }

    void gen_expr(const Expr* expr, std::ostringstream& out) {
        if (expr == nullptr) {
            out << "  MOV R0, #0\n";
            return;
        }
        if (optimize_ && emit_simple_assignment(expr, out)) {
            return;
        }
        if (optimize_ && emit_simple_expr(expr, out)) {
            return;
        }
        if (optimize_ && is_pure_expr(expr) &&
            emit_pure_expr(expr, 0, 0x0Fu, out)) {
            return;
        }
        switch (expr->kind) {
            case Expr::Kind::Number:
                out << "  MOV R0, #" << expr->value << "\n";
                return;
            case Expr::Kind::String:
                out << "  MOV R0, #" << string_label(expr->text) << "\n";
                return;
            case Expr::Kind::Identifier:
                gen_load_identifier(expr->text, out);
                return;
            case Expr::Kind::Call:
                gen_call(expr, out);
                return;
            case Expr::Kind::Assign: {
                const auto target_type = expression_type(expr->children[0].get());
                if (target_type->kind == TypeKind::Struct ||
                    target_type->kind == TypeKind::Union) {
                    gen_addr(expr->children[0].get(), out);
                    out << "  PUSH R0\n";
                    gen_addr(expr->children[1].get(), out);
                    out << "  MOV R1, R0\n";
                    out << "  POP R2\n";
                    for (int offset = 0; offset < target_type->size();) {
                        const int remaining = target_type->size() - offset;
                        if (remaining == 1) {
                            out << "  LDB R0, [R1+" << offset << "]\n";
                            out << "  STB [R2+" << offset << "], R0\n";
                            ++offset;
                        } else {
                            out << "  LD R0, [R1+" << offset << "]\n";
                            out << "  ST [R2+" << offset << "], R0\n";
                            offset += 2;
                        }
                    }
                    out << "  MOV R0, #0\n";
                    return;
                }
                gen_addr(expr->children[0].get(), out);
                out << "  PUSH R0\n";
                gen_expr(expr->children[1].get(), out);
                out << "  MOV R1, R0\n";
                out << "  POP R0\n";
                out << (expression_type(expr->children[0].get())->is_char()
                            ? "  STB [R0], R1\n"
                            : "  ST [R0], R1\n");
                out << "  MOV R0, R1\n";
                return;
            }
            case Expr::Kind::Unary:
                gen_unary(expr, out);
                return;
            case Expr::Kind::Binary:
                gen_binary(expr, out);
                return;
            case Expr::Kind::Index:
                gen_addr(expr, out);
                out << (expression_type(expr)->is_char() ? "  LDB R0, [R0]\n"
                                                        : "  LD R0, [R0]\n");
                return;
            case Expr::Kind::Member: {
                gen_addr(expr, out);
                const auto type = expression_type(expr);
                if (type->is_array()) {
                    return;
                }
                out << (type->is_char() ? "  LDB R0, [R0]\n"
                                        : "  LD R0, [R0]\n");
                return;
            }
            case Expr::Kind::InitList:
                out << "  MOV R0, #0\n";
                return;
            case Expr::Kind::Conditional: {
                const auto else_label = new_label("condelse");
                const auto end = new_label("condend");
                gen_expr(expr->children[0].get(), out);
                out << "  CMP R0, #0\n";
                out << "  BEQ " << else_label << "\n";
                gen_expr(expr->children[1].get(), out);
                out << "  BRA " << end << "\n";
                out << else_label << ":\n";
                gen_expr(expr->children[2].get(), out);
                out << end << ":\n";
                return;
            }
            case Expr::Kind::Sizeof:
                if (!expr->children.empty()) {
                    out << "  MOV R0, #"
                        << expression_type(expr->children[0].get())->size()
                        << "\n";
                } else {
                    out << "  MOV R0, #" << expr->value << "\n";
                }
                return;
            case Expr::Kind::Cast:
                gen_expr(expr->children[0].get(), out);
                if (expr->type && expr->type->kind == TypeKind::Char) {
                    out << "  AND R0, #0xFF\n";
                }
                return;
        }
    }

    void gen_load_identifier(const std::string& name, std::ostringstream& out,
                             int target = 0) {
        const auto local = locals_.find(name);
        if (local != locals_.end()) {
            if (!local->second.symbol.empty()) {
                if (local->second.type->is_array()) {
                    out << "  MOV R" << target << ", #" << local->second.symbol
                        << "\n";
                } else {
                    out << (local->second.type->is_char() ? "  LDB R"
                                                          : "  LD R")
                        << target << ", [" << local->second.symbol << "]\n";
                }
                return;
            }
            if (local->second.type->is_array()) {
                out << "  LEA R" << target << ", [R4-" << local->second.offset
                    << "]\n";
            } else {
                out << (local->second.type->is_char() ? "  LDB R"
                                                      : "  LD R")
                    << target << ", [R4-" << local->second.offset << "]\n";
            }
            return;
        }
        const auto global = global_map_.find(name);
        if (global != global_map_.end()) {
            if (global->second->type->is_array()) {
                out << "  MOV R" << target << ", #" << name << "\n";
            } else {
                out << (global->second->type->is_char() ? "  LDB R"
                                                        : "  LD R")
                    << target << ", [" << name << "]\n";
            }
            return;
        }
        out << "  MOV R" << target << ", #0\n";
    }

    void gen_addr(const Expr* expr, std::ostringstream& out) {
        if (expr->kind == Expr::Kind::Identifier) {
            const auto local = locals_.find(expr->text);
            if (local != locals_.end()) {
                if (local->second.indirect) {
                    out << "  LD R0, [R4-" << local->second.offset << "]\n";
                } else if (!local->second.symbol.empty()) {
                    out << "  MOV R0, #" << local->second.symbol << "\n";
                } else {
                    out << "  LEA R0, [R4-" << local->second.offset << "]\n";
                }
                return;
            }
            out << "  MOV R0, #" << expr->text << "\n";
            return;
        }
        if (expr->kind == Expr::Kind::Call) {
            gen_call(expr, out);
            return;
        }
        if (expr->kind == Expr::Kind::Unary && expr->op == "*") {
            gen_expr(expr->children[0].get(), out);
            return;
        }
        if (expr->kind == Expr::Kind::Index) {
            const auto base_type = expression_type(expr->children[0].get());
            if (base_type->kind == TypeKind::Pointer) {
                gen_expr(expr->children[0].get(), out);
            } else {
                gen_addr(expr->children[0].get(), out);
            }
            out << "  PUSH R0\n";
            gen_expr(expr->children[1].get(), out);
            const auto type = expression_type(expr);
            if (type->size() > 1) {
                out << "  SHL R0, #1\n";
            }
            out << "  POP R1\n";
            out << "  ADD R0, R1\n";
            return;
        }
        if (expr->kind == Expr::Kind::Member) {
            if (expr->op == "->") {
                gen_expr(expr->children[0].get(), out);
            } else {
                gen_addr(expr->children[0].get(), out);
            }
            auto base = expression_type(expr->children[0].get());
            if (expr->op == "->" && base->kind == TypeKind::Pointer) {
                base = base->base;
            }
            int offset = 0;
            if (base && (base->kind == TypeKind::Struct ||
                    base->kind == TypeKind::Union)) {
                for (const auto& member : base->members) {
                    if (member.name == expr->text) {
                        offset = member.offset;
                        break;
                    }
                }
            }
            if (offset != 0) {
                out << "  ADD R0, #" << offset << "\n";
            }
            return;
        }
        out << "  MOV R0, #0\n";
    }

    void gen_unary(const Expr* expr, std::ostringstream& out) {
        if (expr->op == "&") {
            gen_addr(expr->children[0].get(), out);
            return;
        }
        if (expr->op == "*") {
            gen_expr(expr->children[0].get(), out);
            out << (expression_type(expr)->is_char() ? "  LDB R0, [R0]\n"
                                                     : "  LD R0, [R0]\n");
            return;
        }
        if (expr->op == "++" || expr->op == "--") {
            gen_addr(expr->children[0].get(), out);
            out << "  PUSH R0\n";
            out << (expression_type(expr->children[0].get())->is_char()
                        ? "  LDB R1, [R0]\n"
                        : "  LD R1, [R0]\n");
            out << (expr->op == "++" ? "  ADD R1, #1\n" : "  SUB R1, #1\n");
            out << "  POP R0\n";
            out << (expression_type(expr->children[0].get())->is_char()
                        ? "  STB [R0], R1\n"
                        : "  ST [R0], R1\n");
            out << "  MOV R0, R1\n";
            return;
        }
        gen_expr(expr->children[0].get(), out);
        if (expr->op == "-") {
            out << "  NEG R0\n";
        } else if (expr->op == "~") {
            out << "  NOT R0\n";
        } else if (expr->op == "!") {
            const auto yes = new_label("nottrue");
            const auto end = new_label("notend");
            out << "  CMP R0, #0\n";
            out << "  BEQ " << yes << "\n";
            out << "  MOV R0, #0\n";
            out << "  BRA " << end << "\n";
            out << yes << ":\n";
            out << "  MOV R0, #1\n";
            out << end << ":\n";
        }
    }

    void gen_binary(const Expr* expr, std::ostringstream& out) {
        if (expr->op == "&&" || expr->op == "||") {
            const auto short_label = new_label("logic");
            const auto end = new_label("logicend");
            gen_expr(expr->children[0].get(), out);
            out << "  CMP R0, #0\n";
            if (expr->op == "&&") {
                out << "  BEQ " << short_label << "\n";
                gen_expr(expr->children[1].get(), out);
                out << "  CMP R0, #0\n";
                out << "  BEQ " << short_label << "\n";
                out << "  MOV R0, #1\n";
            } else {
                out << "  BNE " << short_label << "\n";
                gen_expr(expr->children[1].get(), out);
                out << "  CMP R0, #0\n";
                out << "  BNE " << short_label << "\n";
                out << "  MOV R0, #0\n";
            }
            out << "  BRA " << end << "\n";
            out << short_label << ":\n";
            out << "  MOV R0, #" << (expr->op == "&&" ? 0 : 1) << "\n";
            out << end << ":\n";
            return;
        }

        gen_expr(expr->children[0].get(), out);
        out << "  PUSH R0\n";
        gen_expr(expr->children[1].get(), out);
        out << "  MOV R1, R0\n";
        out << "  POP R0\n";
        if (expr->op == "+") out << "  ADD R0, R1\n";
        else if (expr->op == "-") out << "  SUB R0, R1\n";
        else if (expr->op == "*") out << "  MUL R0, R1\n";
        else if (expr->op == "/") out << "  DIV R0, R1\n";
        else if (expr->op == "%") out << "  MOD R0, R1\n";
        else if (expr->op == "&") out << "  AND R0, R1\n";
        else if (expr->op == "|") out << "  OR R0, R1\n";
        else if (expr->op == "^") out << "  XOR R0, R1\n";
        else if (expr->op == "<<") out << "  SHL R0, R1\n";
        else if (expr->op == ">>") out << "  SAR R0, R1\n";
        else if (expr->op == "==" || expr->op == "!=" || expr->op == "<" ||
                 expr->op == ">" || expr->op == "<=" || expr->op == ">=") {
            out << "  CMP R0, R1\n";
            const auto yes = new_label("cmpyes");
            const auto end = new_label("cmpend");
            std::string branch = "BEQ";
            if (expr->op == "!=") branch = "BNE";
            else if (expr->op == "<") branch = "BLT";
            else if (expr->op == ">") branch = "BGT";
            else if (expr->op == "<=") branch = "BLE";
            else if (expr->op == ">=") branch = "BGE";
            out << "  " << branch << " " << yes << "\n";
            out << "  MOV R0, #0\n";
            out << "  BRA " << end << "\n";
            out << yes << ":\n";
            out << "  MOV R0, #1\n";
            out << end << ":\n";
        }
    }

    void gen_call(const Expr* expr, std::ostringstream& out) {
        if (expr->text == "__builtin_va_start" &&
            expr->children.size() == 1) {
            gen_expr(expr->children[0].get(), out);
            out << "  MOV R1, R0\n";
            out << "  LEA R0, [R4+"
                << (4 + current_named_params_ * 2) << "]\n";
            out << "  ST [R1], R0\n";
            out << "  MOV R0, #0\n";
            return;
        }
        if (expr->text == "__builtin_va_arg" &&
            expr->children.size() == 2) {
            int size = 2;
            const auto* size_expr = expr->children[1].get();
            if (size_expr->kind == Expr::Kind::Number) {
                size = static_cast<int>(size_expr->value);
            } else if (size_expr->kind == Expr::Kind::Sizeof &&
                       size_expr->children.empty()) {
                size = static_cast<int>(size_expr->value);
            }
            gen_expr(expr->children[0].get(), out);
            out << "  MOV R1, R0\n";
            out << "  LD R2, [R1]\n";
            out << (size == 1 ? "  LDB R0, [R2]\n" : "  LD R0, [R2]\n");
            out << "  PUSH R0\n";
            out << "  ADD R2, #" << size << "\n";
            out << "  ST [R1], R2\n";
            out << "  POP R0\n";
            return;
        }
        if (expr->text == "__host_call" && expr->children.size() == 2 &&
            expr->children[0]->kind == Expr::Kind::Number &&
            expr->children[0]->value >= 0 && expr->children[0]->value <= 255) {
            gen_expr(expr->children[1].get(), out);
            out << "  SYS #" << expr->children[0]->value << "\n";
            out << "  LD R0, [R0]\n";
            return;
        }
        const auto local_function = function_map_.find(expr->text);
        if (local_function != function_map_.end() &&
            local_function->second->variadic) {
            for (auto it = expr->children.rbegin(); it != expr->children.rend();
                 ++it) {
                gen_expr(it->get(), out);
                out << "  PUSH R0\n";
            }
            out << "  CALL " << expr->text << "\n";
            if (!expr->children.empty()) {
                out << "  ADD SP, #" << expr->children.size() * 2 << "\n";
            }
            return;
        }
        external_calls_.insert(expr->text);
        std::vector<bool> struct_args(expr->children.size(), false);
        if (local_function != function_map_.end()) {
            for (std::size_t i = 0; i < struct_args.size(); ++i) {
                const auto& parameters = local_function->second->parameters;
                if (i < parameters.size() &&
                    (parameters[i].type->kind == TypeKind::Struct ||
                     parameters[i].type->kind == TypeKind::Union)) {
                    struct_args[i] = true;
                }
            }
        }
        int temporary_bytes = 0;
        if (!expr->children.empty() && expr->children.size() <= 3) {
            bool all_pure = true;
            for (std::size_t i = 0; i < expr->children.size(); ++i) {
                const auto* argument = expr->children[i].get();
                if (struct_args[i] || !is_pure_expr(argument)) {
                    all_pure = false;
                    break;
                }
            }
            if (all_pure) {
                for (std::size_t i = 0; i < expr->children.size(); ++i) {
                    unsigned mask = 0x0Fu;
                    for (std::size_t previous = 1; previous <= i;
                         ++previous) {
                        mask &= ~(1u << previous);
                    }
                    if (!emit_pure_expr(
                            expr->children[i].get(),
                            static_cast<int>(i) + 1, mask, out)) {
                        all_pure = false;
                        break;
                    }
                }
                if (all_pure) {
                    out << "  CALL " << expr->text << "\n";
                    return;
                }
            }
        }
        if (expr->children.size() == 1 && !struct_args[0]) {
            gen_expr(expr->children[0].get(), out);
            out << "  MOV R1, R0\n";
            out << "  CALL " << expr->text << "\n";
            return;
        }
        for (std::size_t i = expr->children.size(); i-- > 0;) {
            const auto* argument = expr->children[i].get();
            if (!struct_args[i]) {
                gen_expr(argument, out);
                out << "  PUSH R0\n";
                continue;
            }
            const auto type = expression_type(argument);
            const int size = type ? type->size() : 2;
            gen_addr(argument, out);
            out << "  SUB SP, #" << size << "\n";
            out << "  MOV R1, SP\n";
            for (int offset = 0; offset < size;) {
                if (size - offset == 1) {
                    out << "  LDB R3, [R0+" << offset << "]\n";
                    out << "  STB [R1+" << offset << "], R3\n";
                    ++offset;
                } else {
                    out << "  LD R3, [R0+" << offset << "]\n";
                    out << "  ST [R1+" << offset << "], R3\n";
                    offset += 2;
                }
            }
            out << "  MOV R0, SP\n";
            out << "  PUSH R0\n";
            temporary_bytes += size;
        }
        const std::size_t count = expr->children.size();
        const std::size_t register_count = std::min<std::size_t>(3, count);
        for (std::size_t i = 0; i < register_count; ++i) {
            out << "  POP R" << (i + 1) << "\n";
        }
        out << "  CALL " << expr->text << "\n";
        const int stack_bytes =
            (count > 3 ? static_cast<int>(count - 3) * 2 : 0) +
            temporary_bytes;
        if (stack_bytes > 0) {
            out << "  ADD SP, #" << stack_bytes << "\n";
        }
    }

    Program program_;
    std::string source_name_;
    bool debug_ = false;
    bool optimize_ = true;
    std::vector<IrValue> ir_values_;
    std::unordered_map<std::string, int> ir_vars_;
    std::vector<IrBlock> ir_blocks_;
    std::vector<int> ir_break_;
    std::vector<int> ir_continue_;
    int ir_current_ = 0;
    bool ir_terminated_ = false;
    bool ir_failed_ = false;
    std::unordered_map<std::string, Global*> global_map_;
    std::unordered_map<std::string, Function*> function_map_;
    std::unordered_map<std::string, Variable> locals_;
    std::map<std::string, std::string> string_literals_;
    std::set<std::string> external_calls_;
    std::vector<std::string> break_labels_;
    std::vector<std::string> continue_labels_;
    std::vector<DebugVar> debug_vars_;
    std::vector<DebugFrame> debug_frames_;
    std::vector<DebugType> debug_types_;
    std::string return_label_;
    int frame_size_ = 0;
    int current_named_params_ = 0;
    int label_counter_ = 0;
    std::string function_label_;
    std::shared_ptr<Type> current_return_type_;
    std::string current_return_symbol_;
};

}  // namespace

CompileResult compile_c(const std::string& source, const CompileOptions& options) {
    CompileResult result;
    try {
        const std::filesystem::path source_path(options.source_name);
        const std::string base_dir =
            source_path.has_parent_path() ? source_path.parent_path().string()
                                          : "";
        const std::string preprocessed =
            preprocess_source(source, base_dir, options.include_paths);
        Lexer lexer(preprocessed);
        auto tokens = lexer.run();
        Parser parser(std::move(tokens), options.source_name,
                      options.optimize);
        auto program = parser.parse();
        Codegen codegen(std::move(program), options.source_name,
                        options.debug, options.optimize);
        result.assembly = codegen.run();
        result.ok = true;
    } catch (const CompileError& error) {
        result.diagnostics.push_back(
            CompileDiagnostic{error.line, error.column, error.what()});
    } catch (const std::exception& error) {
        result.diagnostics.push_back(
            CompileDiagnostic{0, 1, error.what()});
    }
    return result;
}

}  // namespace kr16
