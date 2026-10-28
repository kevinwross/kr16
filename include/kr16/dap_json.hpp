#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kr16::dap {

// Thrown for malformed JSON or for type accessor mismatches. The offset is
// zero-based within the parsed text; accessor failures use offset 0.
class JsonError : public std::runtime_error {
public:
    JsonError(std::string message, std::size_t offset);

    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_ = 0;
};

// A small, dependency-free JSON value used by the DAP server. Object member
// order is preserved so serialized debugger messages are deterministic.
// Integers are stored separately from doubles so addresses, line numbers, and
// sequence numbers round-trip exactly.
class JsonValue {
public:
    enum class Type {
        Null,
        Boolean,
        Integer,
        Real,
        String,
        Array,
        Object,
    };

    using Array = std::vector<JsonValue>;
    using Member = std::pair<std::string, JsonValue>;
    using Object = std::vector<Member>;

    JsonValue() = default;

    static JsonValue null();
    static JsonValue boolean(bool value);
    static JsonValue integer(std::int64_t value);
    static JsonValue real(double value);
    static JsonValue string(std::string value);
    static JsonValue array(Array values = {});
    static JsonValue object(Object members = {});

    // Parses one complete JSON value. Throws JsonError on any syntax error,
    // trailing non-whitespace input, or excessive nesting.
    static JsonValue parse(std::string_view text);

    Type type() const noexcept { return type_; }
    bool is_null() const noexcept { return type_ == Type::Null; }
    bool is_boolean() const noexcept { return type_ == Type::Boolean; }
    bool is_integer() const noexcept { return type_ == Type::Integer; }
    bool is_real() const noexcept { return type_ == Type::Real; }
    bool is_number() const noexcept {
        return type_ == Type::Integer || type_ == Type::Real;
    }
    bool is_string() const noexcept { return type_ == Type::String; }
    bool is_array() const noexcept { return type_ == Type::Array; }
    bool is_object() const noexcept { return type_ == Type::Object; }

    bool as_boolean() const;
    std::int64_t as_integer() const;
    double as_real() const;
    const std::string& as_string() const;

    const Array& as_array() const;
    Array& as_array();
    const Object& as_object() const;
    Object& as_object();

    const JsonValue* find(std::string_view key) const noexcept;
    JsonValue* find(std::string_view key) noexcept;

    // Returns a null value if the key is absent.
    const JsonValue& at(std::string_view key) const;

    const JsonValue& at(std::size_t index) const;
    JsonValue& at(std::size_t index);

    std::size_t size() const noexcept;

    // Appends to an array or inserts/replaces an object member.
    void push(JsonValue value);
    void set(std::string key, JsonValue value);

    std::string dump() const;

private:
    void dump_to(std::string& out, std::size_t depth) const;

    Type type_ = Type::Null;
    bool boolean_ = false;
    std::int64_t integer_ = 0;
    double real_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

}  // namespace kr16::dap
