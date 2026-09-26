#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pastit {

// A small CommonMark-flavoured parser for previewing clipboard Markdown:
// headings (ATX and setext), paragraphs, fenced code, quotes, nested and
// task lists, pipe tables, rules, and inline bold/italic/strike/code/links.
enum MdStyle : unsigned {
    MdPlain = 0,
    MdBold = 1U << 0,
    MdItalic = 1U << 1,
    MdCode = 1U << 2,
    MdStrike = 1U << 3,
    MdLink = 1U << 4,
};

struct MdSpan {
    std::string text;
    unsigned style = MdPlain;
    std::string url;
};

enum class MdBlockType { Heading, Paragraph, Code, Quote, ListItem, Table, Rule };
enum class MdAlign { Left, Center, Right };

struct MdBlock {
    MdBlockType type = MdBlockType::Paragraph;
    int level = 0;  // heading level 1-6, or list nesting depth
    bool ordered = false;
    int number = 0;
    bool task = false;
    bool checked = false;
    std::string language;  // fenced code info string
    std::string code;
    std::vector<MdSpan> spans;
    std::vector<std::vector<std::vector<MdSpan>>> rows;  // table; row 0 is the header
    std::vector<MdAlign> align;
};

std::vector<MdSpan> parse_markdown_inline(std::string_view text);
std::vector<MdBlock> parse_markdown(std::string_view text);
std::string markdown_to_html(const std::vector<MdBlock>& blocks);
std::string markdown_to_plain_text(const std::vector<MdBlock>& blocks);

}  // namespace pastit
