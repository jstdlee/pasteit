#include "net/page_text.hpp"

#include "util/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace pasteit {
namespace {

std::string lower(std::string_view text) {
    std::string out{text};
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

void append_utf8(std::string& out, unsigned long codepoint) {
    if (codepoint < 0x80) out += static_cast<char>(codepoint);
    else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x110000) {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

std::string decode_entities(std::string_view text) {
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '&') {
            out += text[index];
            continue;
        }
        const auto end = text.find(';', index);
        if (end == std::string_view::npos || end - index > 10) {
            out += '&';
            continue;
        }
        const auto name = text.substr(index + 1, end - index - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos" || name == "#39") out += '\'';
        else if (name == "nbsp") out += ' ';
        else if (name == "mdash") out += "\xE2\x80\x94";
        else if (name == "ndash") out += "\xE2\x80\x93";
        else if (name == "hellip") out += "\xE2\x80\xA6";
        else if (name == "rsquo" || name == "lsquo") out += '\'';
        else if (name == "rdquo" || name == "ldquo") out += '"';
        else if (!name.empty() && name[0] == '#') {
            const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
            const std::string digits{name.substr(hex ? 2 : 1)};
            char* parse_end = nullptr;
            const auto codepoint = std::strtoul(digits.c_str(), &parse_end, hex ? 16 : 10);
            if (parse_end != nullptr && *parse_end == '\0' && !digits.empty()) append_utf8(out, codepoint);
            else out += std::string{text.substr(index, end - index + 1)};
        } else {
            out += std::string{text.substr(index, end - index + 1)};
        }
        index = end;
    }
    return out;
}

// Removes <tag ...>...</tag> blocks (case-insensitive) from html.
void drop_elements(std::string& html, std::string_view tag) {
    const auto open = "<" + std::string{tag};
    const auto close = "</" + std::string{tag};
    auto lowered = lower(html);
    std::size_t start = 0;
    while ((start = lowered.find(open, start)) != std::string::npos) {
        const auto after = start + open.size();
        if (after < lowered.size() && std::isalnum(static_cast<unsigned char>(lowered[after]))) {
            start = after;
            continue;
        }
        auto end = lowered.find(close, after);
        end = end == std::string::npos ? lowered.size() : lowered.find('>', end);
        end = end == std::string::npos ? lowered.size() : end + 1;
        html.erase(start, end - start);
        lowered.erase(start, end - start);
    }
}

std::string inner_element(const std::string& html, std::string_view tag) {
    const auto lowered = lower(html);
    const auto open = lowered.find("<" + std::string{tag});
    if (open == std::string::npos) return {};
    const auto content = lowered.find('>', open);
    const auto close = lowered.rfind("</" + std::string{tag});
    if (content == std::string::npos || close == std::string::npos || close <= content) return {};
    return html.substr(content + 1, close - content - 1);
}

std::string collapse(std::string_view text, std::size_t max_chars, bool& truncated) {
    std::string out;
    std::string line;
    const auto flush = [&] {
        const auto first = line.find_first_not_of(' ');
        if (first != std::string::npos) {
            const auto last = line.find_last_not_of(' ');
            if (!out.empty()) out += '\n';
            out += line.substr(first, last - first + 1);
        }
        line.clear();
    };
    bool space = false;
    for (const char ch : text) {
        if (ch == '\n') {
            flush();
            space = false;
        } else if (ch == ' ' || ch == '\t' || ch == '\r') {
            space = true;
        } else {
            if (space && !line.empty()) line += ' ';
            space = false;
            line += ch;
        }
        if (out.size() + line.size() > max_chars) {
            truncated = true;
            break;
        }
    }
    flush();
    if (out.size() > max_chars) {
        out = utf8_prefix_bytes(out, max_chars);
        truncated = true;
    }
    return out;
}

}  // namespace

PageText extract_page_text(std::string_view body, std::string_view content_type, std::size_t max_chars) {
    PageText page;
    const auto type = lower(content_type);
    const bool html = type.find("html") != std::string::npos ||
                      (type.empty() && lower(body.substr(0, 1024)).find("<html") != std::string::npos) ||
                      (type.empty() && body.find("</") != std::string_view::npos);
    if (!html) {
        page.text = collapse(sanitize_utf8(body), max_chars, page.truncated);
        return page;
    }
    std::string document = sanitize_utf8(body);
    static const std::regex title(R"(<title[^>]*>([\s\S]*?)</title>)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(document, match, title)) {
        bool ignored = false;
        page.title = collapse(decode_entities(match[1].str()), 300, ignored);
    }
    for (const auto tag : {"script", "style", "noscript", "svg", "template", "head", "nav", "footer", "aside", "form",
                           "iframe", "button"}) {
        drop_elements(document, tag);
    }
    for (const auto tag : {"article", "main"}) {
        auto inner = inner_element(document, tag);
        if (inner.size() > 400) {
            document = std::move(inner);
            break;
        }
    }
    static const std::regex comments(R"(<!--[\s\S]*?-->)");
    document = std::regex_replace(document, comments, " ");
    static const std::regex list_item(R"(<li[^>]*>)", std::regex::icase);
    document = std::regex_replace(document, list_item, "\n- ");
    static const std::regex heading(R"(<h([1-6])[^>]*>)", std::regex::icase);
    document = std::regex_replace(document, heading, "\n\n");
    static const std::regex block(R"(</?(p|div|br|tr|table|section|article|blockquote|pre|h[1-6]|ul|ol|dl|dt|dd|hr)\b[^>]*>)", std::regex::icase);
    document = std::regex_replace(document, block, "\n");
    static const std::regex cell(R"(</t[dh]>)", std::regex::icase);
    document = std::regex_replace(document, cell, "\t");
    static const std::regex tag(R"(<[^>]*>)");
    document = std::regex_replace(document, tag, "");
    page.text = collapse(decode_entities(document), max_chars, page.truncated);
    return page;
}

}  // namespace pasteit
