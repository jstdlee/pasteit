#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

// Pure, local text transforms behind the utility actions. Each returns
// std::nullopt when the input is not applicable, so the catalog can use the
// same function to decide whether to offer the action at all.

// Text
std::string to_upper_ascii(std::string_view text);
std::string to_lower_ascii(std::string_view text);
std::string to_title_case(std::string_view text);
std::string tidy_whitespace(std::string_view text);
std::string sort_lines(std::string_view text);
std::string dedupe_lines(std::string_view text);
std::string text_statistics(std::string_view text);
std::size_t line_count(std::string_view text);

// Encoding
std::string base64_encode(std::string_view bytes);
std::optional<std::string> base64_decode(std::string_view text);
// True for a single token that decodes to readable UTF-8 text.
bool looks_like_base64_text(std::string_view text);
std::string url_encode(std::string_view text);
std::optional<std::string> url_decode(std::string_view text);  // nullopt when nothing is encoded

// JSON (input must be valid JSON)
std::optional<std::string> minify_json(std::string_view text);
std::optional<std::string> json_to_yaml(std::string_view text);
std::optional<std::string> json_to_csv(std::string_view text);  // array of objects only
std::optional<std::string> json_paths(std::string_view text);   // jq-style leaf paths

// URL
std::optional<std::string> clean_tracking_url(std::string_view url);  // nullopt when already clean
std::string markdown_link(std::string_view url);

// Color
struct Rgba {
    int r = 0;
    int g = 0;
    int b = 0;
    double a = 1.0;
};
std::optional<Rgba> parse_color(std::string_view text);
std::string color_hex(const Rgba& color);
std::string color_rgb(const Rgba& color);
std::string color_hsl(const Rgba& color);

// Tokens
std::optional<std::string> decode_jwt(std::string_view text);  // pretty JSON of header + payload
bool is_uuid(std::string_view text);
std::string generate_uuid_v4();

// Tables and numbers
std::optional<std::string> to_markdown_table(std::string_view text);
std::optional<std::string> number_statistics(std::string_view text);

// Code
std::string guess_code_extension(std::string_view text);

// Contact fields as (kind, value) pairs, e.g. {"email", "a@b.c"}.
std::string contact_vcard(const std::vector<std::pair<std::string, std::string>>& fields);

}  // namespace pastit
