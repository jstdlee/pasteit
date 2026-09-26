#pragma once

#include <array>
#include <cstdint>
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

// Byte encodings
std::string text_to_hex(std::string_view text);             // "48656c6c6f"
std::optional<std::string> hex_to_text(std::string_view text);  // readable UTF-8 only
std::string text_to_binary(std::string_view text);          // "01001000 01101001"
std::optional<std::string> binary_to_text(std::string_view text);

// Integers written as 255, 0xff, 0b1111_1111 or 0o377.
std::optional<unsigned long long> parse_integer_literal(std::string_view text);
std::string integer_bases(unsigned long long value);  // dec/hex/bin/oct lines
std::string to_hex_literal(unsigned long long value);
std::string to_binary_literal(unsigned long long value);

// IPv4 forms
std::optional<std::uint32_t> parse_ipv4(std::string_view text);
std::string format_ipv4(std::uint32_t address);
std::string ipv4_to_hex(std::uint32_t address);  // 0xC0A80101
// 0xC0A80101 or dotted hex c0.a8.01.01 (a bare 8-digit number is too ambiguous).
std::optional<std::uint32_t> parse_hex_ipv4(std::string_view text);

// Subnets: "10.0.0.0/24", "10.0.0.5 255.255.255.0", or a bare mask.
struct Ipv4Subnet {
    std::uint32_t address = 0;
    int prefix = 0;
};
std::optional<Ipv4Subnet> parse_ipv4_subnet(std::string_view text);
std::optional<int> netmask_to_prefix(std::uint32_t mask);  // contiguous masks only
std::uint32_t prefix_to_netmask(int prefix);
std::optional<int> parse_mask(std::string_view text);  // "/24", "255.255.255.0", or 0xffffff00
std::string subnet_details(const Ipv4Subnet& subnet);
std::string mask_details(int prefix);
// Lists the subnets of new_prefix inside subnet (at most `limit`).
std::string split_subnet(const Ipv4Subnet& subnet, int new_prefix, std::size_t limit = 256);

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
