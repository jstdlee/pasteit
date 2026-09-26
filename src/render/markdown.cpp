#include "render/markdown.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace pasteit {
namespace {

std::string_view trim_view(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::size_t indent_of(std::string_view line) {
    std::size_t spaces = 0;
    for (const char ch : line) {
        if (ch == ' ') ++spaces;
        else if (ch == '\t') spaces += 4;
        else break;
    }
    return spaces;
}

void append(std::vector<MdSpan>& spans, std::string text, unsigned style, const std::string& url = {}) {
    if (text.empty()) return;
    if (!spans.empty() && spans.back().style == style && spans.back().url == url) {
        spans.back().text += text;
        return;
    }
    spans.push_back({std::move(text), style, url});
}

void parse_inline_into(std::string_view text, unsigned style, const std::string& url, std::vector<MdSpan>& out) {
    std::string plain;
    const auto flush = [&] {
        append(out, plain, style, url);
        plain.clear();
    };
    std::size_t index = 0;
    while (index < text.size()) {
        const char ch = text[index];
        if (ch == '\\' && index + 1 < text.size() && std::ispunct(static_cast<unsigned char>(text[index + 1]))) {
            plain += text[index + 1];
            index += 2;
            continue;
        }
        if (ch == '`') {
            std::size_t ticks = 0;
            while (index + ticks < text.size() && text[index + ticks] == '`') ++ticks;
            const auto close = text.find(std::string(ticks, '`'), index + ticks);
            if (close != std::string_view::npos) {
                flush();
                append(out, std::string{trim_view(text.substr(index + ticks, close - index - ticks))}, style | MdCode, url);
                index = close + ticks;
                continue;
            }
        }
        const bool image = ch == '!' && index + 1 < text.size() && text[index + 1] == '[';
        if (ch == '[' || image) {
            const auto open = index + (image ? 1 : 0);
            const auto close = text.find(']', open);
            if (close != std::string_view::npos && close + 1 < text.size() && text[close + 1] == '(') {
                const auto end = text.find(')', close + 2);
                if (end != std::string_view::npos) {
                    flush();
                    std::string target{trim_view(text.substr(close + 2, end - close - 2))};
                    if (const auto space = target.find(' '); space != std::string::npos) target.resize(space);
                    const auto label = text.substr(open + 1, close - open - 1);
                    if (image) {
                        append(out, "[image: " + std::string{label} + "]", style | MdLink, target);
                    } else {
                        parse_inline_into(label, style | MdLink, target, out);
                    }
                    index = end + 1;
                    continue;
                }
            }
        }
        if (ch == '<') {
            const auto end = text.find('>', index);
            if (end != std::string_view::npos) {
                const auto inside = text.substr(index + 1, end - index - 1);
                if (inside.starts_with("http://") || inside.starts_with("https://") || inside.starts_with("mailto:")) {
                    flush();
                    append(out, std::string{inside}, style | MdLink, std::string{inside});
                    index = end + 1;
                    continue;
                }
            }
        }
        if ((ch == '*' || ch == '_' || ch == '~') && index + 1 < text.size() && text[index + 1] == ch) {
            const std::string marker(2, ch);
            const auto close = text.find(marker, index + 2);
            if (close != std::string_view::npos && close > index + 2) {
                flush();
                const unsigned added = ch == '~' ? MdStrike : MdBold;
                parse_inline_into(text.substr(index + 2, close - index - 2), style | added, url, out);
                index = close + 2;
                continue;
            }
        }
        if ((ch == '*' || ch == '_') && index + 1 < text.size() && !std::isspace(static_cast<unsigned char>(text[index + 1]))) {
            // Intra-word underscores (snake_case) are not emphasis.
            const bool word_before = index > 0 && std::isalnum(static_cast<unsigned char>(text[index - 1]));
            if (!(ch == '_' && word_before)) {
                auto close = text.find(ch, index + 1);
                while (close != std::string_view::npos && close + 1 < text.size() && text[close + 1] == ch) {
                    close = text.find(ch, close + 2);
                }
                if (close != std::string_view::npos && !std::isspace(static_cast<unsigned char>(text[close - 1]))) {
                    flush();
                    parse_inline_into(text.substr(index + 1, close - index - 1), style | MdItalic, url, out);
                    index = close + 1;
                    continue;
                }
            }
        }
        plain += ch;
        ++index;
    }
    flush();
}

std::vector<std::string_view> table_cells(std::string_view line) {
    line = trim_view(line);
    if (line.starts_with('|')) line.remove_prefix(1);
    if (line.ends_with('|')) line.remove_suffix(1);
    std::vector<std::string_view> cells;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= line.size(); ++index) {
        if (index == line.size() || (line[index] == '|' && (index == 0 || line[index - 1] != '\\'))) {
            cells.push_back(trim_view(line.substr(start, index - start)));
            start = index + 1;
        }
    }
    return cells;
}

bool is_table_separator(std::string_view line) {
    static const std::regex separator(R"(^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$)");
    return line.find('-') != std::string_view::npos && std::regex_match(line.begin(), line.end(), separator);
}

std::string html_escape(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += ch;
        }
    }
    return out;
}

std::string spans_html(const std::vector<MdSpan>& spans) {
    std::string out;
    for (const auto& span : spans) {
        std::string text = html_escape(span.text);
        if (span.style & MdCode) text = "<code>" + text + "</code>";
        if (span.style & MdItalic) text = "<em>" + text + "</em>";
        if (span.style & MdBold) text = "<strong>" + text + "</strong>";
        if (span.style & MdStrike) text = "<del>" + text + "</del>";
        if (span.style & MdLink) text = "<a href=\"" + html_escape(span.url) + "\">" + text + "</a>";
        out += text;
    }
    return out;
}

std::string spans_plain(const std::vector<MdSpan>& spans) {
    std::string out;
    for (const auto& span : spans) out += span.text;
    return out;
}

}  // namespace

std::vector<MdSpan> parse_markdown_inline(std::string_view text) {
    std::vector<MdSpan> spans;
    parse_inline_into(text, MdPlain, {}, spans);
    return spans;
}

std::vector<MdBlock> parse_markdown(std::string_view text) {
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start <= text.size();) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
        if (end == text.size()) break;
    }
    static const std::regex heading(R"(^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$)");
    static const std::regex rule(R"(^\s{0,3}([-*_])(\s*\1){2,}\s*$)");
    static const std::regex list_item(R"(^(\s*)([-*+]|(\d{1,9})[.)])\s+(.*)$)");
    static const std::regex task(R"(^\[( |x|X)\]\s+(.*)$)");

    std::vector<MdBlock> blocks;
    std::string paragraph;
    const auto flush_paragraph = [&] {
        if (paragraph.empty()) return;
        blocks.push_back({.type = MdBlockType::Paragraph, .spans = parse_markdown_inline(paragraph)});
        paragraph.clear();
    };

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string line{lines[index]};
        const auto trimmed = trim_view(line);
        std::smatch match;
        if (trimmed.starts_with("```") || trimmed.starts_with("~~~")) {
            flush_paragraph();
            const auto fence = std::string(trimmed.substr(0, 3));
            MdBlock block{.type = MdBlockType::Code};
            block.language = std::string{trim_view(trimmed.substr(3))};
            for (++index; index < lines.size() && !trim_view(lines[index]).starts_with(fence); ++index) {
                if (!block.code.empty()) block.code += '\n';
                block.code += lines[index];
            }
            blocks.push_back(std::move(block));
            continue;
        }
        if (trimmed.empty()) {
            flush_paragraph();
            continue;
        }
        // Setext headings underline the paragraph line above.
        if (!paragraph.empty() && paragraph.find('\n') == std::string::npos &&
            (std::regex_match(line, std::regex(R"(^\s{0,3}=+\s*$)")) || std::regex_match(line, std::regex(R"(^\s{0,3}-+\s*$)")))) {
            blocks.push_back({.type = MdBlockType::Heading, .level = trimmed.front() == '=' ? 1 : 2,
                              .spans = parse_markdown_inline(paragraph)});
            paragraph.clear();
            continue;
        }
        if (std::regex_match(line, match, heading)) {
            flush_paragraph();
            blocks.push_back({.type = MdBlockType::Heading, .level = static_cast<int>(match[1].length()),
                              .spans = parse_markdown_inline(match[2].str())});
            continue;
        }
        if (std::regex_match(line, rule)) {
            flush_paragraph();
            blocks.push_back({.type = MdBlockType::Rule});
            continue;
        }
        if (trimmed.find('|') != std::string_view::npos && index + 1 < lines.size() && is_table_separator(lines[index + 1])) {
            flush_paragraph();
            MdBlock block{.type = MdBlockType::Table};
            for (const auto cell : table_cells(lines[index + 1])) {
                const bool left = cell.starts_with(':');
                const bool right = cell.ends_with(':');
                block.align.push_back(left && right ? MdAlign::Center : right ? MdAlign::Right : MdAlign::Left);
            }
            const auto add_row = [&](std::string_view row_line) {
                std::vector<std::vector<MdSpan>> row;
                for (const auto cell : table_cells(row_line)) row.push_back(parse_markdown_inline(cell));
                row.resize(block.align.size());
                block.rows.push_back(std::move(row));
            };
            add_row(lines[index]);
            for (index += 2; index < lines.size() && trim_view(lines[index]).find('|') != std::string_view::npos; ++index) {
                add_row(lines[index]);
            }
            --index;
            blocks.push_back(std::move(block));
            continue;
        }
        if (trimmed.starts_with('>')) {
            flush_paragraph();
            std::string quote;
            for (; index < lines.size() && trim_view(lines[index]).starts_with('>'); ++index) {
                auto inner = trim_view(lines[index]).substr(1);
                if (!inner.empty() && inner.front() == ' ') inner.remove_prefix(1);
                if (!quote.empty()) quote += ' ';
                quote += inner;
            }
            --index;
            blocks.push_back({.type = MdBlockType::Quote, .spans = parse_markdown_inline(quote)});
            continue;
        }
        if (std::regex_match(line, match, list_item)) {
            flush_paragraph();
            MdBlock block{.type = MdBlockType::ListItem};
            block.level = static_cast<int>(indent_of(match[1].str()) / 2);
            block.ordered = match[3].matched;
            block.number = block.ordered ? std::stoi(match[3].str()) : 0;
            std::string content = match[4].str();
            std::smatch task_match;
            if (std::regex_match(content, task_match, task)) {
                block.task = true;
                block.checked = task_match[1].str() != " ";
                content = task_match[2].str();
            }
            // Lazy continuation lines belong to the item.
            while (index + 1 < lines.size()) {
                const auto next = std::string{lines[index + 1]};
                const auto next_trimmed = trim_view(next);
                if (next_trimmed.empty() || std::regex_match(next, list_item) || next_trimmed.starts_with('#') ||
                    next_trimmed.starts_with('>') || next_trimmed.starts_with("```") || indent_of(next) < 2) {
                    break;
                }
                content += ' ';
                content += next_trimmed;
                ++index;
            }
            block.spans = parse_markdown_inline(content);
            blocks.push_back(std::move(block));
            continue;
        }
        if (!paragraph.empty()) paragraph += ' ';
        paragraph += trimmed;
    }
    flush_paragraph();
    return blocks;
}

std::string markdown_to_html(const std::vector<MdBlock>& blocks) {
    std::ostringstream out;
    std::vector<bool> open_lists;  // ordered flag per open nesting level
    const auto close_lists = [&](std::size_t depth) {
        while (open_lists.size() > depth) {
            out << (open_lists.back() ? "</ol>\n" : "</ul>\n");
            open_lists.pop_back();
        }
    };
    for (const auto& block : blocks) {
        if (block.type != MdBlockType::ListItem) close_lists(0);
        switch (block.type) {
            case MdBlockType::Heading:
                out << "<h" << block.level << '>' << spans_html(block.spans) << "</h" << block.level << ">\n";
                break;
            case MdBlockType::Paragraph:
                out << "<p>" << spans_html(block.spans) << "</p>\n";
                break;
            case MdBlockType::Code:
                out << "<pre><code" << (block.language.empty() ? "" : " class=\"language-" + html_escape(block.language) + "\"")
                    << '>' << html_escape(block.code) << "</code></pre>\n";
                break;
            case MdBlockType::Quote:
                out << "<blockquote><p>" << spans_html(block.spans) << "</p></blockquote>\n";
                break;
            case MdBlockType::Rule:
                out << "<hr>\n";
                break;
            case MdBlockType::ListItem: {
                const auto depth = static_cast<std::size_t>(block.level) + 1;
                close_lists(depth);
                if (open_lists.size() == depth && open_lists.back() != block.ordered) close_lists(depth - 1);
                while (open_lists.size() < depth) {
                    out << (block.ordered ? "<ol>\n" : "<ul>\n");
                    open_lists.push_back(block.ordered);
                }
                out << "<li>";
                if (block.task) out << (block.checked ? "<input type=\"checkbox\" checked disabled> " : "<input type=\"checkbox\" disabled> ");
                out << spans_html(block.spans) << "</li>\n";
                break;
            }
            case MdBlockType::Table: {
                out << "<table>\n";
                for (std::size_t row = 0; row < block.rows.size(); ++row) {
                    out << "<tr>";
                    for (std::size_t column = 0; column < block.rows[row].size(); ++column) {
                        const char* tag = row == 0 ? "th" : "td";
                        const auto align = column < block.align.size() ? block.align[column] : MdAlign::Left;
                        out << '<' << tag
                            << (align == MdAlign::Right ? " align=\"right\"" : align == MdAlign::Center ? " align=\"center\"" : "")
                            << '>' << spans_html(block.rows[row][column]) << "</" << tag << '>';
                    }
                    out << "</tr>\n";
                }
                out << "</table>\n";
                break;
            }
        }
    }
    close_lists(0);
    return out.str();
}

std::string markdown_to_plain_text(const std::vector<MdBlock>& blocks) {
    std::ostringstream out;
    bool first = true;
    for (const auto& block : blocks) {
        if (!first && block.type != MdBlockType::ListItem) out << '\n';
        first = false;
        switch (block.type) {
            case MdBlockType::Heading:
            case MdBlockType::Paragraph:
            case MdBlockType::Quote:
                out << spans_plain(block.spans) << '\n';
                break;
            case MdBlockType::Code:
                out << block.code << '\n';
                break;
            case MdBlockType::Rule:
                out << "----\n";
                break;
            case MdBlockType::ListItem:
                out << std::string(static_cast<std::size_t>(block.level) * 2, ' ')
                    << (block.ordered ? std::to_string(block.number) + ". " : "- ")
                    << (block.task ? (block.checked ? "[x] " : "[ ] ") : "") << spans_plain(block.spans) << '\n';
                break;
            case MdBlockType::Table:
                for (const auto& row : block.rows) {
                    for (std::size_t column = 0; column < row.size(); ++column) {
                        out << (column ? "\t" : "") << spans_plain(row[column]);
                    }
                    out << '\n';
                }
                break;
        }
    }
    auto text = out.str();
    while (!text.empty() && text.back() == '\n') text.pop_back();
    return text;
}

}  // namespace pasteit
