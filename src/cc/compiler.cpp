#include "kr16/compiler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

enum class TypeKind { Void, Char, Int, Pointer, Array };

struct Type {
    TypeKind kind = TypeKind::Int;
    int count = 0;
    std::shared_ptr<Type> base;

    int size() const {
        switch (kind) {
            case TypeKind::Void: return 0;
            case TypeKind::Char: return 1;
            case TypeKind::Int: return 2;
            case TypeKind::Pointer: return 2;
            case TypeKind::Array: return count * (base ? base->size() : 2);
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
                while (position_ < source_.size() && source_[position_] != '\n') {
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
        Conditional, Sizeof
    };
    Kind kind = Kind::Number;
    std::int64_t value = 0;
    std::string text;
    std::string op;
    std::vector<std::unique_ptr<Expr>> children;
    int line = 0;
};

struct Decl {
    std::string name;
    std::shared_ptr<Type> type;
    std::unique_ptr<Expr> init;
    int line = 0;
};

struct Stmt {
    enum class Kind {
        Empty, Expr, Return, Compound, If, While, DoWhile, For, Decl, Break,
        Continue
    };
    Kind kind = Kind::Empty;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> expr2;
    std::unique_ptr<Expr> expr3;
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Stmt> else_body;
    std::unique_ptr<Stmt> init;
    std::vector<std::unique_ptr<Stmt>> statements;
    std::vector<Decl> declarations;
    int line = 0;
};

struct Function {
    std::string name;
    std::shared_ptr<Type> return_type;
    std::vector<Decl> parameters;
    std::unique_ptr<Stmt> body;
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
    Parser(std::vector<Token> tokens, std::string source_name)
        : tokens_(std::move(tokens)), source_name_(std::move(source_name)) {}

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
               text == "signed" || text == "const" || text == "static";
    }

    std::shared_ptr<Type> parse_base_type() {
        while (is_type(peek().text) &&
               (peek().text == "const" || peek().text == "static" ||
                peek().text == "unsigned" || peek().text == "signed")) {
            ++position_;
        }
        if (accept("void")) return make_type(TypeKind::Void);
        if (accept("char")) return make_type(TypeKind::Char);
        if (accept("int")) return make_type(TypeKind::Int);
        if (accept("short")) {
            accept("int");
            return make_type(TypeKind::Int);
        }
        if (accept("long")) {
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
        auto type = parse_type();
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
            global.init = parse_expression();
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
        if (is_type(peek().text)) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind = Stmt::Kind::Decl;
            stmt->line = peek().line;
            auto type = parse_type();
            for (;;) {
                Decl decl;
                decl.name = parse_name();
                decl.type = type;
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
                    decl.init = parse_expression();
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
                        decl.init = parse_expression();
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

    std::unique_ptr<Expr> parse_expression() { return parse_assignment(); }

    std::unique_ptr<Expr> make_binary(std::string op, std::unique_ptr<Expr> left,
                                      std::unique_ptr<Expr> right, int line) {
        auto expr = std::make_unique<Expr>();
        expr->kind = Expr::Kind::Binary;
        expr->op = std::move(op);
        expr->line = line;
        expr->children.push_back(std::move(left));
        expr->children.push_back(std::move(right));
        return expr;
    }

    std::unique_ptr<Expr> parse_assignment() {
        auto left = parse_conditional();
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
            auto expr = std::make_unique<Expr>();
            expr->kind = Expr::Kind::Unary;
            expr->op = op;
            expr->line = line;
            expr->children.push_back(parse_unary());
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
};

class Codegen {
public:
    Codegen(Program program, std::string source_name, bool debug)
        : program_(std::move(program)),
          source_name_(std::move(source_name)),
          debug_(debug) {}

    std::string run() {
        collect_globals();
        std::ostringstream out;
        out << "; generated by kr16 cc from " << source_name_ << "\n";
        if (debug_) {
            out << ".file 0 \"" << source_name_ << "\"\n";
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
        return out.str();
    }

private:
    struct Variable {
        std::shared_ptr<Type> type;
        int offset = 0;
        bool global = false;
        std::string name;
    };

    void collect_globals() {
        for (auto& global : program_.globals) {
            global_map_[global.name] = &global;
        }
        for (auto& function : program_.functions) {
            function_map_[function.name] = &function;
        }
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

    void compile_function(Function& function, std::ostringstream& out) {
        locals_.clear();
        frame_size_ = 0;
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
            frame += align(declaration->type->size(), 2);
            Variable variable;
            variable.type = declaration->type;
            variable.offset = frame;
            variable.name = declaration->name;
            locals_[declaration->name] = variable;
        }
        frame_size_ = align(frame, 2);

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
        if (expr->kind == Expr::Kind::String) {
            return make_type(TypeKind::Pointer, make_type(TypeKind::Char));
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
                if (stmt->expr) {
                    gen_expr(stmt->expr.get(), out);
                } else {
                    out << "  MOV R0, #0\n";
                }
                out << "  BRA " << return_label_ << "\n";
                return;
            case Stmt::Kind::Compound:
                for (const auto& child : stmt->statements) {
                    gen_stmt(child.get(), out);
                }
                return;
            case Stmt::Kind::Decl:
                for (const auto& decl : stmt->declarations) {
                    if (decl.init) {
                        const auto it = locals_.find(decl.name);
                        if (it == locals_.end()) {
                            continue;
                        }
                        gen_expr(decl.init.get(), out);
                        out << "  LEA R1, [R4-" << it->second.offset << "]\n";
                        out << (decl.type->is_char() ? "  STB [R1], R0\n"
                                                    : "  ST [R1], R0\n");
                    }
                }
                return;
            case Stmt::Kind::If: {
                const auto else_label = new_label("else");
                const auto end_label = new_label("endif");
                gen_expr(stmt->expr.get(), out);
                out << "  CMP R0, #0\n";
                out << "  BEQ " << (stmt->else_body ? else_label : end_label)
                    << "\n";
                gen_stmt(stmt->body.get(), out);
                if (stmt->else_body) {
                    out << "  BRA " << end_label << "\n";
                    out << else_label << ":\n";
                    gen_stmt(stmt->else_body.get(), out);
                }
                out << end_label << ":\n";
                return;
            }
            case Stmt::Kind::While: {
                const auto start = new_label("while");
                const auto end = new_label("endwhile");
                break_labels_.push_back(end);
                continue_labels_.push_back(start);
                out << start << ":\n";
                gen_expr(stmt->expr.get(), out);
                out << "  CMP R0, #0\n";
                out << "  BEQ " << end << "\n";
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
                gen_expr(stmt->expr.get(), out);
                out << "  CMP R0, #0\n";
                out << "  BNE " << start << "\n";
                out << end << ":\n";
                break_labels_.pop_back();
                continue_labels_.pop_back();
                return;
            }
            case Stmt::Kind::For: {
                const auto start = new_label("for");
                const auto next = new_label("fornext");
                const auto end = new_label("endfor");
                if (stmt->init) {
                    gen_stmt(stmt->init.get(), out);
                }
                break_labels_.push_back(end);
                continue_labels_.push_back(next);
                out << start << ":\n";
                if (stmt->expr) {
                    gen_expr(stmt->expr.get(), out);
                    out << "  CMP R0, #0\n";
                    out << "  BEQ " << end << "\n";
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
        }
    }

    void gen_expr(const Expr* expr, std::ostringstream& out) {
        if (expr == nullptr) {
            out << "  MOV R0, #0\n";
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
            case Expr::Kind::Assign:
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
        }
    }

    void gen_load_identifier(const std::string& name, std::ostringstream& out) {
        const auto local = locals_.find(name);
        if (local != locals_.end()) {
            if (local->second.type->is_array()) {
                out << "  LEA R0, [R4-" << local->second.offset << "]\n";
            } else {
                out << (local->second.type->is_char()
                            ? "  LDB R0, [R4-"
                            : "  LD R0, [R4-")
                    << local->second.offset << "]\n";
            }
            return;
        }
        const auto global = global_map_.find(name);
        if (global != global_map_.end()) {
            if (global->second->type->is_array()) {
                out << "  MOV R0, #" << name << "\n";
            } else {
                out << (global->second->type->is_char() ? "  LDB R0, ["
                                                        : "  LD R0, [")
                    << name << "]\n";
            }
            return;
        }
        out << "  MOV R0, #0\n";
    }

    void gen_addr(const Expr* expr, std::ostringstream& out) {
        if (expr->kind == Expr::Kind::Identifier) {
            const auto local = locals_.find(expr->text);
            if (local != locals_.end()) {
                out << "  LEA R0, [R4-" << local->second.offset << "]\n";
                return;
            }
            out << "  MOV R0, #" << expr->text << "\n";
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
        if (expr->text == "__host_call" && expr->children.size() == 2 &&
            expr->children[0]->kind == Expr::Kind::Number &&
            expr->children[0]->value >= 0 && expr->children[0]->value <= 255) {
            gen_expr(expr->children[1].get(), out);
            out << "  SYSCALL #" << expr->children[0]->value << "\n";
            out << "  LD R0, [R0]\n";
            return;
        }
        external_calls_.insert(expr->text);
        for (auto it = expr->children.rbegin(); it != expr->children.rend(); ++it) {
            gen_expr(it->get(), out);
            out << "  PUSH R0\n";
        }
        const std::size_t count = expr->children.size();
        const std::size_t register_count = std::min<std::size_t>(3, count);
        for (std::size_t i = 0; i < register_count; ++i) {
            out << "  POP R" << (i + 1) << "\n";
        }
        out << "  CALL " << expr->text << "\n";
        if (count > 3) {
            out << "  ADD SP, #" << (count - 3) * 2 << "\n";
        }
    }

    Program program_;
    std::string source_name_;
    bool debug_ = false;
    std::unordered_map<std::string, Global*> global_map_;
    std::unordered_map<std::string, Function*> function_map_;
    std::unordered_map<std::string, Variable> locals_;
    std::map<std::string, std::string> string_literals_;
    std::set<std::string> external_calls_;
    std::vector<std::string> break_labels_;
    std::vector<std::string> continue_labels_;
    std::string return_label_;
    int frame_size_ = 0;
    int label_counter_ = 0;
};

}  // namespace

CompileResult compile_c(const std::string& source, const CompileOptions& options) {
    CompileResult result;
    try {
        Lexer lexer(source);
        auto tokens = lexer.run();
        Parser parser(std::move(tokens), options.source_name);
        auto program = parser.parse();
        Codegen codegen(std::move(program), options.source_name, options.debug);
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
