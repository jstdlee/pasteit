#include "transform/text_transforms.hpp"

#include "graph/graph_data.hpp"
#include "util/json.hpp"
#include "util/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>

namespace pastit {
namespace {

constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) out += '\n';
        out += lines[index];
    }
    return out;
}

std::string lower(std::string_view text) {
    std::string out{text};
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

std::string format_number(double value) {
    if (std::isfinite(value) && std::fabs(value) < 1e15 && value == std::floor(value)) {
        return std::to_string(static_cast<long long>(value));
    }
    std::ostringstream out;
    out << std::setprecision(12) << value;
    return out.str();
}

std::size_t codepoints(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char ch) {
        return (static_cast<unsigned char>(ch) & 0xC0U) != 0x80U;
    }));
}

bool readable_utf8(std::string_view text) {
    if (text.empty() || sanitize_utf8(text) != text) return false;
    std::size_t control = 0;
    for (const unsigned char ch : text) {
        if (ch < 0x20 && ch != '\n' && ch != '\r' && ch != '\t') ++control;
    }
    return control == 0;
}

std::string yaml_scalar(const std::string& value) {
    static const std::set<std::string> reserved{"true", "false", "null", "yes", "no", "on", "off", "~", ""};
    const bool plain = !reserved.contains(lower(value)) && value.find_first_of(":#{}[],&*!|>'\"%@`\n\t") == std::string::npos &&
                       value.front() != ' ' && value.back() != ' ' && value.front() != '-' && value.front() != '?' &&
                       !std::isdigit(static_cast<unsigned char>(value.front()));
    return plain ? value : json_quote(value);
}

void write_yaml(std::ostringstream& out, const JsonValue& value, int indent, bool inline_start);

std::string compact_json(const JsonValue& value) {
    if (value.object()) {
        std::string out = "{";
        bool first = true;
        for (const auto& [key, item] : *value.object()) {
            if (!first) out += ',';
            first = false;
            out += json_quote(key) + ":" + compact_json(item);
        }
        return out + "}";
    }
    if (value.array()) {
        std::string out = "[";
        for (std::size_t index = 0; index < value.array()->size(); ++index) {
            if (index != 0) out += ',';
            out += compact_json((*value.array())[index]);
        }
        return out + "]";
    }
    if (value.string()) return json_quote(*value.string());
    if (const auto number = value.number()) return format_number(*number);
    if (const auto boolean = value.boolean()) return *boolean ? "true" : "false";
    return "null";
}

std::string yaml_leaf(const JsonValue& value) {
    if (value.string()) return yaml_scalar(*value.string());
    if (value.object() && value.object()->empty()) return "{}";
    if (value.array() && value.array()->empty()) return "[]";
    return compact_json(value);
}

bool is_container(const JsonValue& value) {
    return (value.object() && !value.object()->empty()) || (value.array() && !value.array()->empty());
}

void write_yaml(std::ostringstream& out, const JsonValue& value, int indent, bool inline_start) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    if (value.object() && !value.object()->empty()) {
        bool first = true;
        for (const auto& [key, item] : *value.object()) {
            if (!(first && inline_start)) out << pad;
            first = false;
            out << yaml_scalar(key) << ':';
            if (is_container(item)) {
                out << '\n';
                write_yaml(out, item, indent + 2, false);
            } else {
                out << ' ' << yaml_leaf(item) << '\n';
            }
        }
        return;
    }
    if (value.array() && !value.array()->empty()) {
        for (const auto& item : *value.array()) {
            out << pad << "- ";
            if (is_container(item)) {
                write_yaml(out, item, indent + 2, true);
            } else {
                out << yaml_leaf(item) << '\n';
            }
        }
        return;
    }
    out << (inline_start ? "" : pad) << yaml_leaf(value) << '\n';
}

void collect_paths(const JsonValue& value, const std::string& prefix, std::vector<std::string>& out) {
    static const std::regex identifier(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    if (out.size() >= 500) return;
    if (value.object() && !value.object()->empty()) {
        for (const auto& [key, item] : *value.object()) {
            const auto segment = std::regex_match(key, identifier) ? "." + key : "[" + json_quote(key) + "]";
            collect_paths(item, prefix + segment, out);
        }
        return;
    }
    if (value.array() && !value.array()->empty()) {
        for (std::size_t index = 0; index < value.array()->size(); ++index) {
            collect_paths((*value.array())[index], prefix + "[" + std::to_string(index) + "]", out);
        }
        return;
    }
    out.push_back(prefix.empty() ? "." : prefix);
}

std::string csv_cell(std::string value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '"') out += '"';
        out += ch;
    }
    return out + "\"";
}

std::optional<std::string> base64url_decode(std::string_view text) {
    std::string normal{text};
    std::replace(normal.begin(), normal.end(), '-', '+');
    std::replace(normal.begin(), normal.end(), '_', '/');
    return base64_decode(normal);
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::vector<std::string> split_cells(std::string_view line, char delimiter) {
    std::vector<std::string> cells;
    std::string cell;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                cell += '"';
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (ch == delimiter && !quoted) {
            cells.push_back(trim(cell));
            cell.clear();
        } else {
            cell += ch;
        }
    }
    cells.push_back(trim(cell));
    if (delimiter == '|') {
        if (!cells.empty() && cells.front().empty()) cells.erase(cells.begin());
        if (!cells.empty() && cells.back().empty()) cells.pop_back();
    }
    return cells;
}

}  // namespace

std::string to_upper_ascii(std::string_view text) {
    std::string out{text};
    for (auto& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}

std::string to_lower_ascii(std::string_view text) {
    return lower(text);
}

std::string to_title_case(std::string_view text) {
    std::string out{text};
    bool word_start = true;
    for (auto& ch : out) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalpha(byte)) {
            ch = static_cast<char>(word_start ? std::toupper(byte) : std::tolower(byte));
            word_start = false;
        } else {
            word_start = byte < 0x80 && ch != '\'' && !std::isdigit(byte);
        }
    }
    return out;
}

std::string tidy_whitespace(std::string_view text) {
    std::vector<std::string> out;
    int blank_run = 0;
    for (const auto line : split_lines(text)) {
        auto value = std::string{line};
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        if (value.empty()) {
            if (out.empty() || ++blank_run > 1) continue;
        } else {
            blank_run = 0;
        }
        out.push_back(std::move(value));
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return join_lines(out);
}

std::string sort_lines(std::string_view text) {
    std::vector<std::string> lines;
    for (const auto line : split_lines(text)) lines.emplace_back(line);
    std::stable_sort(lines.begin(), lines.end(), [](const std::string& left, const std::string& right) {
        return lower(left) < lower(right);
    });
    return join_lines(lines);
}

std::string dedupe_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::set<std::string_view> seen;
    for (const auto line : split_lines(text)) {
        if (seen.insert(line).second) lines.emplace_back(line);
    }
    return join_lines(lines);
}

std::size_t line_count(std::string_view text) {
    return split_lines(text).size();
}

std::string text_statistics(std::string_view text) {
    std::size_t words = 0;
    bool in_word = false;
    for (const unsigned char ch : text) {
        const bool space = std::isspace(ch) != 0;
        if (!space && !in_word) ++words;
        in_word = !space;
    }
    std::ostringstream out;
    out << "Lines: " << line_count(text) << "\nWords: " << words << "\nCharacters: " << codepoints(text)
        << "\nBytes: " << text.size();
    return out.str();
}

std::string base64_encode(std::string_view bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        std::uint32_t chunk = static_cast<unsigned char>(bytes[index]) << 16;
        if (index + 1 < bytes.size()) chunk |= static_cast<unsigned char>(bytes[index + 1]) << 8;
        if (index + 2 < bytes.size()) chunk |= static_cast<unsigned char>(bytes[index + 2]);
        out += kBase64Alphabet[(chunk >> 18) & 0x3F];
        out += kBase64Alphabet[(chunk >> 12) & 0x3F];
        out += index + 1 < bytes.size() ? kBase64Alphabet[(chunk >> 6) & 0x3F] : '=';
        out += index + 2 < bytes.size() ? kBase64Alphabet[chunk & 0x3F] : '=';
    }
    return out;
}

std::optional<std::string> base64_decode(std::string_view text) {
    std::string clean;
    for (const char ch : text) {
        if (ch == '\n' || ch == '\r' || ch == ' ') continue;
        clean += ch;
    }
    while (!clean.empty() && clean.back() == '=') clean.pop_back();
    if (clean.empty() || clean.size() % 4 == 1) return std::nullopt;
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char ch : clean) {
        const auto position = kBase64Alphabet.find(ch);
        if (position == std::string_view::npos) return std::nullopt;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(position);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

bool looks_like_base64_text(std::string_view text) {
    const auto value = trim(text);
    if (value.size() < 16 || value.size() > 64 * 1024 || value.find_first_of(" \t\n") != std::string::npos) return false;
    // Ordinary identifiers and hex digests are valid base64 alphabets too;
    // require the mixed shape real encodings have.
    const bool has_digit = std::any_of(value.begin(), value.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
    const bool has_upper = std::any_of(value.begin(), value.end(), [](char ch) { return std::isupper(static_cast<unsigned char>(ch)); });
    const bool has_lower = std::any_of(value.begin(), value.end(), [](char ch) { return std::islower(static_cast<unsigned char>(ch)); });
    if (!(has_upper && has_lower) && !has_digit) return false;
    if (value.size() % 4 != 0 && value.find('=') == std::string::npos && value.size() % 4 == 1) return false;
    const auto decoded = base64_decode(value);
    return decoded && decoded->size() >= 4 && readable_utf8(*decoded);
}

std::string url_encode(std::string_view text) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

std::optional<std::string> url_decode(std::string_view text) {
    std::string out;
    bool changed = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2 < text.size()) {
            const int high = hex_value(text[index + 1]);
            const int low = hex_value(text[index + 2]);
            if (high >= 0 && low >= 0) {
                out += static_cast<char>(high * 16 + low);
                index += 2;
                changed = true;
                continue;
            }
        }
        out += text[index];
    }
    if (!changed || sanitize_utf8(out) != out) return std::nullopt;
    return out;
}

std::optional<std::string> minify_json(std::string_view text) {
    if (!is_valid_json(text)) return std::nullopt;
    std::string out;
    bool in_string = false;
    bool escaped = false;
    for (const char ch : text) {
        if (in_string) {
            out += ch;
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') in_string = false;
        } else if (ch == '"') {
            in_string = true;
            out += ch;
        } else if (!std::isspace(static_cast<unsigned char>(ch))) {
            out += ch;
        }
    }
    return out;
}

std::optional<std::string> json_to_yaml(std::string_view text) {
    const auto root = parse_json(text);
    if (!root) return std::nullopt;
    std::ostringstream out;
    write_yaml(out, *root, 0, false);
    auto yaml = out.str();
    if (!yaml.empty() && yaml.back() == '\n') yaml.pop_back();
    return yaml;
}

std::optional<std::string> json_to_csv(std::string_view text) {
    const auto root = parse_json(text);
    if (!root || !root->array() || root->array()->empty()) return std::nullopt;
    std::vector<std::string> columns;
    std::set<std::string> known;
    for (const auto& row : *root->array()) {
        if (!row.object()) return std::nullopt;
        for (const auto& [key, value] : *row.object()) {
            if (known.insert(key).second) columns.push_back(key);
        }
    }
    std::ostringstream out;
    for (std::size_t index = 0; index < columns.size(); ++index) out << (index ? "," : "") << csv_cell(columns[index]);
    for (const auto& row : *root->array()) {
        out << '\n';
        for (std::size_t index = 0; index < columns.size(); ++index) {
            if (index != 0) out << ',';
            const auto* value = row.get(columns[index]);
            if (value == nullptr || std::holds_alternative<std::nullptr_t>(value->value)) continue;
            out << csv_cell(value->string() ? *value->string() : compact_json(*value));
        }
    }
    return out.str();
}

std::optional<std::string> json_paths(std::string_view text) {
    const auto root = parse_json(text);
    if (!root || !is_container(*root)) return std::nullopt;
    std::vector<std::string> paths;
    collect_paths(*root, "", paths);
    return join_lines(paths);
}

std::optional<std::string> clean_tracking_url(std::string_view url) {
    const auto query_start = url.find('?');
    if (query_start == std::string_view::npos) return std::nullopt;
    const auto fragment_start = url.find('#', query_start);
    const auto query = url.substr(query_start + 1, fragment_start == std::string_view::npos
                                                       ? std::string_view::npos : fragment_start - query_start - 1);
    static const std::set<std::string> tracking{"fbclid", "gclid", "dclid", "gbraid", "wbraid", "msclkid", "yclid",
                                                "mc_cid", "mc_eid", "igshid", "_hsenc", "_hsmi", "ref_src", "spm",
                                                "si", "mkt_tok", "vero_id", "oly_enc_id", "oly_anon_id"};
    std::vector<std::string_view> kept;
    bool removed = false;
    std::size_t start = 0;
    while (start <= query.size()) {
        const auto end = query.find('&', start);
        const auto pair = query.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        const auto name = lower(pair.substr(0, pair.find('=')));
        if (name.starts_with("utm_") || tracking.contains(name)) removed = true;
        else if (!pair.empty()) kept.push_back(pair);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (!removed) return std::nullopt;
    std::string out{url.substr(0, query_start)};
    for (std::size_t index = 0; index < kept.size(); ++index) {
        out += index == 0 ? '?' : '&';
        out += kept[index];
    }
    if (fragment_start != std::string_view::npos) out += url.substr(fragment_start);
    return out;
}

std::string markdown_link(std::string_view url) {
    auto label = std::string{url};
    if (const auto scheme = label.find("://"); scheme != std::string::npos) label = label.substr(scheme + 3);
    if (const auto end = label.find_first_of("?#"); end != std::string::npos) label = label.substr(0, end);
    while (!label.empty() && label.back() == '/') label.pop_back();
    if (label.starts_with("www.")) label = label.substr(4);
    std::string escaped;
    for (const char ch : label) {
        if (ch == '[' || ch == ']') escaped += '\\';
        escaped += ch;
    }
    return "[" + escaped + "](" + std::string{url} + ")";
}

std::optional<Rgba> parse_color(std::string_view text) {
    const auto value = lower(trim(text));
    static const std::regex hex(R"(^#([0-9a-f]{3}|[0-9a-f]{4}|[0-9a-f]{6}|[0-9a-f]{8})$)");
    static const std::regex rgb(R"(^rgba?\(\s*(\d{1,3})\s*[, ]\s*(\d{1,3})\s*[, ]\s*(\d{1,3})\s*(?:[,/]\s*([0-9.]+%?)\s*)?\)$)");
    static const std::regex hsl(R"(^hsla?\(\s*([0-9.]+)(?:deg)?\s*[, ]\s*([0-9.]+)%\s*[, ]\s*([0-9.]+)%\s*(?:[,/]\s*([0-9.]+%?)\s*)?\)$)");
    std::smatch match;
    const auto alpha = [](const std::ssub_match& group) {
        if (!group.matched) return 1.0;
        auto text = group.str();
        const bool percent = !text.empty() && text.back() == '%';
        if (percent) text.pop_back();
        const double value = std::strtod(text.c_str(), nullptr);
        return std::clamp(percent ? value / 100.0 : value, 0.0, 1.0);
    };
    if (std::regex_match(value, match, hex)) {
        auto digits = match[1].str();
        if (digits.size() <= 4) {
            std::string expanded;
            for (const char ch : digits) expanded += std::string(2, ch);
            digits = expanded;
        }
        const auto byte = [&](std::size_t offset) { return hex_value(digits[offset]) * 16 + hex_value(digits[offset + 1]); };
        return Rgba{byte(0), byte(2), byte(4), digits.size() == 8 ? byte(6) / 255.0 : 1.0};
    }
    if (std::regex_match(value, match, rgb)) {
        const int r = std::stoi(match[1].str()), g = std::stoi(match[2].str()), b = std::stoi(match[3].str());
        if (r > 255 || g > 255 || b > 255) return std::nullopt;
        return Rgba{r, g, b, alpha(match[4])};
    }
    if (std::regex_match(value, match, hsl)) {
        const double h = std::fmod(std::strtod(match[1].str().c_str(), nullptr), 360.0) / 360.0;
        const double s = std::clamp(std::strtod(match[2].str().c_str(), nullptr) / 100.0, 0.0, 1.0);
        const double l = std::clamp(std::strtod(match[3].str().c_str(), nullptr) / 100.0, 0.0, 1.0);
        const auto channel = [&](double t) {
            const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
            const double p = 2 * l - q;
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            double v = p;
            if (t < 1.0 / 6) v = p + (q - p) * 6 * t;
            else if (t < 0.5) v = q;
            else if (t < 2.0 / 3) v = p + (q - p) * (2.0 / 3 - t) * 6;
            return static_cast<int>(std::lround(v * 255));
        };
        return Rgba{channel(h + 1.0 / 3), channel(h), channel(h - 1.0 / 3), alpha(match[4])};
    }
    return std::nullopt;
}

std::string color_hex(const Rgba& color) {
    std::ostringstream out;
    out << '#' << std::hex << std::setfill('0') << std::setw(2) << color.r << std::setw(2) << color.g << std::setw(2)
        << color.b;
    if (color.a < 1.0) out << std::setw(2) << static_cast<int>(std::lround(color.a * 255));
    return out.str();
}

std::string color_rgb(const Rgba& color) {
    std::ostringstream out;
    if (color.a < 1.0) {
        out << "rgba(" << color.r << ", " << color.g << ", " << color.b << ", " << std::setprecision(3) << color.a << ')';
    } else {
        out << "rgb(" << color.r << ", " << color.g << ", " << color.b << ')';
    }
    return out.str();
}

std::string color_hsl(const Rgba& color) {
    const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
    const double high = std::max({r, g, b}), low = std::min({r, g, b});
    const double l = (high + low) / 2;
    double h = 0, s = 0;
    if (high != low) {
        const double d = high - low;
        s = l > 0.5 ? d / (2 - high - low) : d / (high + low);
        if (high == r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (high == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6;
    }
    std::ostringstream out;
    out << (color.a < 1.0 ? "hsla(" : "hsl(") << std::lround(h * 360) << ", " << std::lround(s * 100) << "%, "
        << std::lround(l * 100) << '%';
    if (color.a < 1.0) out << ", " << std::setprecision(3) << color.a;
    out << ')';
    return out.str();
}

std::optional<std::string> decode_jwt(std::string_view text) {
    const auto value = trim(text);
    static const std::regex jwt(R"(^[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]*$)");
    if (!std::regex_match(value, jwt)) return std::nullopt;
    const auto first_dot = value.find('.');
    const auto second_dot = value.find('.', first_dot + 1);
    const auto header = base64url_decode(std::string_view{value}.substr(0, first_dot));
    const auto payload = base64url_decode(std::string_view{value}.substr(first_dot + 1, second_dot - first_dot - 1));
    if (!header || !payload || !is_valid_json(*header) || !is_valid_json(*payload)) return std::nullopt;
    const auto header_root = parse_json(*header);
    if (!header_root || !header_root->get("alg")) return std::nullopt;
    auto pretty = pretty_json("{\"header\":" + *header + ",\"payload\":" + *payload + "}");
    if (!pretty) return std::nullopt;
    if (const auto root = parse_json(*payload); root && root->get("exp") && root->get("exp")->number()) {
        const auto exp = static_cast<std::time_t>(*root->get("exp")->number());
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &exp);
#else
        gmtime_r(&exp, &utc);
#endif
        std::ostringstream note;
        note << std::put_time(&utc, "%Y-%m-%d %H:%M:%S UTC");
        *pretty += "\n\nexpires: " + note.str() + (exp < std::time(nullptr) ? " (expired)" : "");
    }
    *pretty += "\n\nsignature not verified";
    return pretty;
}

bool is_uuid(std::string_view text) {
    static const std::regex uuid(R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");
    return std::regex_match(trim(text), uuid);
}

std::string generate_uuid_v4() {
    std::random_device device;
    std::mt19937_64 engine(device());
    std::uniform_int_distribution<int> byte(0, 255);
    std::array<int, 16> bytes{};
    for (auto& value : bytes) value = byte(engine);
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) out << '-';
        out << std::setw(2) << bytes[index];
    }
    return out.str();
}

std::optional<std::string> to_markdown_table(std::string_view text) {
    const auto lines = split_lines(text);
    if (lines.size() < 2 || lines.size() > 500) return std::nullopt;
    for (const char delimiter : {'\t', ',', ';', '|'}) {
        std::vector<std::vector<std::string>> rows;
        bool consistent = true;
        for (const auto line : lines) {
            if (trim(line).empty()) continue;
            if (delimiter == '|' && trim(line).find_first_not_of("|-: ") == std::string::npos) {
                consistent = false;  // Already a Markdown table.
                break;
            }
            rows.push_back(split_cells(line, delimiter));
            if (rows.back().size() < 2 || rows.back().size() != rows.front().size()) {
                consistent = false;
                break;
            }
        }
        if (!consistent || rows.size() < 2) continue;
        std::ostringstream out;
        const auto write_row = [&](const std::vector<std::string>& row) {
            out << '|';
            for (const auto& cell : row) {
                std::string escaped;
                for (const char ch : cell) {
                    if (ch == '|') escaped += '\\';
                    escaped += ch;
                }
                out << ' ' << escaped << " |";
            }
        };
        write_row(rows.front());
        out << "\n|";
        for (std::size_t index = 0; index < rows.front().size(); ++index) out << " --- |";
        for (std::size_t index = 1; index < rows.size(); ++index) {
            out << '\n';
            write_row(rows[index]);
        }
        return out.str();
    }
    return std::nullopt;
}

std::optional<std::string> number_statistics(std::string_view text) {
    std::vector<double> values;
    if (const auto graph = parse_graph_data(text, false, false)) {
        for (const auto& point : graph->points) values.push_back(point.value);
    } else if (const auto with_header = parse_graph_data(text, true, false)) {
        for (const auto& point : with_header->points) values.push_back(point.value);
    }
    if (values.size() < 2) return std::nullopt;
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (const double value : values) sum += value;
    const double mean = sum / static_cast<double>(values.size());
    const auto middle = sorted.size() / 2;
    const double median = sorted.size() % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2.0;
    std::ostringstream out;
    out << "Count: " << values.size() << "\nSum: " << format_number(sum) << "\nMean: " << format_number(mean)
        << "\nMedian: " << format_number(median) << "\nMin: " << format_number(sorted.front())
        << "\nMax: " << format_number(sorted.back());
    return out.str();
}

std::string guess_code_extension(std::string_view text) {
    const std::string value{text.substr(0, std::min<std::size_t>(text.size(), 8192))};
    const auto has = [&](const char* pattern) {
        return std::regex_search(value, std::regex(pattern, std::regex::multiline));
    };
    if (has(R"(^#!.*\b(ba|z)?sh\b)")) return "sh";
    if (has(R"(^#!.*python)") || has(R"(^\s*def \w+\(.*\):\s*$)") || has(R"(^\s*(from \w+(\.\w+)* )?import \w+\s*$)")) return "py";
    if (has(R"(^\s*#include\s*[<"])")) return has(R"(\b(std::|class |template\s*<|namespace )\b)") ? "cpp" : "c";
    if (has(R"(^\s*package \w+\s*$)") && has(R"(\bfunc\b)")) return "go";
    if (has(R"(\bfn \w+\(|\blet mut\b|\bimpl\b)")) return "rs";
    if (has(R"(\bpublic (static )?(class|void)\b)")) return "java";
    if (has(R"(^\s*(SELECT|INSERT|UPDATE|DELETE|CREATE TABLE)\b)")) return "sql";
    if (has(R"(^\s*<(!DOCTYPE|html|div|body)\b)")) return "html";
    if (has(R"(:\s*(string|number|boolean)\b|\binterface \w+\s*\{)")) return "ts";
    if (has(R"(\b(const|let|function)\b.*(=>|\{))")) return "js";
    if (has(R"(^[.#]?[\w-]+\s*\{[^}]*:\s*[^}]*;)")) return "css";
    return "txt";
}

std::string contact_vcard(const std::vector<std::pair<std::string, std::string>>& fields) {
    const auto escape = [](const std::string& value) {
        std::string out;
        for (const char ch : value) {
            if (ch == ',' || ch == ';' || ch == '\\') out += '\\';
            if (ch == '\n') {
                out += "\\n";
                continue;
            }
            out += ch;
        }
        return out;
    };
    std::string name;
    for (const auto& [kind, value] : fields) {
        if (kind == "name") {
            name = value;
            break;
        }
    }
    std::ostringstream out;
    out << "BEGIN:VCARD\r\nVERSION:3.0\r\n";
    out << "FN:" << escape(name.empty() ? "Unknown" : name) << "\r\n";
    if (!name.empty()) {
        const auto space = name.rfind(' ');
        out << "N:" << escape(space == std::string::npos ? name : name.substr(space + 1)) << ';'
            << escape(space == std::string::npos ? std::string{} : name.substr(0, space)) << ";;;\r\n";
    }
    for (const auto& [kind, value] : fields) {
        if (kind == "email") out << "EMAIL;TYPE=INTERNET:" << escape(value) << "\r\n";
        else if (kind == "phone") out << "TEL:" << escape(value) << "\r\n";
        else if (kind == "organization") out << "ORG:" << escape(value) << "\r\n";
        else if (kind == "title") out << "TITLE:" << escape(value) << "\r\n";
        else if (kind == "address") out << "ADR:;;" << escape(value) << ";;;;\r\n";
    }
    out << "END:VCARD\r\n";
    return out.str();
}

}  // namespace pastit
