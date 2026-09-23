#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pastit {

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
std::string json_quote(std::string_view input);

}  // namespace pastit
