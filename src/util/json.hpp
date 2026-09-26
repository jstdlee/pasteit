#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace pasteit {

bool is_valid_json(std::string_view input);
std::optional<std::string> pretty_json(std::string_view input);

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value = nullptr;

    const Object* object() const { return std::get_if<Object>(&value); }
    const Array* array() const { return std::get_if<Array>(&value); }
    const std::string* string() const { return std::get_if<std::string>(&value); }
    std::optional<double> number() const;
    std::optional<bool> boolean() const;
    const JsonValue* get(std::string_view key) const;
};

std::optional<JsonValue> parse_json(std::string_view input);

// Parse tree that keeps object members in source order and numbers as
// written, for conversions whose output should mirror the input.
struct OrderedJson {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::string text;  // string value, or the number exactly as written
    std::vector<OrderedJson> items;
    std::vector<std::pair<std::string, OrderedJson>> members;

    const OrderedJson* get(std::string_view key) const;
};

std::optional<OrderedJson> parse_ordered_json(std::string_view input);
// Compact serialization preserving order and number text.
std::string ordered_json_compact(const OrderedJson& value);
std::string json_quote(std::string_view input);

}  // namespace pasteit
