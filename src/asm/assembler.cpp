#include "kr16/assembler.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "kr16/diagnostic.hpp"
#include "kr16/instruction.hpp"

namespace kr16 {
namespace {

class AsmError : public std::runtime_error {
public:
    AsmError(int line, int column, std::string message)
        : std::runtime_error(std::move(message)), line_(line), column_(column) {}

    int line() const noexcept { return line_; }
    int column() const noexcept { return column_; }

private:
    int line_;
    int column_;
};

struct SourceLine {
    std::string file;
    int line = 1;
    std::string text;
};

std::string trim(std::string_view text) {
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

std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& ch : result) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return result;
}

std::string strip_comment(std::string_view text) {
    bool in_string = false;
    bool in_char = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            if (ch == '\\') {
                ++i;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            if (ch == '\\') {
                ++i;
            } else if (ch == '\'') {
                in_char = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '\'') {
            in_char = true;
            continue;
        }
        if (ch == ';') {
            return std::string(text.substr(0, i));
        }
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            return std::string(text.substr(0, i));
        }
    }
    return std::string(text);
}

std::vector<std::string> split_operands(std::string_view text) {
    std::vector<std::string> operands;
    std::size_t start = 0;
    int bracket = 0;
    int paren = 0;
    bool in_string = false;
    bool in_char = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            if (ch == '\\') {
                ++i;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            if (ch == '\\') {
                ++i;
            } else if (ch == '\'') {
                in_char = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == '\'') {
            in_char = true;
        } else if (ch == '[') {
            ++bracket;
        } else if (ch == ']') {
            --bracket;
        } else if (ch == '(') {
            ++paren;
        } else if (ch == ')') {
            --paren;
        } else if (ch == ',' && bracket == 0 && paren == 0) {
            operands.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    const std::string last = trim(text.substr(start));
    if (!last.empty() || !operands.empty()) {
        operands.push_back(last);
    }
    return operands;
}

std::vector<std::string> split_words(std::string_view text) {
    std::vector<std::string> words;
    std::istringstream input{std::string(text)};
    std::string word;
    while (input >> word) {
        words.push_back(std::move(word));
    }
    return words;
}

int parse_register(std::string_view text) {
    const std::string name = lower(trim(text));
    if (name.size() == 2 && name[0] == 'r' && name[1] >= '0' && name[1] <= '4') {
        return name[1] - '0';
    }
    if (name == "sp") return 5;
    if (name == "sr") return 6;
    if (name == "pc") return 7;
    if (name == "fp") return 4;
    return -1;
}

struct ExprValue {
    std::int64_t value = 0;
    std::string symbol;
    bool symbolic = false;
    bool resolved = true;
    bool external = false;
};

class ExprParser {
public:
    using Lookup = std::function<ExprValue(std::string_view)>;

    ExprParser(std::string_view text, int line, Lookup lookup)
        : text_(text), line_(line), lookup_(std::move(lookup)) {}

    ExprValue parse() {
        ExprValue value = parse_or();
        skip_space();
        if (position_ != text_.size()) {
            fail("unexpected character in expression");
        }
        return value;
    }

private:
    void fail(const char* message) const {
        throw AsmError(line_, static_cast<int>(position_) + 1, message);
    }

    void skip_space() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
    }

    bool consume(std::string_view token) {
        skip_space();
        if (text_.substr(position_, token.size()) == token) {
            position_ += token.size();
            return true;
        }
        return false;
    }

    bool peek(std::string_view token) {
        skip_space();
        return text_.substr(position_, token.size()) == token;
    }

    static ExprValue combine(char op, const ExprValue& a, const ExprValue& b) {
        if (!a.symbolic && !b.symbolic) {
            ExprValue result;
            switch (op) {
                case '+': result.value = a.value + b.value; break;
                case '-': result.value = a.value - b.value; break;
                case '*': result.value = a.value * b.value; break;
                case '/':
                    if (b.value == 0) throw AsmError(0, 1, "division by zero");
                    result.value = a.value / b.value;
                    break;
                case '%':
                    if (b.value == 0) throw AsmError(0, 1, "division by zero");
                    result.value = a.value % b.value;
                    break;
                case '&': result.value = a.value & b.value; break;
                case '|': result.value = a.value | b.value; break;
                case '^': result.value = a.value ^ b.value; break;
                case '<': result.value = a.value << b.value; break;
                case '>': result.value = a.value >> b.value; break;
                default: throw AsmError(0, 1, "invalid expression operator");
            }
            return result;
        }
        if (a.symbolic && !b.symbolic) {
            ExprValue result = a;
            if (op == '+') {
                result.value += b.value;
            } else if (op == '-') {
                result.value -= b.value;
            } else {
                throw AsmError(0, 1, "symbolic expression is too complex");
            }
            return result;
        }
        if (!a.symbolic && b.symbolic && op == '+') {
            ExprValue result = b;
            result.value += a.value;
            return result;
        }
        throw AsmError(0, 1, "symbolic expression is too complex");
    }

    ExprValue parse_or() {
        ExprValue value = parse_xor();
        while (!peek("||") && consume("|")) {
            value = combine('|', value, parse_xor());
        }
        return value;
    }

    ExprValue parse_xor() {
        ExprValue value = parse_and();
        while (consume("^")) {
            value = combine('^', value, parse_and());
        }
        return value;
    }

    ExprValue parse_and() {
        ExprValue value = parse_shift();
        while (!peek("&&") && consume("&")) {
            value = combine('&', value, parse_shift());
        }
        return value;
    }

    ExprValue parse_shift() {
        ExprValue value = parse_add();
        for (;;) {
            if (consume("<<")) {
                value = combine('<', value, parse_add());
            } else if (consume(">>")) {
                value = combine('>', value, parse_add());
            } else {
                break;
            }
        }
        return value;
    }

    ExprValue parse_add() {
        ExprValue value = parse_mul();
        for (;;) {
            if (consume("+")) {
                value = combine('+', value, parse_mul());
            } else if (consume("-")) {
                value = combine('-', value, parse_mul());
            } else {
                break;
            }
        }
        return value;
    }

    ExprValue parse_mul() {
        ExprValue value = parse_unary();
        for (;;) {
            if (consume("*")) {
                value = combine('*', value, parse_unary());
            } else if (consume("/")) {
                value = combine('/', value, parse_unary());
            } else if (consume("%")) {
                value = combine('%', value, parse_unary());
            } else {
                break;
            }
        }
        return value;
    }

    ExprValue parse_unary() {
        if (consume("+")) {
            return parse_unary();
        }
        if (consume("-")) {
            ExprValue value = parse_unary();
            if (value.symbolic) {
                fail("cannot negate a symbol");
            }
            value.value = -value.value;
            return value;
        }
        if (consume("~")) {
            ExprValue value = parse_unary();
            if (value.symbolic) {
                fail("cannot complement a symbol");
            }
            value.value = ~value.value;
            return value;
        }
        return parse_primary();
    }

    ExprValue parse_primary() {
        skip_space();
        if (consume("(")) {
            ExprValue value = parse_or();
            if (!consume(")")) {
                fail("missing ')'");
            }
            return value;
        }
        if (position_ < text_.size() && text_[position_] == '\'') {
            return parse_char();
        }
        if (position_ < text_.size() &&
            std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
            return parse_number();
        }
        if (position_ < text_.size() &&
            (std::isalpha(static_cast<unsigned char>(text_[position_])) != 0 ||
             text_[position_] == '_' || text_[position_] == '.')) {
            const std::size_t start = position_;
            while (position_ < text_.size()) {
                const char ch = text_[position_];
                if (std::isalnum(static_cast<unsigned char>(ch)) == 0 &&
                    ch != '_' && ch != '.') {
                    break;
                }
                ++position_;
            }
            const std::string name(text_.substr(start, position_ - start));
            const std::string lowered = lower(name);
            if ((lowered == "low" || lowered == "high") && consume("(")) {
                ExprValue value = parse_or();
                if (!consume(")")) {
                    fail("missing ')'");
                }
                if (value.symbolic) {
                    fail("low/high require a constant");
                }
                if (lowered == "low") {
                    value.value &= 0xFFFF;
                } else {
                    value.value = (value.value >> 16) & 0xFFFF;
                }
                return value;
            }
            return lookup_(name);
        }
        fail("expected expression");
        return {};
    }

    ExprValue parse_number() {
        std::size_t start = position_;
        int base = 10;
        if (text_.substr(position_, 2) == "0x" ||
            text_.substr(position_, 2) == "0X") {
            base = 16;
            position_ += 2;
        } else if (text_.substr(position_, 2) == "0b" ||
                   text_.substr(position_, 2) == "0B") {
            base = 2;
            position_ += 2;
        } else if (text_.substr(position_, 2) == "0o" ||
                   text_.substr(position_, 2) == "0O") {
            base = 8;
            position_ += 2;
        }
        const std::size_t digits = position_;
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            const bool digit =
                base == 16 ? std::isxdigit(static_cast<unsigned char>(ch)) != 0
                           : std::isdigit(static_cast<unsigned char>(ch)) != 0;
            if (!digit) {
                break;
            }
            ++position_;
        }
        if (digits == position_) {
            position_ = start;
            fail("invalid number");
        }
        const std::string token(text_.substr(start, position_ - start));
        try {
            return ExprValue{static_cast<std::int64_t>(std::stoll(token, nullptr, 0)), "", false, true, false};
        } catch (const std::exception&) {
            fail("number out of range");
        }
        return {};
    }

    ExprValue parse_char() {
        ++position_;
        if (position_ >= text_.size()) {
            fail("unterminated character literal");
        }
        char value = text_[position_++];
        if (value == '\\') {
            if (position_ >= text_.size()) {
                fail("unterminated escape");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case 'n': value = '\n'; break;
                case 't': value = '\t'; break;
                case 'r': value = '\r'; break;
                case '\\': value = '\\'; break;
                case '\'': value = '\''; break;
                case '"': value = '"'; break;
                case '0': value = '\0'; break;
                default: value = escape; break;
            }
        }
        if (position_ >= text_.size() || text_[position_] != '\'') {
            fail("unterminated character literal");
        }
        ++position_;
        return ExprValue{static_cast<std::int64_t>(
                             static_cast<unsigned char>(value)),
                         "", false, true, false};
    }

    std::string_view text_;
    std::size_t position_ = 0;
    int line_;
    Lookup lookup_;
};

struct AsmSymbol {
    std::string name;
    int section = -1;
    std::uint32_t value = 0;
    std::uint32_t size = 0;
    bool defined = false;
    bool global = false;
    bool external = false;
    SymbolType type = SymbolType::NoType;
};

struct SectionState {
    std::string name;
    std::uint16_t flags = 0;
    std::uint16_t alignment = 2;
    bool nobits = false;
    std::vector<std::uint8_t> data;
    std::uint32_t nobits_size = 0;
};

struct RelocRecord {
    std::string section;
    std::uint32_t offset = 0;
    RelocationType type = RelocationType::Abs16;
    std::string symbol;
    std::int32_t addend = 0;
};

struct LineParts {
    std::string label;
    std::string mnemonic;
    std::vector<std::string> operands;
};

class Assembler {
public:
    Assembler(const AsmOptions& options, std::vector<SourceLine> lines)
        : options_(options), lines_(std::move(lines)) {}

    AsmResult run();
    std::vector<SourceLine> expand_includes(const std::vector<SourceLine>& input);
    std::vector<SourceLine> expand_macros(const std::vector<SourceLine>& input,
                                          int depth = 0);

private:
    struct Macro {
        std::vector<std::string> parameters;
        std::vector<SourceLine> body;
    };

    struct Conditional {
        bool parent_active = true;
        bool condition = true;
        bool else_seen = false;
        bool active() const { return parent_active && condition; }
    };

    struct LineRow {
        std::uint16_t address = 0;
        std::uint16_t file = 0;
        std::uint16_t line = 0;
        std::uint8_t column = 0;
        std::uint8_t flags = 0;
    };

    struct DebugFile {
        std::string path;
        std::string build_directory;
    };

    bool active() const {
        return conditionals_.empty() || conditionals_.back().active();
    }
    void handle_conditional(const std::string& mnemonic,
                            const std::vector<std::string>& operands, int line);
    void append_listing(const SourceLine& line, std::uint32_t start_address,
                        std::uint32_t start_offset,
                        std::size_t start_section);
    void append_line_row(std::uint32_t address, int line);
    void emit_debug_line();
    void reset_for_pass(bool first);
    void process_line(const SourceLine& line);
    LineParts split_line(const SourceLine& line) const;
    void define_label(const std::string& name, int line);
    void ensure_object_section(const std::string& name);
    SectionState& section();
    std::uint16_t current_address() const;
    std::uint32_t current_offset() const;
    void emit_u8(std::uint8_t value, int line);
    void emit_u16(std::uint16_t value, int line);
    void emit_u32(std::uint32_t value, int line);
    void align_to(std::uint32_t alignment, int line);
    void emit_instruction(const Instruction& instruction, int line,
                          std::optional<RelocRecord> relocation = std::nullopt);
    void add_reloc(std::uint32_t offset, RelocationType type,
                   const std::string& symbol, std::int32_t addend);
    ExprValue eval(std::string_view text, int line) const;
    ExprValue lookup_expr_symbol(std::string_view name) const;
    void handle_directive(const std::string& mnemonic,
                          const std::vector<std::string>& operands, int line);
    void handle_instruction(const std::string& mnemonic,
                            const std::vector<std::string>& operands, int line);
    void handle_data(const std::string& directive,
                     const std::vector<std::string>& operands, int line);
    void handle_equ(const std::vector<std::string>& operands, int line);
    void handle_global(const std::vector<std::string>& operands);
    void handle_extern(const std::vector<std::string>& operands);
    void handle_section(const std::string& name);
    std::optional<std::pair<bool, int>> parse_memory(std::string_view text,
                                                     int line,
                                                     ExprValue& expression);
    void emit_symbol_word(const ExprValue& value, int line);
    void emit_immediate(Op op, int rd, const ExprValue& value, int line);
    void emit_load_store(Op op, int rd, bool absolute, int base,
                         const ExprValue& displacement, int line);
    void emit_jump(bool call, const ExprValue& target, int line);
    void emit_branch(std::uint8_t condition, const ExprValue& target, int line);
    void emit_mul_pair(Op op, int rd, int rs, int line);
    std::string current_file_;
    int current_line_ = 0;
    const AsmOptions& options_;
    std::vector<SourceLine> lines_;
    std::vector<SectionState> sections_;
    std::size_t current_section_ = 0;
    std::vector<AsmSymbol> symbols_;
    std::unordered_map<std::string, std::size_t> symbol_index_;
    std::unordered_map<std::string, std::int64_t> constants_;
    std::unordered_set<std::string> globals_;
    std::unordered_map<std::string, Macro> macros_;
    std::vector<Conditional> conditionals_;
    std::vector<RelocRecord> relocations_;
    std::vector<LineRow> line_rows_;
    std::vector<DebugFile> debug_files_;
    std::uint16_t current_debug_file_ = 0;
    std::uint16_t current_debug_line_ = 0;
    std::uint8_t current_debug_column_ = 0;
    std::vector<std::uint8_t> raw_;
    bool first_pass_ = true;
    std::vector<std::string> listing_;
};

std::string read_file_text(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        throw AsmError(0, 1, "cannot open " + path);
    }
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

std::vector<SourceLine> split_lines(std::string_view text,
                                    const std::string& file) {
    std::vector<SourceLine> lines;
    int number = 1;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::size_t length =
            end == std::string_view::npos ? text.size() - start : end - start;
        std::string line(text.substr(start, length));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(SourceLine{file, number, std::move(line)});
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
        ++number;
    }
    return lines;
}

std::string unquote(std::string_view text) {
    std::string value = trim(text);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        throw AsmError(0, 1, "expected a quoted string");
    }
    std::string result;
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size()) {
            const char escape = value[++i];
            switch (escape) {
                case 'n': result.push_back('\n'); break;
                case 't': result.push_back('\t'); break;
                case 'r': result.push_back('\r'); break;
                case '\\': result.push_back('\\'); break;
                case '"': result.push_back('"'); break;
                default: result.push_back(escape); break;
            }
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

}  // namespace

std::vector<SourceLine> Assembler::expand_includes(
    const std::vector<SourceLine>& input) {
    std::vector<SourceLine> result;
    for (const auto& line : input) {
        const std::string text = trim(strip_comment(line.text));
        if (text.rfind(".include", 0) == 0) {
            const std::string argument = trim(text.substr(8));
            const std::string name = unquote(argument);
            std::filesystem::path path(name);
            if (path.is_relative()) {
                path = std::filesystem::path(line.file).parent_path() / path;
            }
            std::filesystem::path found = path;
            if (!std::filesystem::exists(found)) {
                for (const auto& include_path : options_.include_paths) {
                    const auto candidate =
                        std::filesystem::path(include_path) / name;
                    if (std::filesystem::exists(candidate)) {
                        found = candidate;
                        break;
                    }
                }
            }
            if (!std::filesystem::exists(found)) {
                throw AsmError(line.line, 1, "cannot find include " + name);
            }
            const auto nested = split_lines(read_file_text(found.string()),
                                            found.string());
            const auto expanded = expand_includes(nested);
            result.insert(result.end(), expanded.begin(), expanded.end());
        } else {
            result.push_back(line);
        }
    }
    return result;
}

std::vector<SourceLine> Assembler::expand_macros(
    const std::vector<SourceLine>& input, int depth) {
    if (depth > 32) {
        throw AsmError(0, 1, "macro expansion is too deep");
    }

    std::vector<SourceLine> result;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const SourceLine& line = input[i];
        const std::string text = trim(strip_comment(line.text));
        if (text.rfind(".macro", 0) == 0) {
            const std::string rest = trim(text.substr(6));
            const auto space = rest.find_first_of(" \t");
            const std::string name = lower(rest.substr(0, space));
            if (name.empty()) {
                throw AsmError(line.line, 1, ".macro needs a name");
            }
            Macro macro;
            if (space != std::string::npos) {
                macro.parameters = split_operands(rest.substr(space + 1));
            }
            ++i;
            bool closed = false;
            for (; i < input.size(); ++i) {
                const std::string body_text =
                    trim(strip_comment(input[i].text));
                if (body_text == ".endm") {
                    closed = true;
                    break;
                }
                macro.body.push_back(input[i]);
            }
            if (!closed) {
                throw AsmError(line.line, 1, "missing .endm");
            }
            macros_[name] = std::move(macro);
            continue;
        }

        std::string instruction_text = text;
        const auto colon = instruction_text.find(':');
        if (colon != std::string::npos) {
            const std::string candidate = trim(instruction_text.substr(0, colon));
            if (!candidate.empty() &&
                candidate.find_first_of(" \t[]") == std::string::npos) {
                instruction_text = trim(instruction_text.substr(colon + 1));
            }
        }
        if (instruction_text.empty()) {
            result.push_back(line);
            continue;
        }
        const auto space = instruction_text.find_first_of(" \t");
        const std::string name =
            lower(instruction_text.substr(0, space));
        const auto macro_it = macros_.find(name);
        if (macro_it == macros_.end()) {
            result.push_back(line);
            continue;
        }

        std::vector<std::string> arguments;
        if (space != std::string::npos) {
            arguments = split_operands(instruction_text.substr(space + 1));
        }
        if (arguments.size() != macro_it->second.parameters.size()) {
            throw AsmError(line.line, 1,
                           "macro " + name + " expects " +
                               std::to_string(macro_it->second.parameters.size()) +
                               " arguments");
        }

        std::vector<SourceLine> expanded;
        for (const auto& body_line : macro_it->second.body) {
            std::string substituted = body_line.text;
            for (std::size_t p = 0; p < arguments.size(); ++p) {
                const std::string token = "\\" + macro_it->second.parameters[p];
                std::size_t position = 0;
                while ((position = substituted.find(token, position)) !=
                       std::string::npos) {
                    substituted.replace(position, token.size(), arguments[p]);
                    position += arguments[p].size();
                }
            }
            expanded.push_back(
                SourceLine{line.file, line.line, std::move(substituted)});
        }
        const auto nested = expand_macros(expanded, depth + 1);
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}

void Assembler::reset_for_pass(bool first) {
    first_pass_ = first;
    raw_.clear();
    relocations_.clear();
    conditionals_.clear();
    listing_.clear();
    line_rows_.clear();
    current_debug_file_ = 0;
    current_debug_line_ = 0;
    current_debug_column_ = 0;
    current_section_ = 0;
    current_file_.clear();
    current_line_ = 0;
    if (sections_.empty()) {
        SectionState text;
        text.name = ".text";
        text.flags = SectionAlloc | SectionExec;
        sections_.push_back(std::move(text));
    }
    for (auto& section : sections_) {
        section.data.clear();
        section.nobits_size = 0;
    }
    if (first) {
        symbols_.clear();
        symbol_index_.clear();
        constants_.clear();
        globals_.clear();
        debug_files_.clear();
    }
}

AsmResult Assembler::run() {
    AsmResult result;
    try {
        const auto expanded = expand_includes(lines_);
        lines_ = expanded;
        lines_ = expand_macros(lines_);

        reset_for_pass(true);
        for (const auto& line : lines_) {
            process_line(line);
        }

        reset_for_pass(false);
        for (const auto& line : lines_) {
            process_line(line);
        }
        emit_debug_line();

        for (auto& symbol : symbols_) {
            if (globals_.count(symbol.name) != 0) {
                symbol.global = true;
            }
        }

        if (options_.raw) {
            result.raw = raw_;
            result.ok = true;
            result.listing = [&]() {
                std::string text;
                for (const auto& line : listing_) {
                    text += line;
                    text.push_back('\n');
                }
                return text;
            }();
            return result;
        }

        Container object;
        object.kind = ContainerKind::Rel;
        for (const auto& state : sections_) {
            Section section;
            section.name = state.name;
            section.flags = state.flags;
            section.alignment = state.alignment;
            if (state.nobits) {
                section.type = SectionType::Nobits;
                section.nobits_size = state.nobits_size;
            } else {
                section.data = state.data;
            }
            object.sections.push_back(std::move(section));
        }

        std::unordered_map<std::string, std::uint16_t> section_numbers;
        for (std::size_t i = 0; i < sections_.size(); ++i) {
            section_numbers.emplace(sections_[i].name,
                                    static_cast<std::uint16_t>(i + 1));
        }

        for (const auto& symbol : symbols_) {
            if (!symbol.defined && !symbol.external && !symbol.global) {
                continue;
            }
            kr16::Symbol output;
            output.name = symbol.name;
            output.value = static_cast<std::uint16_t>(symbol.value & 0xFFFF);
            output.size = symbol.size;
            output.section =
                symbol.external
                    ? 0
                    : section_numbers.at(
                          sections_[static_cast<std::size_t>(symbol.section)]
                              .name);
            output.binding = symbol.external
                                 ? SymbolBinding::Extern
                                 : (symbol.global ? SymbolBinding::Global
                                                  : SymbolBinding::Local);
            output.type = symbol.type;
            object.symbols.push_back(std::move(output));
        }

        std::unordered_map<std::string, std::uint32_t> symbol_numbers;
        for (std::size_t i = 0; i < object.symbols.size(); ++i) {
            symbol_numbers.emplace(object.symbols[i].name,
                                   static_cast<std::uint32_t>(i));
        }
        for (const auto& relocation : relocations_) {
            const auto number = symbol_numbers.find(relocation.symbol);
            if (number == symbol_numbers.end()) {
                continue;
            }
            Relocation output;
            output.section = section_numbers.at(relocation.section);
            output.type = relocation.type;
            output.offset = relocation.offset;
            output.symbol = number->second;
            output.addend = relocation.addend;
            object.relocations.push_back(output);
        }

        result.object = std::move(object);
        result.ok = true;
        for (const auto& line : listing_) {
            result.listing += line;
            result.listing.push_back('\n');
        }
        return result;
    } catch (const AsmError& error) {
        result.diagnostics.push_back(
            AsmDiagnostic{current_file_, error.line(), error.column(),
                          error.what()});
    } catch (const std::exception& error) {
        result.diagnostics.push_back(
            AsmDiagnostic{current_file_, current_line_, 1, error.what()});
    }
    return result;
}

SectionState& Assembler::section() {
    if (current_section_ >= sections_.size()) {
        current_section_ = 0;
    }
    return sections_[current_section_];
}

std::uint16_t Assembler::current_address() const {
    if (options_.raw) {
        return static_cast<std::uint16_t>(options_.base + raw_.size());
    }
    return static_cast<std::uint16_t>(sections_[current_section_].nobits
                                          ? sections_[current_section_].nobits_size
                                          : sections_[current_section_].data.size());
}

std::uint32_t Assembler::current_offset() const {
    if (options_.raw) {
        return static_cast<std::uint32_t>(raw_.size());
    }
    return sections_[current_section_].nobits
               ? sections_[current_section_].nobits_size
               : static_cast<std::uint32_t>(sections_[current_section_].data.size());
}

void Assembler::emit_u8(std::uint8_t value, int line) {
    if (options_.raw) {
        raw_.push_back(value);
        return;
    }
    auto& target = section();
    if (target.nobits) {
        throw AsmError(line, 1, "cannot emit data in a NOBITS section");
    }
    target.data.push_back(value);
}

void Assembler::emit_u16(std::uint16_t value, int line) {
    emit_u8(static_cast<std::uint8_t>(value & 0xFF), line);
    emit_u8(static_cast<std::uint8_t>((value >> 8) & 0xFF), line);
}

void Assembler::emit_u32(std::uint32_t value, int line) {
    emit_u8(static_cast<std::uint8_t>(value & 0xFF), line);
    emit_u8(static_cast<std::uint8_t>((value >> 8) & 0xFF), line);
    emit_u8(static_cast<std::uint8_t>((value >> 16) & 0xFF), line);
    emit_u8(static_cast<std::uint8_t>((value >> 24) & 0xFF), line);
}

void Assembler::align_to(std::uint32_t alignment, int line) {
    (void)line;
    if (alignment <= 1) {
        return;
    }
    if (options_.raw) {
        const std::uint32_t address = options_.base +
                                      static_cast<std::uint32_t>(raw_.size());
        const std::uint32_t padding =
            (alignment - (address % alignment)) % alignment;
        raw_.insert(raw_.end(), padding, 0);
        return;
    }
    auto& target = section();
    if (target.nobits) {
        target.nobits_size = (target.nobits_size + alignment - 1) /
                             alignment * alignment;
        return;
    }
    const std::uint32_t padding =
        (alignment - (target.data.size() % alignment)) % alignment;
    target.data.insert(target.data.end(), padding, 0);
}

void Assembler::add_reloc(std::uint32_t offset, RelocationType type,
                          const std::string& symbol, std::int32_t addend) {
    if (first_pass_ || options_.raw) {
        return;
    }
    relocations_.push_back(RelocRecord{section().name, offset, type, symbol,
                                       addend});
}

void Assembler::emit_instruction(
    const Instruction& instruction, int line,
    std::optional<RelocRecord> relocation) {
    const auto words = instruction.encode();
    const std::uint32_t start = current_offset();
    for (const auto word : words) {
        emit_u16(word, line);
    }
    if (relocation.has_value()) {
        add_reloc(start + 2, relocation->type, relocation->symbol,
                  relocation->addend);
    }
}

void Assembler::define_label(const std::string& name, int line) {
    if (first_pass_) {
        if (symbol_index_.count(name) != 0) {
            throw AsmError(line, 1, "duplicate label " + name);
        }
        AsmSymbol symbol;
        symbol.name = name;
        symbol.defined = true;
        symbol.section = static_cast<int>(current_section_);
        symbol.value = current_address();
        symbols_.push_back(symbol);
        symbol_index_[name] = symbols_.size() - 1;
        return;
    }
    const auto it = symbol_index_.find(name);
    if (it == symbol_index_.end()) {
        throw AsmError(line, 1, "label disappeared in second pass: " + name);
    }
    symbols_[it->second].value = current_address();
}

void Assembler::ensure_object_section(const std::string& name) {
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        if (sections_[i].name == name) {
            current_section_ = i;
            return;
        }
    }
    SectionState section;
    section.name = name;
    if (name == ".text") {
        section.flags = SectionAlloc | SectionExec;
    } else if (name == ".rodata") {
        section.flags = SectionAlloc;
    } else if (name == ".data") {
        section.flags = SectionAlloc | SectionWrite;
    } else if (name == ".bss") {
        section.flags = SectionAlloc | SectionWrite;
        section.nobits = true;
    } else if (name.rfind(".debug", 0) == 0) {
        section.flags = SectionDebug;
    } else {
        section.flags = SectionAlloc;
    }
    sections_.push_back(std::move(section));
    current_section_ = sections_.size() - 1;
}

ExprValue Assembler::lookup_expr_symbol(std::string_view name) const {
    if (name == ".") {
        ExprValue value;
        value.value = current_address();
        return value;
    }
    const std::string key(name);
    const auto constant = constants_.find(key);
    if (constant != constants_.end()) {
        ExprValue value;
        value.value = constant->second;
        return value;
    }
    const auto it = symbol_index_.find(key);
    if (it != symbol_index_.end()) {
        const auto& symbol = symbols_[it->second];
        ExprValue value;
        value.symbol = key;
        value.symbolic = true;
        value.resolved = symbol.defined;
        value.external = symbol.external;
        value.value = symbol.defined ? symbol.value : 0;
        return value;
    }
    ExprValue value;
    value.symbol = key;
    value.symbolic = true;
    value.resolved = false;
    value.external = true;
    return value;
}

ExprValue Assembler::eval(std::string_view text, int line) const {
    ExprParser parser(text, line,
                      [this](std::string_view name) {
                          return lookup_expr_symbol(name);
                      });
    return parser.parse();
}

LineParts Assembler::split_line(const SourceLine& line) const {
    LineParts parts;
    std::string text = trim(strip_comment(line.text));
    if (text.empty()) {
        return parts;
    }
    const auto colon = text.find(':');
    if (colon != std::string::npos) {
        const std::string candidate = trim(text.substr(0, colon));
        if (!candidate.empty() &&
            candidate.find_first_of(" \t[]") == std::string::npos) {
            parts.label = candidate;
            text = trim(text.substr(colon + 1));
        }
    }
    if (text.empty()) {
        return parts;
    }
    const auto space = text.find_first_of(" \t");
    if (space == std::string::npos) {
        parts.mnemonic = lower(text);
        return parts;
    }
    parts.mnemonic = lower(text.substr(0, space));
    parts.operands = split_operands(text.substr(space + 1));
    return parts;
}

void Assembler::append_line_row(std::uint32_t address, int line) {
    (void)line;
    if (first_pass_) {
        return;
    }
    if (current_debug_line_ == 0) {
        return;
    }
    LineRow row;
    row.address = static_cast<std::uint16_t>(address & 0xFFFF);
    row.file = current_debug_file_;
    row.line = current_debug_line_;
    row.column = current_debug_column_;
    row.flags = 0;
    if (!line_rows_.empty() && line_rows_.back().address == row.address &&
        line_rows_.back().file == row.file &&
        line_rows_.back().line == row.line &&
        line_rows_.back().column == row.column) {
        return;
    }
    line_rows_.push_back(row);
}

void Assembler::emit_debug_line() {
    if (first_pass_ || options_.raw || line_rows_.empty()) {
        return;
    }
    std::vector<std::uint8_t> data;
    auto append8 = [&data](std::uint8_t value) { data.push_back(value); };
    auto append16 = [&append8](std::uint16_t value) {
        append8(static_cast<std::uint8_t>(value & 0xFF));
        append8(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    };
    auto append32 = [&append8](std::uint32_t value) {
        append8(static_cast<std::uint8_t>(value & 0xFF));
        append8(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        append8(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        append8(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    };
    append32(static_cast<std::uint32_t>(line_rows_.size()));
    append16(1);
    append16(0);
    for (const auto& row : line_rows_) {
        append16(row.address);
        append16(row.file);
        append16(row.line);
        append8(row.column);
        append8(row.flags);
    }
    SectionState section;
    section.name = ".debug_line";
    section.flags = SectionDebug;
    section.alignment = 2;
    section.data = std::move(data);
    sections_.push_back(std::move(section));
}

void Assembler::append_listing(const SourceLine& line,
                               std::uint32_t start_address,
                               std::uint32_t start_offset,
                               std::size_t start_section) {
    if (first_pass_) {
        return;
    }
    std::vector<std::uint8_t> bytes;
    if (options_.raw) {
        if (start_offset <= raw_.size()) {
            bytes.assign(raw_.begin() + static_cast<std::ptrdiff_t>(start_offset),
                         raw_.end());
        }
    } else if (start_section < sections_.size() &&
               !sections_[start_section].nobits) {
        const auto& data = sections_[start_section].data;
        if (start_offset <= data.size()) {
            bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(start_offset),
                         data.end());
        }
    }

    std::ostringstream text;
    text << "0x" << std::hex << std::uppercase << std::setw(4)
         << std::setfill('0') << (start_address & 0xFFFF) << "  ";
    for (std::size_t i = 0; i < bytes.size() && i < 8; ++i) {
        text << std::setw(2) << static_cast<unsigned>(bytes[i]) << " ";
    }
    text << " " << line.text;
    listing_.push_back(text.str());
}

void Assembler::process_line(const SourceLine& line) {
    current_file_ = line.file;
    current_line_ = line.line;
    const auto parts = split_line(line);
    const auto start_address = current_address();
    const auto start_offset = current_offset();
    const auto start_section = current_section_;

    const bool conditional = parts.mnemonic == ".if" ||
                             parts.mnemonic == ".else" ||
                             parts.mnemonic == ".endif";
    if (!active() && !conditional) {
        return;
    }

    if (!parts.label.empty() && active()) {
        define_label(parts.label, line.line);
    }
    if (parts.mnemonic.empty()) {
        append_listing(line, start_address, start_offset, start_section);
        return;
    }
    if (parts.mnemonic.front() == '.') {
        handle_directive(parts.mnemonic, parts.operands, line.line);
    } else {
        handle_instruction(parts.mnemonic, parts.operands, line.line);
        append_line_row(start_address, line.line);
    }
    append_listing(line, start_address, start_offset, start_section);
}

void Assembler::handle_conditional(const std::string& mnemonic,
                                   const std::vector<std::string>& operands,
                                   int line) {
    if (mnemonic == ".if") {
        const bool parent_active = active();
        bool condition = false;
        if (parent_active) {
            if (operands.empty()) {
                throw AsmError(line, 1, ".if needs a condition");
            }
            const auto value = eval(operands[0], line);
            if (value.symbolic) {
                throw AsmError(line, 1, ".if needs a constant condition");
            }
            condition = value.value != 0;
        }
        conditionals_.push_back(Conditional{parent_active, condition, false});
        return;
    }
    if (mnemonic == ".else") {
        if (conditionals_.empty()) {
            throw AsmError(line, 1, ".else without .if");
        }
        auto& condition = conditionals_.back();
        if (condition.else_seen) {
            throw AsmError(line, 1, "duplicate .else");
        }
        condition.else_seen = true;
        condition.condition = !condition.condition;
        return;
    }
    if (mnemonic == ".endif") {
        if (conditionals_.empty()) {
            throw AsmError(line, 1, ".endif without .if");
        }
        conditionals_.pop_back();
    }
}

void Assembler::handle_directive(const std::string& mnemonic,
                                 const std::vector<std::string>& operands,
                                 int line) {
    if (mnemonic == ".if" || mnemonic == ".else" || mnemonic == ".endif") {
        handle_conditional(mnemonic, operands, line);
        return;
    }
    if (mnemonic == ".text") {
        ensure_object_section(".text");
    } else if (mnemonic == ".data") {
        ensure_object_section(".data");
    } else if (mnemonic == ".rodata") {
        ensure_object_section(".rodata");
    } else if (mnemonic == ".bss") {
        ensure_object_section(".bss");
    } else if (mnemonic == ".section") {
        if (operands.empty()) {
            throw AsmError(line, 1, ".section needs a name");
        }
        handle_section(trim(operands[0]));
    } else if (mnemonic == ".byte" || mnemonic == ".word" ||
               mnemonic == ".long" || mnemonic == ".ascii" ||
               mnemonic == ".asciz") {
        handle_data(mnemonic, operands, line);
    } else if (mnemonic == ".align" || mnemonic == ".balign" ||
               mnemonic == ".p2align") {
        if (operands.empty()) {
            throw AsmError(line, 1, ".align needs a value");
        }
        const auto value = eval(operands[0], line);
        if (value.symbolic || value.value < 0) {
            throw AsmError(line, 1, ".align needs a constant");
        }
        std::uint32_t alignment = static_cast<std::uint32_t>(value.value);
        if (mnemonic == ".p2align") {
            alignment = 1u << alignment;
        }
        align_to(alignment, line);
    } else if (mnemonic == ".org") {
        if (!options_.raw) {
            throw AsmError(line, 1, ".org is only allowed in raw mode");
        }
        if (operands.empty()) {
            throw AsmError(line, 1, ".org needs an address");
        }
        const auto value = eval(operands[0], line);
        if (value.symbolic || value.value < options_.base ||
            value.value > 0xFFFF) {
            throw AsmError(line, 1, ".org address out of range");
        }
        const auto target = static_cast<std::size_t>(value.value - options_.base);
        if (target < raw_.size()) {
            throw AsmError(line, 1, ".org cannot move backwards");
        }
        raw_.resize(target, 0);
    } else if (mnemonic == ".equ" || mnemonic == ".set") {
        handle_equ(operands, line);
    } else if (mnemonic == ".global" || mnemonic == ".globl") {
        handle_global(operands);
    } else if (mnemonic == ".extern") {
        handle_extern(operands);
    } else if (mnemonic == ".type" || mnemonic == ".size") {
        if (operands.size() < 2) {
            throw AsmError(line, 1, mnemonic + " needs two operands");
        }
        const auto it = symbol_index_.find(trim(operands[0]));
        if (it != symbol_index_.end()) {
            if (mnemonic == ".type") {
                const std::string kind = lower(trim(operands[1]));
                if (kind == "@function" || kind == "function") {
                    symbols_[it->second].type = SymbolType::Function;
                } else if (kind == "@object" || kind == "object") {
                    symbols_[it->second].type = SymbolType::Object;
                }
            } else {
                const auto value = eval(operands[1], line);
                if (!value.symbolic && value.value >= 0) {
                    symbols_[it->second].size =
                        static_cast<std::uint32_t>(value.value);
                }
            }
        }
    } else if (mnemonic == ".file") {
        if (operands.empty()) {
            throw AsmError(line, 1, ".file needs a path");
        }
        std::size_t index = debug_files_.size();
        std::string path;
        const std::string text = trim(operands[0]);
        const auto quote = text.find('"');
        if (quote != std::string::npos) {
            const std::string prefix = trim(text.substr(0, quote));
            if (!prefix.empty()) {
                const auto value = eval(prefix, line);
                if (value.symbolic || value.value < 0) {
                    throw AsmError(line, 1, ".file index must be a constant");
                }
                index = static_cast<std::size_t>(value.value);
            }
            path = unquote(text.substr(quote));
        } else if (operands.size() >= 2) {
            const auto value = eval(operands[0], line);
            if (value.symbolic || value.value < 0) {
                throw AsmError(line, 1, ".file index must be a constant");
            }
            index = static_cast<std::size_t>(value.value);
            path = unquote(operands[1]);
        } else {
            throw AsmError(line, 1, ".file needs a quoted path");
        }
        if (debug_files_.size() <= index) {
            debug_files_.resize(index + 1);
        }
        debug_files_[index].path = path;
        debug_files_[index].build_directory = ".";
    } else if (mnemonic == ".loc") {
        std::vector<std::string> words = operands;
        if (words.size() == 1) {
            words = split_words(words[0]);
        }
        if (words.size() < 2) {
            throw AsmError(line, 1, ".loc needs file and line");
        }
        const auto file = eval(words[0], line);
        const auto line_number = eval(words[1], line);
        if (file.symbolic || line_number.symbolic || file.value < 0 ||
            file.value > 0xFFFF || line_number.value < 0 ||
            line_number.value > 0xFFFF) {
            throw AsmError(line, 1, ".loc values are out of range");
        }
        current_debug_file_ = static_cast<std::uint16_t>(file.value);
        current_debug_line_ = static_cast<std::uint16_t>(line_number.value);
        current_debug_column_ = 0;
        if (words.size() >= 3) {
            const auto column = eval(words[2], line);
            if (!column.symbolic && column.value >= 0 && column.value <= 255) {
                current_debug_column_ = static_cast<std::uint8_t>(column.value);
            }
        }
    } else if (mnemonic == ".include") {
        throw AsmError(line, 1, "nested include was not expanded");
    } else {
        throw AsmError(line, 1, "unsupported directive " + mnemonic);
    }
}

void Assembler::handle_equ(const std::vector<std::string>& operands, int line) {
    if (operands.size() != 2) {
        throw AsmError(line, 1, ".equ needs a name and a value");
    }
    const std::string name = trim(operands[0]);
    const auto value = eval(operands[1], line);
    if (value.symbolic) {
        throw AsmError(line, 1, ".equ requires a constant expression");
    }
    constants_[name] = value.value;
}

void Assembler::handle_global(const std::vector<std::string>& operands) {
    for (const auto& operand : operands) {
        const std::string name = trim(operand);
        globals_.insert(name);
        const auto it = symbol_index_.find(name);
        if (it != symbol_index_.end()) {
            symbols_[it->second].global = true;
        }
    }
}

void Assembler::handle_extern(const std::vector<std::string>& operands) {
    for (const auto& operand : operands) {
        const std::string name = trim(operand);
        if (symbol_index_.count(name) == 0) {
            AsmSymbol symbol;
            symbol.name = name;
            symbol.external = true;
            symbols_.push_back(std::move(symbol));
            symbol_index_[name] = symbols_.size() - 1;
        }
    }
}

void Assembler::handle_section(const std::string& name) {
    ensure_object_section(name);
}

void Assembler::handle_data(const std::string& directive,
                            const std::vector<std::string>& operands,
                            int line) {
    if (directive == ".ascii" || directive == ".asciz") {
        for (const auto& operand : operands) {
            const std::string text = unquote(operand);
            for (const char ch : text) {
                emit_u8(static_cast<std::uint8_t>(ch), line);
            }
            if (directive == ".asciz") {
                emit_u8(0, line);
            }
        }
        return;
    }
    for (const auto& operand : operands) {
        const auto value = eval(operand, line);
        if (directive == ".byte") {
            if (value.symbolic) {
                throw AsmError(line, 1, ".byte cannot hold a symbol");
            }
            emit_u8(static_cast<std::uint8_t>(value.value & 0xFF), line);
        } else if (directive == ".word") {
            if (value.symbolic) {
                if (value.resolved) {
                    emit_u16(static_cast<std::uint16_t>(value.value & 0xFFFF),
                             line);
                } else {
                    const auto offset = current_offset();
                    emit_u16(0, line);
                    add_reloc(offset, RelocationType::Abs16, value.symbol,
                              static_cast<std::int32_t>(value.value));
                }
            } else {
                emit_u16(static_cast<std::uint16_t>(value.value & 0xFFFF),
                         line);
            }
        } else if (directive == ".long") {
            if (value.symbolic) {
                throw AsmError(line, 1, ".long cannot hold a symbol");
            }
            emit_u32(static_cast<std::uint32_t>(value.value), line);
        }
    }
}

std::optional<std::pair<bool, int>> Assembler::parse_memory(
    std::string_view text, int line, ExprValue& expression) {
    const std::string value = trim(text);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        throw AsmError(line, 1, "expected [memory] operand");
    }
    const std::string inner = trim(value.substr(1, value.size() - 2));
    if (inner.empty()) {
        throw AsmError(line, 1, "empty memory operand");
    }
    const auto space = inner.find_first_of("+-");
    const std::string first = trim(inner.substr(0, space));
    const int reg = parse_register(first);
    if (reg >= 0) {
        std::string rest = space == std::string::npos
                               ? ""
                               : trim(inner.substr(space));
        if (rest.empty()) {
            expression = ExprValue{};
        } else if (rest.front() != '+' && rest.front() != '-') {
            throw AsmError(line, 1, "expected +/- displacement");
        } else {
            const bool negative = rest.front() == '-';
            rest = trim(rest.substr(1));
            expression = eval(rest, line);
            if (negative) {
                if (expression.symbolic) {
                    throw AsmError(line, 1, "negative symbol displacement");
                }
                expression.value = -expression.value;
            }
        }
        return std::make_pair(false, reg);
    }
    expression = eval(inner, line);
    return std::make_pair(true, 0);
}

void Assembler::emit_immediate(Op op, int rd, const ExprValue& value, int line) {
    Instruction instruction;
    instruction.op = op;
    instruction.rd = static_cast<std::uint8_t>(rd);
    instruction.is_immediate = true;
    instruction.imm = static_cast<std::int32_t>(value.value);
    if (value.symbolic) {
        instruction.force_long = true;
    }
    if (value.symbolic && !value.resolved) {
        instruction.imm = 0;
        emit_instruction(instruction, line,
                         RelocRecord{"", 0, RelocationType::Abs16, value.symbol,
                                     static_cast<std::int32_t>(value.value)});
    } else {
        emit_instruction(instruction, line);
    }
}

void Assembler::emit_load_store(Op op, int rd, bool absolute, int base,
                                const ExprValue& displacement, int line) {
    Instruction instruction;
    instruction.op = op;
    instruction.rd = static_cast<std::uint8_t>(rd);
    instruction.rs = static_cast<std::uint8_t>(base);
    instruction.absolute = absolute;
    instruction.imm = static_cast<std::int32_t>(displacement.value);
    if (displacement.symbolic) {
        instruction.force_long = true;
        if (op == Op::LoadByte || op == Op::LoadSignedByte ||
            op == Op::StoreByte) {
            if (!absolute) {
                throw AsmError(
                    line, 1,
                    "byte indexed forms require a constant 6-bit displacement");
            }
        }
    }
    if (displacement.symbolic && !displacement.resolved) {
        instruction.imm = 0;
        emit_instruction(instruction, line,
                         RelocRecord{"", 0, RelocationType::Abs16,
                                     displacement.symbol, 
                                     static_cast<std::int32_t>(displacement.value)});
    } else {
        emit_instruction(instruction, line);
    }
}

void Assembler::emit_jump(bool call, const ExprValue& target, int line) {
    if (target.symbolic && !target.resolved) {
        Instruction instruction;
        instruction.op = call ? Op::CallRel : Op::JmpRel;
        instruction.imm = 0;
        instruction.size_bytes = 4;
        const auto words = instruction.encode();
        const auto start = current_offset();
        for (const auto word : words) {
            emit_u16(word, line);
        }
        add_reloc(start + 2, RelocationType::Pcrel16w, target.symbol,
                  static_cast<std::int32_t>(target.value));
        return;
    }
    const auto next = static_cast<std::int64_t>(current_address()) + 4;
    const auto displacement = target.value - next;
    if (displacement < -32768 || displacement > 32767 ||
        (displacement % 2) != 0) {
        throw AsmError(line, 1, "jump target out of range");
    }
    Instruction instruction;
    instruction.op = call ? Op::CallRel : Op::JmpRel;
    instruction.imm = static_cast<std::int32_t>(displacement / 2);
    instruction.size_bytes = 4;
    emit_instruction(instruction, line);
}

void Assembler::emit_branch(std::uint8_t condition, const ExprValue& target,
                            int line) {
    if (target.symbolic && !target.resolved) {
        throw AsmError(line, 1, "external branch target is not supported");
    }
    const auto next = static_cast<std::int64_t>(current_address()) + 2;
    const auto displacement = target.value - next;
    if (displacement < -256 || displacement > 254 || (displacement % 2) != 0) {
        throw AsmError(line, 1, "branch target out of range");
    }
    Instruction instruction;
    instruction.op = Op::Bcc;
    instruction.cond = condition;
    instruction.imm = static_cast<std::int32_t>(displacement);
    instruction.size_bytes = 2;
    emit_instruction(instruction, line);
}

void Assembler::emit_mul_pair(Op op, int rd, int rs, int line) {
    Instruction instruction;
    instruction.op = op;
    instruction.rd = static_cast<std::uint8_t>(rd);
    instruction.rs = static_cast<std::uint8_t>(rs);
    emit_instruction(instruction, line);
}

void Assembler::handle_instruction(const std::string& mnemonic,
                                   const std::vector<std::string>& operands,
                                   int line) {
    auto require_operands = [&](std::size_t count) {
        if (operands.size() != count) {
            throw AsmError(line, 1, mnemonic + " needs " +
                                       std::to_string(count) + " operands");
        }
    };
    auto reg_operand = [&](std::size_t index) {
        const int reg = parse_register(operands[index]);
        if (reg < 0) {
            throw AsmError(line, 1, "expected register");
        }
        return reg;
    };

    if (mnemonic == "nop" || mnemonic == "ret" || mnemonic == "reti" ||
        mnemonic == "brk" || mnemonic == "hlt" || mnemonic == "ei" ||
        mnemonic == "di" || mnemonic == "wait") {
        require_operands(0);
        Instruction instruction;
        if (mnemonic == "nop") instruction.op = Op::Nop;
        if (mnemonic == "ret") instruction.op = Op::Ret;
        if (mnemonic == "reti") instruction.op = Op::Reti;
        if (mnemonic == "brk") instruction.op = Op::Brk;
        if (mnemonic == "hlt") instruction.op = Op::Hlt;
        if (mnemonic == "ei") instruction.op = Op::Ei;
        if (mnemonic == "di") instruction.op = Op::Di;
        if (mnemonic == "wait") instruction.op = Op::Wait;
        emit_instruction(instruction, line);
        return;
    }

    if (mnemonic == "trap" || mnemonic == "syscall") {
        require_operands(1);
        std::string text = trim(operands[0]);
        if (text.empty() || text.front() != '#') {
            throw AsmError(line, 1, "expected #number");
        }
        const auto value = eval(text.substr(1), line);
        if (value.symbolic || value.value < 0 || value.value > 255) {
            throw AsmError(line, 1, "service number out of range");
        }
        Instruction instruction;
        instruction.op = mnemonic == "trap" ? Op::Trap : Op::Syscall;
        instruction.n = static_cast<std::uint8_t>(value.value);
        emit_instruction(instruction, line);
        return;
    }

    if (mnemonic.size() > 1 && mnemonic.front() == 'b') {
        const std::string condition_name = mnemonic.substr(1);
        const std::vector<std::pair<std::string, int>> conditions = {
            {"eq", 0},  {"ne", 1},  {"cs", 2},  {"hs", 2},  {"cc", 3},
            {"lo", 3},  {"mi", 4},  {"pl", 5},  {"vs", 6},  {"vc", 7},
            {"hi", 8},  {"ls", 9},  {"ge", 10}, {"lt", 11}, {"gt", 12},
            {"le", 13}, {"al", 14}, {"ra", 14}, {"nv", 15}};
        for (const auto& [name, code] : conditions) {
            if (condition_name == name) {
                require_operands(1);
                emit_branch(static_cast<std::uint8_t>(code),
                            eval(operands[0], line), line);
                return;
            }
        }
    }

    if (mnemonic == "jmp" || mnemonic == "call") {
        require_operands(1);
        const std::string operand = trim(operands[0]);
        if (operand.rfind('#', 0) == 0) {
            const auto value = eval(operand.substr(1), line);
            Instruction instruction;
            instruction.op = Op::Mov;
            instruction.rd = 7;
            instruction.is_immediate = true;
            instruction.imm = static_cast<std::int32_t>(value.value);
            instruction.force_long = true;
            emit_instruction(instruction, line);
            return;
        }
        const int reg = parse_register(operand);
        if (reg >= 0) {
            Instruction instruction;
            instruction.op = mnemonic == "jmp" ? Op::JmpReg : Op::CallReg;
            instruction.rs = static_cast<std::uint8_t>(reg);
            emit_instruction(instruction, line);
            return;
        }
        emit_jump(mnemonic == "call", eval(operand, line), line);
        return;
    }

    if (mnemonic == "push" || mnemonic == "pop") {
        require_operands(1);
        Instruction instruction;
        instruction.op = mnemonic == "push" ? Op::Push : Op::Pop;
        instruction.rd = static_cast<std::uint8_t>(reg_operand(0));
        emit_instruction(instruction, line);
        return;
    }

    if (mnemonic == "mov") {
        require_operands(2);
        const int rd = reg_operand(0);
        const std::string source = trim(operands[1]);
        if (!source.empty() && source.front() == '#') {
            emit_immediate(Op::Mov, rd, eval(source.substr(1), line), line);
        } else {
            emit_mul_pair(Op::Mov, rd, reg_operand(1), line);
        }
        return;
    }

    const std::vector<std::string> alu_names = {"add", "sub", "cmp", "and",
                                                 "or",  "xor", "test"};
    for (const auto& name : alu_names) {
        if (mnemonic == name) {
            require_operands(2);
            const int rd = reg_operand(0);
            const std::string source = trim(operands[1]);
            Op op = Op::Add;
            if (name == "add") op = Op::Add;
            if (name == "sub") op = Op::Sub;
            if (name == "cmp") op = Op::Cmp;
            if (name == "and") op = Op::And;
            if (name == "or") op = Op::Or;
            if (name == "xor") op = Op::Xor;
            if (name == "test") op = Op::Test;
            if (!source.empty() && source.front() == '#') {
                emit_immediate(op, rd, eval(source.substr(1), line), line);
            } else {
                emit_mul_pair(op, rd, reg_operand(1), line);
            }
            return;
        }
    }

    const std::vector<std::pair<std::string, Op>> unary = {
        {"neg", Op::Neg},  {"not", Op::Not}, {"inc", Op::Inc},
        {"dec", Op::Dec},  {"sext", Op::SignExtendByte},
        {"zxtb", Op::ZeroExtendByte}};
    for (const auto& [name, op] : unary) {
        if (mnemonic == name) {
            require_operands(1);
            const int rd = reg_operand(0);
            emit_mul_pair(op, rd, rd, line);
            return;
        }
    }

    const std::vector<std::pair<std::string, Op>> binary_reg = {
        {"adc", Op::AddCarry}, {"sbc", Op::SubBorrow}, {"mul", Op::Mul},
        {"div", Op::Div},      {"divu", Op::DivU},     {"mod", Op::Mod},
        {"modu", Op::ModU}};
    for (const auto& [name, op] : binary_reg) {
        if (mnemonic == name) {
            require_operands(2);
            emit_mul_pair(op, reg_operand(0), reg_operand(1), line);
            return;
        }
    }

    if (mnemonic == "muls" || mnemonic == "mulu") {
        require_operands(operands.size() == 1 ? 1 : 2);
        const int rd = reg_operand(0);
        int rs = static_cast<int>(rd) + 1;
        if (operands.size() == 2) {
            rs = reg_operand(1);
        }
        emit_mul_pair(mnemonic == "muls" ? Op::MulS : Op::MulU, rd, rs, line);
        return;
    }

    const std::vector<std::pair<std::string, Op>> shift_reg = {
        {"shl", Op::Shl}, {"shr", Op::Shr}, {"sar", Op::Sar},
        {"btst", Op::Btst}, {"bset", Op::Bset}, {"bclr", Op::Bclr},
        {"btog", Op::Btog}};
    for (const auto& [name, op] : shift_reg) {
        if (mnemonic == name) {
            require_operands(2);
            const int rd = reg_operand(0);
            const std::string source = trim(operands[1]);
            Instruction instruction;
            instruction.op = op;
            instruction.rd = static_cast<std::uint8_t>(rd);
            if (!source.empty() && source.front() == '#') {
                const auto value = eval(source.substr(1), line);
                if (value.symbolic || value.value < 0 || value.value > 15) {
                    throw AsmError(line, 1, "shift/bit count out of range");
                }
                instruction.imm = static_cast<std::int32_t>(value.value);
                instruction.is_immediate = true;
            } else {
                instruction.rs = static_cast<std::uint8_t>(reg_operand(1));
            }
            emit_instruction(instruction, line);
            return;
        }
    }

    const std::vector<std::pair<std::string, Op>> memory_ops = {
        {"ld", Op::Load},       {"ldb", Op::LoadByte},
        {"ldsb", Op::LoadSignedByte}, {"st", Op::Store},
        {"stb", Op::StoreByte}, {"lea", Op::Lea}};
    for (const auto& [name, op] : memory_ops) {
        if (mnemonic == name) {
            require_operands(2);
            const bool store = op == Op::Store || op == Op::StoreByte;
            const std::size_t mem_index = store ? 0 : 1;
            const std::size_t reg_index = store ? 1 : 0;
            const int rd = reg_operand(reg_index);
            ExprValue displacement;
            const auto memory = parse_memory(operands[mem_index], line,
                                             displacement);
            if (!memory.has_value()) {
                throw AsmError(line, 1, "invalid memory operand");
            }
            emit_load_store(op, rd, memory->first, memory->second,
                            displacement, line);
            return;
        }
    }

    throw AsmError(line, 1, "unknown instruction " + mnemonic);
}

AsmResult assemble_text(std::string_view source, const AsmOptions& options) {
    Assembler assembler(options, split_lines(source, options.source_name));
    return assembler.run();
}

AsmResult assemble_file(const std::string& path, AsmOptions options) {
    try {
        options.source_name = path;
        const std::string text = read_file_text(path);
        return assemble_text(text, options);
    } catch (const AsmError& error) {
        AsmResult result;
        result.diagnostics.push_back(
            AsmDiagnostic{path, error.line(), error.column(), error.what()});
        return result;
    }
}

}  // namespace kr16
