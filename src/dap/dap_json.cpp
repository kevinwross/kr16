#include "kr16/dap_json.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace kr16::dap {
namespace {

constexpr std::size_t kMaxDepth = 128;

bool is_json_whitespace(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

int hex_value(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

void append_escaped_string(std::string& out, std::string_view text) {
    constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const char character : text) {
        const auto value = static_cast<unsigned char>(character);
        switch (character) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (value < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[(value >> 4) & 0xF]);
                    out.push_back(kHex[value & 0xF]);
                } else {
                    out.push_back(character);
                }
                break;
        }
    }
    out.push_back('"');
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    JsonValue parse() {
        skip_whitespace();
        JsonValue value = parse_value(0);
        skip_whitespace();
        if (position_ != text_.size()) {
            fail("unexpected trailing characters");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        throw JsonError(message + " at offset " + std::to_string(position_),
                        position_);
    }

    void skip_whitespace() {
        while (position_ < text_.size() &&
               is_json_whitespace(text_[position_])) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    bool starts_with(std::string_view literal) const {
        return text_.substr(position_, literal.size()) == literal;
    }

    JsonValue parse_value(std::size_t depth) {
        if (depth > kMaxDepth) {
            fail("JSON nesting is too deep");
        }
        if (position_ >= text_.size()) {
            fail("unexpected end of input");
        }
        switch (text_[position_]) {
            case '{':
                return parse_object(depth);
            case '[':
                return parse_array(depth);
            case '"':
                return JsonValue::string(parse_string());
            case 't':
                if (!starts_with("true")) {
                    fail("invalid literal");
                }
                position_ += 4;
                return JsonValue::boolean(true);
            case 'f':
                if (!starts_with("false")) {
                    fail("invalid literal");
                }
                position_ += 5;
                return JsonValue::boolean(false);
            case 'n':
                if (!starts_with("null")) {
                    fail("invalid literal");
                }
                position_ += 4;
                return JsonValue::null();
            default:
                if (text_[position_] == '-' ||
                    (text_[position_] >= '0' && text_[position_] <= '9')) {
                    return parse_number();
                }
                fail("unexpected character");
        }
    }

    JsonValue parse_object(std::size_t depth) {
        expect('{');
        JsonValue result = JsonValue::object();
        skip_whitespace();
        if (consume('}')) {
            return result;
        }
        while (true) {
            skip_whitespace();
            if (position_ >= text_.size() || text_[position_] != '"') {
                fail("expected an object key");
            }
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            skip_whitespace();
            JsonValue value = parse_value(depth + 1);
            result.as_object().emplace_back(std::move(key), std::move(value));
            skip_whitespace();
            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                return result;
            }
            fail("expected ',' or '}'");
        }
    }

    JsonValue parse_array(std::size_t depth) {
        expect('[');
        JsonValue result = JsonValue::array();
        skip_whitespace();
        if (consume(']')) {
            return result;
        }
        while (true) {
            skip_whitespace();
            result.as_array().push_back(parse_value(depth + 1));
            skip_whitespace();
            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                return result;
            }
            fail("expected ',' or ']'");
        }
    }

    std::uint32_t parse_hex4() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (position_ >= text_.size()) {
                fail("truncated \\u escape");
            }
            const int digit = hex_value(text_[position_]);
            if (digit < 0) {
                fail("invalid \\u escape");
            }
            value = (value << 4) | static_cast<std::uint32_t>(digit);
            ++position_;
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"') {
                return result;
            }
            if (character == '\\') {
                if (position_ >= text_.size()) {
                    fail("truncated escape sequence");
                }
                const char escape = text_[position_++];
                switch (escape) {
                    case '"':
                        result.push_back('"');
                        break;
                    case '\\':
                        result.push_back('\\');
                        break;
                    case '/':
                        result.push_back('/');
                        break;
                    case 'b':
                        result.push_back('\b');
                        break;
                    case 'f':
                        result.push_back('\f');
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    case 'u': {
                        std::uint32_t code_point = parse_hex4();
                        if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                            if (position_ + 1 >= text_.size() ||
                                text_[position_] != '\\' ||
                                text_[position_ + 1] != 'u') {
                                fail("missing low surrogate");
                            }
                            position_ += 2;
                            const std::uint32_t low = parse_hex4();
                            if (low < 0xDC00 || low > 0xDFFF) {
                                fail("invalid low surrogate");
                            }
                            code_point = 0x10000 +
                                         ((code_point - 0xD800) << 10) +
                                         (low - 0xDC00);
                        } else if (code_point >= 0xDC00 &&
                                   code_point <= 0xDFFF) {
                            fail("unexpected low surrogate");
                        }
                        append_utf8(result, code_point);
                        break;
                    }
                    default:
                        fail("invalid escape sequence");
                }
                continue;
            }
            if (static_cast<unsigned char>(character) < 0x20) {
                fail("control character in string");
            }
            result.push_back(character);
        }
        fail("unterminated string");
    }

    JsonValue parse_number() {
        const std::size_t start = position_;
        if (consume('-')) {
            if (position_ >= text_.size()) {
                fail("truncated number");
            }
        }
        if (position_ >= text_.size() || text_[position_] < '0' ||
            text_[position_] > '9') {
            fail("expected a digit");
        }
        if (text_[position_] == '0') {
            ++position_;
            if (position_ < text_.size() && text_[position_] >= '0' &&
                text_[position_] <= '9') {
                fail("leading zeros are not allowed");
            }
        } else {
            while (position_ < text_.size() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }

        bool is_real = false;
        if (position_ < text_.size() && text_[position_] == '.') {
            is_real = true;
            ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' ||
                text_[position_] > '9') {
                fail("expected a fraction digit");
            }
            while (position_ < text_.size() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < text_.size() &&
            (text_[position_] == 'e' || text_[position_] == 'E')) {
            is_real = true;
            ++position_;
            if (position_ < text_.size() &&
                (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= text_.size() || text_[position_] < '0' ||
                text_[position_] > '9') {
                fail("expected an exponent digit");
            }
            while (position_ < text_.size() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }

        const std::string_view token = text_.substr(start, position_ - start);
        if (!is_real) {
            std::int64_t value = 0;
            const auto result = std::from_chars(
                token.data(), token.data() + token.size(), value);
            if (result.ec != std::errc{} ||
                result.ptr != token.data() + token.size()) {
                fail("integer is out of range");
            }
            return JsonValue::integer(value);
        }

        double value = 0.0;
        const auto result = std::from_chars(
            token.data(), token.data() + token.size(), value,
            std::chars_format::general);
        if (result.ec != std::errc{} ||
            result.ptr != token.data() + token.size()) {
            fail("invalid number");
        }
        return JsonValue::real(value);
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

}  // namespace

JsonError::JsonError(std::string message, std::size_t offset)
    : std::runtime_error(std::move(message)), offset_(offset) {}

JsonValue JsonValue::null() { return JsonValue{}; }

JsonValue JsonValue::boolean(bool value) {
    JsonValue result;
    result.type_ = Type::Boolean;
    result.boolean_ = value;
    return result;
}

JsonValue JsonValue::integer(std::int64_t value) {
    JsonValue result;
    result.type_ = Type::Integer;
    result.integer_ = value;
    return result;
}

JsonValue JsonValue::real(double value) {
    JsonValue result;
    result.type_ = Type::Real;
    result.real_ = value;
    return result;
}

JsonValue JsonValue::string(std::string value) {
    JsonValue result;
    result.type_ = Type::String;
    result.string_ = std::move(value);
    return result;
}

JsonValue JsonValue::array(Array values) {
    JsonValue result;
    result.type_ = Type::Array;
    result.array_ = std::move(values);
    return result;
}

JsonValue JsonValue::object(Object members) {
    JsonValue result;
    result.type_ = Type::Object;
    result.object_ = std::move(members);
    return result;
}

JsonValue JsonValue::parse(std::string_view text) { return Parser(text).parse(); }

bool JsonValue::as_boolean() const {
    if (type_ != Type::Boolean) {
        throw JsonError("expected a boolean", 0);
    }
    return boolean_;
}

std::int64_t JsonValue::as_integer() const {
    if (type_ != Type::Integer) {
        throw JsonError("expected an integer", 0);
    }
    return integer_;
}

double JsonValue::as_real() const {
    if (type_ == Type::Real) {
        return real_;
    }
    if (type_ == Type::Integer) {
        return static_cast<double>(integer_);
    }
    throw JsonError("expected a number", 0);
}

const std::string& JsonValue::as_string() const {
    if (type_ != Type::String) {
        throw JsonError("expected a string", 0);
    }
    return string_;
}

const JsonValue::Array& JsonValue::as_array() const {
    if (type_ != Type::Array) {
        throw JsonError("expected an array", 0);
    }
    return array_;
}

JsonValue::Array& JsonValue::as_array() {
    if (type_ != Type::Array) {
        throw JsonError("expected an array", 0);
    }
    return array_;
}

const JsonValue::Object& JsonValue::as_object() const {
    if (type_ != Type::Object) {
        throw JsonError("expected an object", 0);
    }
    return object_;
}

JsonValue::Object& JsonValue::as_object() {
    if (type_ != Type::Object) {
        throw JsonError("expected an object", 0);
    }
    return object_;
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

JsonValue* JsonValue::find(std::string_view key) noexcept {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (auto& member : object_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

const JsonValue& JsonValue::at(std::string_view key) const {
    static const JsonValue kNull;
    const JsonValue* value = find(key);
    return value == nullptr ? kNull : *value;
}

const JsonValue& JsonValue::at(std::size_t index) const {
    if (type_ != Type::Array) {
        throw JsonError("expected an array", 0);
    }
    if (index >= array_.size()) {
        throw JsonError("array index is out of range", 0);
    }
    return array_[index];
}

JsonValue& JsonValue::at(std::size_t index) {
    if (type_ != Type::Array) {
        throw JsonError("expected an array", 0);
    }
    if (index >= array_.size()) {
        throw JsonError("array index is out of range", 0);
    }
    return array_[index];
}

std::size_t JsonValue::size() const noexcept {
    if (type_ == Type::Array) {
        return array_.size();
    }
    if (type_ == Type::Object) {
        return object_.size();
    }
    return 0;
}

void JsonValue::push(JsonValue value) {
    if (type_ != Type::Array) {
        throw JsonError("expected an array", 0);
    }
    array_.push_back(std::move(value));
}

void JsonValue::set(std::string key, JsonValue value) {
    if (type_ != Type::Object) {
        throw JsonError("expected an object", 0);
    }
    for (auto& member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
}

std::string JsonValue::dump() const {
    std::string result;
    dump_to(result, 0);
    return result;
}

void JsonValue::dump_to(std::string& out, std::size_t depth) const {
    if (depth > kMaxDepth) {
        throw JsonError("JSON nesting is too deep", 0);
    }
    switch (type_) {
        case Type::Null:
            out += "null";
            return;
        case Type::Boolean:
            out += boolean_ ? "true" : "false";
            return;
        case Type::Integer:
            out += std::to_string(integer_);
            return;
        case Type::Real: {
            char buffer[64]{};
            const auto result = std::to_chars(
                buffer, buffer + sizeof(buffer), real_,
                std::chars_format::general,
                std::numeric_limits<double>::max_digits10);
            if (result.ec != std::errc{}) {
                throw JsonError("cannot serialize number", 0);
            }
            out.append(buffer, result.ptr);
            return;
        }
        case Type::String:
            append_escaped_string(out, string_);
            return;
        case Type::Array:
            out.push_back('[');
            for (std::size_t i = 0; i < array_.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                array_[i].dump_to(out, depth + 1);
            }
            out.push_back(']');
            return;
        case Type::Object:
            out.push_back('{');
            for (std::size_t i = 0; i < object_.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                append_escaped_string(out, object_[i].first);
                out.push_back(':');
                object_[i].second.dump_to(out, depth + 1);
            }
            out.push_back('}');
            return;
    }
}

}  // namespace kr16::dap
