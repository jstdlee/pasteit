#include "util/json.hpp"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <string_view>

namespace pastit {
namespace {

class JsonValidator {
public:
    explicit JsonValidator(std::string_view input) : input_(input) {}

    bool parse() {
        skip_ws();
        if (!parse_value()) {
            return false;
        }
        skip_ws();
        return pos_ == input_.size();
    }

private:
    bool parse_value() {
        skip_ws();
        if (pos_ >= input_.size()) {
            return false;
        }
        switch (input_[pos_]) {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"':
                return parse_string();
            case 't':
                return consume("true");
            case 'f':
                return consume("false");
            case 'n':
                return consume("null");
            default:
                return parse_number();
        }
    }

    bool parse_object() {
        ++pos_;
        skip_ws();
        if (consume_char('}')) {
            return true;
        }
        while (true) {
            skip_ws();
            if (!parse_string()) {
                return false;
            }
            skip_ws();
            if (!consume_char(':')) {
                return false;
            }
            if (!parse_value()) {
                return false;
            }
            skip_ws();
            if (consume_char('}')) {
                return true;
            }
            if (!consume_char(',')) {
                return false;
            }
        }
    }

    bool parse_array() {
        ++pos_;
        skip_ws();
        if (consume_char(']')) {
            return true;
        }
        while (true) {
            if (!parse_value()) {
                return false;
            }
            skip_ws();
            if (consume_char(']')) {
                return true;
            }
            if (!consume_char(',')) {
                return false;
            }
        }
    }

    bool parse_string() {
        if (!consume_char('"')) {
            return false;
        }
        while (pos_ < input_.size()) {
            const auto ch = input_[pos_++];
            if (ch == '"') {
                return true;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                return false;
            }
            if (ch == '\\') {
                if (pos_ >= input_.size()) {
                    return false;
                }
                const auto escaped = input_[pos_++];
                if (escaped == 'u') {
                    for (int count = 0; count < 4; ++count) {
                        if (pos_ >= input_.size() || !std::isxdigit(static_cast<unsigned char>(input_[pos_]))) {
                            return false;
                        }
                        ++pos_;
                    }
                } else if (std::string_view{"\"\\/bfnrt"}.find(escaped) == std::string_view::npos) {
                    return false;
                }
            }
        }
        return false;
    }

    bool parse_number() {
        const auto start = pos_;
        consume_char('-');
        if (consume_char('0')) {
        } else {
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                return false;
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }
        if (consume_char('.')) {
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                return false;
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                return false;
            }
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }
        return pos_ > start;
    }

    bool consume(std::string_view token) {
        if (input_.substr(pos_, token.size()) != token) {
            return false;
        }
        pos_ += token.size();
        return true;
    }

    bool consume_char(char ch) {
        if (pos_ < input_.size() && input_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    void skip_ws() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    std::string_view input_;
    std::size_t pos_ = 0;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}
    std::optional<JsonValue> parse() {
        skip_ws();
        auto value = parse_value();
        skip_ws();
        if (!value.has_value() || pos_ != input_.size()) return std::nullopt;
        return value;
    }

private:
    std::optional<JsonValue> parse_value() {
        skip_ws();
        if (pos_ >= input_.size()) return std::nullopt;
        if (input_[pos_] == '{') return parse_object();
        if (input_[pos_] == '[') return parse_array();
        if (input_[pos_] == '"') {
            auto text = parse_string();
            if (!text) return std::nullopt;
            return JsonValue{.value = std::move(*text)};
        }
        if (consume("true")) return JsonValue{.value = true};
        if (consume("false")) return JsonValue{.value = false};
        if (consume("null")) return JsonValue{};
        return parse_number();
    }

    std::optional<JsonValue> parse_object() {
        ++pos_;
        JsonValue::Object object;
        skip_ws();
        if (take('}')) return JsonValue{.value = std::move(object)};
        for (;;) {
            skip_ws();
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_ws();
            if (!take(':')) return std::nullopt;
            auto value = parse_value();
            if (!value) return std::nullopt;
            object.insert_or_assign(std::move(*key), std::move(*value));
            skip_ws();
            if (take('}')) return JsonValue{.value = std::move(object)};
            if (!take(',')) return std::nullopt;
        }
    }

    std::optional<JsonValue> parse_array() {
        ++pos_;
        JsonValue::Array array;
        skip_ws();
        if (take(']')) return JsonValue{.value = std::move(array)};
        for (;;) {
            auto value = parse_value();
            if (!value) return std::nullopt;
            array.push_back(std::move(*value));
            skip_ws();
            if (take(']')) return JsonValue{.value = std::move(array)};
            if (!take(',')) return std::nullopt;
        }
    }

    std::optional<std::string> parse_string() {
        if (!take('"')) return std::nullopt;
        std::string out;
        while (pos_ < input_.size()) {
            char ch = input_[pos_++];
            if (ch == '"') return out;
            if (static_cast<unsigned char>(ch) < 0x20) return std::nullopt;
            if (ch != '\\') { out.push_back(ch); continue; }
            if (pos_ >= input_.size()) return std::nullopt;
            const char escaped = input_[pos_++];
            switch (escaped) {
                case '"': case '\\': case '/': out.push_back(escaped); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    if (pos_ + 4 > input_.size()) return std::nullopt;
                    pos_ += 4;
                    out.push_back('?');
                    break;
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parse_number() {
        const auto start = pos_;
        if (pos_ < input_.size() && input_[pos_] == '-') ++pos_;
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        }
        if (start == pos_) return std::nullopt;
        std::string token{input_.substr(start, pos_ - start)};
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size()) return std::nullopt;
        return JsonValue{.value = value};
    }

    bool consume(std::string_view token) {
        if (input_.substr(pos_, token.size()) != token) return false;
        pos_ += token.size();
        return true;
    }
    bool take(char ch) {
        if (pos_ < input_.size() && input_[pos_] == ch) { ++pos_; return true; }
        return false;
    }
    void skip_ws() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }
    std::string_view input_;
    std::size_t pos_ = 0;
};

}  // namespace

bool is_valid_json(std::string_view input) {
    return JsonValidator{input}.parse();
}

std::optional<double> JsonValue::number() const {
    if (const auto* result = std::get_if<double>(&value)) return *result;
    return std::nullopt;
}

std::optional<bool> JsonValue::boolean() const {
    if (const auto* result = std::get_if<bool>(&value)) return *result;
    return std::nullopt;
}

const JsonValue* JsonValue::get(std::string_view key) const {
    const auto* values = object();
    if (values == nullptr) return nullptr;
    const auto found = values->find(std::string{key});
    return found == values->end() ? nullptr : &found->second;
}

std::optional<JsonValue> parse_json(std::string_view input) { return JsonParser{input}.parse(); }

std::string json_quote(std::string_view input) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : input) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    const char* digits = "0123456789abcdef";
                    out << "\\u00" << digits[(ch >> 4) & 0xF] << digits[ch & 0xF];
                } else out << static_cast<char>(ch);
        }
    }
    out << '"';
    return out.str();
}

std::optional<std::string> pretty_json(std::string_view input) {
    if (!is_valid_json(input)) {
        return std::nullopt;
    }

    std::string out;
    int indent = 0;
    bool in_string = false;
    bool escaped = false;
    auto newline = [&]() {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent) * 2, ' ');
    };

    for (const char ch : input) {
        if (in_string) {
            out.push_back(ch);
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }

        switch (ch) {
            case '"':
                in_string = true;
                out.push_back(ch);
                break;
            case '{':
            case '[':
                out.push_back(ch);
                ++indent;
                newline();
                break;
            case '}':
            case ']':
                --indent;
                newline();
                out.push_back(ch);
                break;
            case ',':
                out.push_back(ch);
                newline();
                break;
            case ':':
                out += ": ";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    out.push_back('\n');
    return out;
}

namespace {

class OrderedParser {
public:
    explicit OrderedParser(std::string_view input) : input_(input) {}

    std::optional<OrderedJson> parse() {
        auto value = parse_value(0);
        skip_space();
        if (!value || position_ != input_.size()) return std::nullopt;
        return value;
    }

private:
    static constexpr int kMaxDepth = 256;

    void skip_space() {
        while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\n' ||
                                             input_[position_] == '\r' || input_[position_] == '\t')) {
            ++position_;
        }
    }

    bool literal(std::string_view word) {
        if (input_.substr(position_, word.size()) != word) return false;
        position_ += word.size();
        return true;
    }

    static void append_utf8(std::string& out, unsigned int codepoint) {
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    std::optional<unsigned int> hex4() {
        if (position_ + 4 > input_.size()) return std::nullopt;
        unsigned int value = 0;
        for (int index = 0; index < 4; ++index) {
            const char ch = input_[position_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= static_cast<unsigned int>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= static_cast<unsigned int>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= static_cast<unsigned int>(ch - 'A' + 10);
            else return std::nullopt;
        }
        return value;
    }

    std::optional<std::string> parse_string() {
        if (position_ >= input_.size() || input_[position_] != '"') return std::nullopt;
        ++position_;
        std::string out;
        while (position_ < input_.size()) {
            const char ch = input_[position_++];
            if (ch == '"') return out;
            if (static_cast<unsigned char>(ch) < 0x20) return std::nullopt;
            if (ch != '\\') {
                out += ch;
                continue;
            }
            if (position_ >= input_.size()) return std::nullopt;
            const char escape = input_[position_++];
            switch (escape) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    auto codepoint = hex4();
                    if (!codepoint) return std::nullopt;
                    if (*codepoint >= 0xD800 && *codepoint <= 0xDBFF && input_.substr(position_, 2) == "\\u") {
                        position_ += 2;
                        const auto low = hex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF) return std::nullopt;
                        codepoint = 0x10000 + ((*codepoint - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    append_utf8(out, *codepoint);
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<OrderedJson> parse_value(int depth) {
        if (depth > kMaxDepth) return std::nullopt;
        skip_space();
        if (position_ >= input_.size()) return std::nullopt;
        OrderedJson value;
        const char ch = input_[position_];
        if (ch == '{') {
            ++position_;
            value.type = OrderedJson::Type::Object;
            skip_space();
            if (position_ < input_.size() && input_[position_] == '}') {
                ++position_;
                return value;
            }
            for (;;) {
                skip_space();
                auto key = parse_string();
                if (!key) return std::nullopt;
                skip_space();
                if (position_ >= input_.size() || input_[position_++] != ':') return std::nullopt;
                auto member = parse_value(depth + 1);
                if (!member) return std::nullopt;
                value.members.emplace_back(std::move(*key), std::move(*member));
                skip_space();
                if (position_ >= input_.size()) return std::nullopt;
                const char next = input_[position_++];
                if (next == '}') return value;
                if (next != ',') return std::nullopt;
            }
        }
        if (ch == '[') {
            ++position_;
            value.type = OrderedJson::Type::Array;
            skip_space();
            if (position_ < input_.size() && input_[position_] == ']') {
                ++position_;
                return value;
            }
            for (;;) {
                auto item = parse_value(depth + 1);
                if (!item) return std::nullopt;
                value.items.push_back(std::move(*item));
                skip_space();
                if (position_ >= input_.size()) return std::nullopt;
                const char next = input_[position_++];
                if (next == ']') return value;
                if (next != ',') return std::nullopt;
            }
        }
        if (ch == '"') {
            auto text = parse_string();
            if (!text) return std::nullopt;
            value.type = OrderedJson::Type::String;
            value.text = std::move(*text);
            return value;
        }
        if (literal("true")) { value.type = OrderedJson::Type::Bool; value.boolean = true; return value; }
        if (literal("false")) { value.type = OrderedJson::Type::Bool; return value; }
        if (literal("null")) return value;
        const auto start = position_;
        if (input_[position_] == '-') ++position_;
        const auto digits = [&] {
            const auto begin = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            return position_ > begin;
        };
        if (!digits()) return std::nullopt;
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            if (!digits()) return std::nullopt;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (!digits()) return std::nullopt;
        }
        value.type = OrderedJson::Type::Number;
        value.text = std::string{input_.substr(start, position_ - start)};
        return value;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

}  // namespace

const OrderedJson* OrderedJson::get(std::string_view key) const {
    for (const auto& [name, value] : members) {
        if (name == key) return &value;
    }
    return nullptr;
}

std::optional<OrderedJson> parse_ordered_json(std::string_view input) {
    return OrderedParser(input).parse();
}

std::string ordered_json_compact(const OrderedJson& value) {
    switch (value.type) {
        case OrderedJson::Type::Null: return "null";
        case OrderedJson::Type::Bool: return value.boolean ? "true" : "false";
        case OrderedJson::Type::Number: return value.text;
        case OrderedJson::Type::String: return json_quote(value.text);
        case OrderedJson::Type::Array: {
            std::string out = "[";
            for (std::size_t index = 0; index < value.items.size(); ++index) {
                if (index != 0) out += ',';
                out += ordered_json_compact(value.items[index]);
            }
            return out + "]";
        }
        case OrderedJson::Type::Object: {
            std::string out = "{";
            for (std::size_t index = 0; index < value.members.size(); ++index) {
                if (index != 0) out += ',';
                out += json_quote(value.members[index].first) + ":" + ordered_json_compact(value.members[index].second);
            }
            return out + "}";
        }
    }
    return "null";
}

}  // namespace pastit
