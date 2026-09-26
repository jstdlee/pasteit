#include "detect/content_profile.hpp"

#include "transform/text_transforms.hpp"
#include "util/json.hpp"
#include "util/utf8.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>

namespace pasteit {
namespace {

constexpr std::size_t kMaxSampledLines = 64;
constexpr std::size_t kMiddleBytes = 2 * 1024;

std::string_view trim_view(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

// Cut to whole lines so a sample never starts or ends mid-row.
std::string whole_lines(std::string_view text, bool drop_first, bool drop_last) {
    if (drop_first) {
        const auto newline = text.find('\n');
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
    }
    if (drop_last) {
        const auto newline = text.rfind('\n');
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(0, newline);
    }
    return std::string{text};
}

std::vector<std::string_view> lines_of(std::string_view text, std::size_t limit) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size() && lines.size() < limit) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!trim_view(line).empty()) lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

bool is_number(std::string_view text) {
    text = trim_view(text);
    if (text.empty()) return false;
    std::size_t index = 0;
    if (text[0] == '-' || text[0] == '+') ++index;
    bool digit = false;
    bool dot = false;
    for (; index < text.size(); ++index) {
        const char ch = text[index];
        if (std::isdigit(static_cast<unsigned char>(ch))) digit = true;
        else if (ch == '.' && !dot) dot = true;
        else if (ch == ',' || ch == '_' || ch == '%') continue;
        else if ((ch == 'e' || ch == 'E') && digit && index + 1 < text.size()) continue;
        else return false;
    }
    return digit;
}

// Field count per line for a delimiter, ignoring delimiters inside quotes.
std::size_t field_count(std::string_view line, char delimiter) {
    std::size_t fields = 1;
    bool quoted = false;
    for (const char ch : line) {
        if (ch == '"') quoted = !quoted;
        else if (ch == delimiter && !quoted) ++fields;
    }
    if (delimiter == '|') {
        const auto trimmed = trim_view(line);
        if (trimmed.starts_with('|')) --fields;
        if (trimmed.size() > 1 && trimmed.ends_with('|')) --fields;
    }
    return fields;
}

std::vector<std::string> split_fields(std::string_view line, char delimiter) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (const char ch : line) {
        if (ch == '"') quoted = !quoted;
        else if (ch == delimiter && !quoted) {
            fields.emplace_back(trim_view(field));
            field.clear();
        } else {
            field += ch;
        }
    }
    fields.emplace_back(trim_view(field));
    return fields;
}

std::size_t word_count(std::string_view text) {
    std::size_t words = 0;
    bool in_word = false;
    for (const unsigned char ch : text) {
        const bool space = std::isspace(ch) != 0;
        if (!space && !in_word) ++words;
        in_word = !space;
    }
    return words;
}

struct TableGuess {
    double score = 0.0;
    char delimiter = 0;
    std::size_t columns = 0;
    std::size_t numeric_columns = 0;
    bool header = false;
};

TableGuess guess_table(const std::vector<std::string_view>& lines) {
    TableGuess best;
    if (lines.size() < 2) return best;
    for (const char delimiter : {'\t', ',', ';', '|'}) {
        std::map<std::size_t, std::size_t> counts;
        for (const auto line : lines) ++counts[field_count(line, delimiter)];
        const auto mode = std::max_element(counts.begin(), counts.end(), [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        const std::size_t columns = mode->first;
        if (columns < 2) continue;
        const double share = static_cast<double>(mode->second) / static_cast<double>(lines.size());
        const std::size_t minimum_rows = delimiter == ',' || delimiter == ';' ? 3 : 2;
        if (share < 0.8 || lines.size() < minimum_rows) continue;
        // Prose with commas has long, wordy fields; data fields are short.
        double words = 0.0;
        std::size_t samples = 0;
        std::vector<std::size_t> numeric(columns, 0);
        std::size_t rows = 0;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const auto fields = split_fields(lines[index], delimiter);
            if (fields.size() != columns) continue;
            ++rows;
            for (std::size_t column = 0; column < columns; ++column) {
                words += static_cast<double>(word_count(fields[column]));
                ++samples;
                if (index > 0 && is_number(fields[column])) ++numeric[column];
            }
        }
        const double words_per_field = samples == 0 ? 0.0 : words / static_cast<double>(samples);
        double score = 0.45 + 0.4 * share + (delimiter == '\t' ? 0.08 : 0.0);
        if (words_per_field > 4.0) score -= 0.35;
        else if (words_per_field > 2.5) score -= 0.15;
        if (lines.size() >= 5) score += 0.05;
        if (score <= best.score) continue;
        best = TableGuess{.score = std::min(score, 0.97), .delimiter = delimiter, .columns = columns};
        const auto first = split_fields(lines.front(), delimiter);
        std::size_t numeric_columns = 0;
        bool header_texty = true;
        for (std::size_t column = 0; column < columns; ++column) {
            if (rows > 1 && numeric[column] * 2 >= rows - 1) {
                ++numeric_columns;
                if (column < first.size() && is_number(first[column])) header_texty = false;
            }
            if (column < first.size() && first[column].empty()) header_texty = false;
        }
        best.numeric_columns = numeric_columns;
        best.header = header_texty && (numeric_columns > 0 || rows <= 50);
    }
    return best;
}

double share_matching(const std::vector<std::string_view>& lines, const std::regex& pattern) {
    if (lines.empty()) return 0.0;
    std::size_t matches = 0;
    for (const auto line : lines) {
        if (std::regex_search(line.begin(), line.end(), pattern)) ++matches;
    }
    return static_cast<double>(matches) / static_cast<double>(lines.size());
}

double markdown_score(std::string_view text, const std::vector<std::string_view>& lines, std::vector<std::string>& tags) {
    static const std::regex heading(R"(^#{1,6}\s+\S)");
    static const std::regex list(R"(^\s*([-*+]|\d+[.)])\s+\S)");
    static const std::regex quote(R"(^>\s?)");
    static const std::regex table_rule(R"(^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$)");
    static const std::regex inline_marks(R"((\[[^\]]+\]\([^)\s]+\)|\*\*[^*]+\*\*|`[^`]+`|!\[[^\]]*\]\())");
    int kinds = 0;
    double score = 0.0;
    const double headings = share_matching(lines, heading);
    const double lists = share_matching(lines, list);
    const double quotes = share_matching(lines, quote);
    const bool fence = text.find("\n```") != std::string_view::npos || text.starts_with("```");
    const bool table = share_matching(lines, table_rule) > 0.0;
    const bool inline_found = std::regex_search(text.begin(), text.end(), inline_marks);
    if (headings > 0.0) { ++kinds; score += 0.3 + std::min(0.2, headings); }
    if (lists > 0.0) { ++kinds; score += 0.15; }
    if (quotes > 0.0) { ++kinds; score += 0.1; }
    if (fence) { ++kinds; score += 0.25; tags.emplace_back("code_block"); }
    if (table) { ++kinds; score += 0.25; tags.emplace_back("md_table"); }
    if (inline_found) { ++kinds; score += 0.2; }
    if (kinds < 2) score *= 0.6;  // A single list or link is just text.
    return std::min(score, 0.95);
}

bool is_url_line(std::string_view line) {
    line = trim_view(line);
    return (line.starts_with("http://") || line.starts_with("https://")) && line.find(' ') == std::string_view::npos;
}

bool is_path_line(std::string_view line) {
    line = trim_view(line);
    if (line.starts_with("file://")) return true;
    if (line.size() > 1 && line[0] == '/' && line.find("  ") == std::string_view::npos) return true;
    if (line.size() > 2 && std::isalpha(static_cast<unsigned char>(line[0])) && line[1] == ':' &&
        (line[2] == '\\' || line[2] == '/')) {
        return true;
    }
    return line.starts_with("~/");
}

std::size_t count_cjk(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t index = 0; index + 2 < text.size(); ++index) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first >= 0xE4 && first <= 0xE9) ++count;
    }
    return count;
}

}  // namespace

bool ContentProfile::has_tag(std::string_view tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

ContentSample sample_text(std::string_view text) {
    ContentSample sample;
    sample.total_bytes = text.size();
    if (text.size() <= kProfileHeadBytes) {
        sample.head = std::string{text};
        sample.complete = true;
        return sample;
    }
    sample.head = whole_lines(text.substr(0, kProfileHeadBytes), false, true);
    const auto middle_start = text.size() / 2;
    sample.middle = whole_lines(text.substr(middle_start, kMiddleBytes), true, true);
    sample.tail = whole_lines(text.substr(text.size() - kProfileTailBytes), true, false);
    return sample;
}

ContentSample sample_file(const std::filesystem::path& path) {
    ContentSample sample;
    std::ifstream input(path, std::ios::binary);
    if (!input) return sample;
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::uint64_t>(std::max<std::streamoff>(0, input.tellg()));
    input.seekg(0);
    const auto read = [&](std::uint64_t offset, std::size_t bytes) {
        std::string buffer(bytes, '\0');
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset));
        input.read(buffer.data(), static_cast<std::streamsize>(bytes));
        buffer.resize(static_cast<std::size_t>(input.gcount()));
        return buffer;
    };
    if (size <= kProfileHeadBytes) {
        sample.head = read(0, static_cast<std::size_t>(size));
        sample.total_bytes = size;
        sample.complete = true;
        return sample;
    }
    sample.total_bytes = size;
    sample.head = whole_lines(read(0, kProfileHeadBytes), false, true);
    sample.middle = whole_lines(read(size / 2, kMiddleBytes), true, true);
    sample.tail = whole_lines(read(size - kProfileTailBytes, kProfileTailBytes), true, false);
    return sample;
}

ContentProfile profile_content(ContentKind kind, const ContentSample& sample) {
    ContentProfile profile;
    profile.total_bytes = sample.total_bytes;
    if (kind == ContentKind::Image) {
        profile.shape = DataShape::Binary;
        profile.confidence = 1.0;
        profile.tags.emplace_back("image");
        return profile;
    }
    const std::string_view head{sample.head};
    const auto trimmed = trim_view(head);
    if (trimmed.empty()) return profile;
    if (head.find('\0') != std::string_view::npos || sanitize_utf8(head) != head) {
        profile.shape = DataShape::Binary;
        profile.confidence = 0.9;
        return profile;
    }

    auto lines = lines_of(head, kMaxSampledLines);
    // Mix in middle/tail rows so a preamble or a trailing summary does not
    // decide the shape of the whole body.
    for (const auto extra : {std::string_view{sample.middle}, std::string_view{sample.tail}}) {
        for (const auto line : lines_of(extra, 8)) lines.push_back(line);
    }
    profile.sampled_lines = lines.size();
    const auto head_lines = static_cast<std::size_t>(std::count(head.begin(), head.end(), '\n')) + 1;
    profile.estimated_lines = sample.complete || head.empty()
        ? head_lines
        : static_cast<std::size_t>(static_cast<double>(head_lines) * static_cast<double>(sample.total_bytes) /
                                   static_cast<double>(head.size()));
    if (count_cjk(head) * 3 > head.size() / 10) profile.tags.emplace_back("cjk");

    std::vector<std::pair<DataShape, double>> scores;
    const auto score = [&](DataShape shape, double value) {
        if (value > 0.0) scores.emplace_back(shape, std::clamp(value, 0.0, 1.0));
    };

    // A single short token: exact validators decide what it is.
    const bool single_line = profile.estimated_lines <= 1 && trimmed.find('\n') == std::string_view::npos;
    if (single_line && trimmed.size() <= 512 && trimmed.find(' ') == std::string_view::npos) {
        score(DataShape::Token, 0.9);
        const std::string token{trimmed};
        if (parse_color(token)) profile.tags.emplace_back("color");
        if (is_uuid(token)) profile.tags.emplace_back("uuid");
        if (decode_jwt(token)) profile.tags.emplace_back("jwt");
        if (looks_like_base64_text(token)) profile.tags.emplace_back("base64");
        if (is_number(token)) profile.tags.emplace_back("number");
    }

    // JSON: validate fully when small; otherwise trust balanced head/tail.
    const char first = trimmed.front();
    if (first == '{' || first == '[') {
        if (sample.complete) {
            if (is_valid_json(trimmed)) {
                score(DataShape::Json, 1.0);
                profile.validated = true;
            }
        } else {
            const auto tail = trim_view(sample.tail);
            const char last = tail.empty() ? '\0' : tail.back();
            if ((first == '{' && last == '}') || (first == '[' && last == ']')) score(DataShape::Json, 0.8);
        }
        std::size_t object_lines = 0;
        std::size_t valid_lines = 0;
        for (const auto line : lines) {
            const auto value = trim_view(line);
            if (value.starts_with('{') && value.ends_with('}')) {
                ++object_lines;
                if (is_valid_json(value)) ++valid_lines;
            }
        }
        if (lines.size() >= 2 && valid_lines * 10 >= lines.size() * 9) score(DataShape::Ndjson, 0.95);
        else if (lines.size() >= 2 && object_lines * 10 >= lines.size() * 9) score(DataShape::Ndjson, 0.7);
    }

    if (first == '<') {
        static const std::regex html(R"(<(!doctype html|html|head|body|div|span|p|a|table|script)\b)", std::regex::icase);
        const bool is_html = std::regex_search(head.begin(), head.end(), html);
        score(is_html ? DataShape::Html : DataShape::Xml, trimmed.find("</") != std::string_view::npos ? 0.85 : 0.6);
    }

    static const std::regex sql(R"(^\s*(select|insert\s+into|update|delete\s+from|create\s+(table|index|view)|alter\s+table|with)\b)",
                                std::regex::icase | std::regex_constants::multiline);
    if (std::regex_search(head.begin(), head.end(), sql) &&
        std::regex_search(head.begin(), head.end(), std::regex(R"(\b(from|values|set|table|where|as)\b)", std::regex::icase))) {
        score(DataShape::Sql, 0.85);
    }

    const auto table = guess_table(lines);
    if (table.score > 0.0) {
        score(table.delimiter == '\t' ? DataShape::Tsv : DataShape::Csv,
              table.delimiter == '|' ? table.score - 0.1 : table.score);
    }

    std::vector<std::string> markdown_tags;
    score(DataShape::Markdown, markdown_score(head, lines, markdown_tags));

    static const std::regex log_line(
        R"(^\s*(\[?\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}|\[?\d{2}:\d{2}:\d{2}|[A-Z][a-z]{2}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2}|\[?(INFO|WARN|WARNING|ERROR|DEBUG|TRACE|FATAL)\b))",
        std::regex_constants::ECMAScript | std::regex_constants::icase);
    const double logs = share_matching(lines, log_line);
    if (lines.size() >= 2 && logs >= 0.6) score(DataShape::Log, 0.5 + 0.4 * logs);

    static const std::regex key_value(R"(^\s*[A-Za-z_][\w .-]{0,40}\s*[:=]\s*\S)");
    static const std::regex yaml_line(R"(^(\s*-\s+\S|\s*[\w-]+:\s*$|\s{2,}[\w-]+:\s))");
    const double pairs = share_matching(lines, key_value);
    const double yaml = share_matching(lines, yaml_line);
    if (lines.size() >= 2 && pairs >= 0.7) {
        const bool nested = yaml >= 0.2 || head.starts_with("---");
        score(nested ? DataShape::Yaml : DataShape::KeyValue, 0.5 + 0.35 * pairs);
    } else if (lines.size() >= 3 && pairs + yaml >= 0.8 && yaml >= 0.3) {
        score(DataShape::Yaml, 0.7);
    }

    const auto urls = static_cast<std::size_t>(std::count_if(lines.begin(), lines.end(), is_url_line));
    if (lines.size() >= 2 && urls * 10 >= lines.size() * 8) score(DataShape::UrlList, 0.9);
    const auto paths = static_cast<std::size_t>(std::count_if(lines.begin(), lines.end(), is_path_line));
    if (lines.size() >= 2 && paths * 10 >= lines.size() * 8) score(DataShape::PathList, 0.85);

    if (!single_line || trimmed.find(' ') != std::string_view::npos) {
        std::size_t numbers = 0;
        std::size_t tokens = 0;
        for (const auto line : lines) {
            for (const auto& field : split_fields(line, line.find(',') != std::string_view::npos ? ',' : ' ')) {
                if (field.empty()) continue;
                ++tokens;
                if (is_number(field)) ++numbers;
            }
        }
        if (tokens >= 2 && numbers * 10 >= tokens * 9) score(DataShape::NumberSeries, 0.85);
    }

    const std::string code_probe{head.substr(0, std::min<std::size_t>(head.size(), 8192))};
    static const std::regex strong_code(
        R"((^#!\S|#include\s*[<"]|^\s*(import|from)\s+[\w.]+(\s+import\b|\s*;?\s*$)|^\s*(def|class|function|fn|func)\s+\w+|\b(int|void|auto|let|const|var)\s+\w+\s*[(=]|=>|^\s*(public|private|protected|static)\s+\w+))",
        std::regex_constants::ECMAScript | std::regex_constants::multiline);
    const double braces = share_matching(lines, std::regex(R"(([{};]\s*$|^\s*[})\]]))"));
    if (std::regex_search(code_probe, strong_code)) score(DataShape::Code, 0.75 + std::min(0.2, braces));
    else if (lines.size() >= 3 && braces >= 0.4) score(DataShape::Code, 0.55 + 0.3 * braces);
    if (!scores.empty() && std::any_of(scores.begin(), scores.end(), [](const auto& entry) {
            return entry.first == DataShape::Code;
        })) {
        profile.language = guess_code_extension(code_probe);
    }

    // Prose is the fallback, stronger with sentence punctuation.
    std::size_t sentences = 0;
    for (const char ch : head) {
        if (ch == '.' || ch == '?' || ch == '!' || ch == '\xE3') ++sentences;
    }
    const double words = static_cast<double>(word_count(head)) / std::max<std::size_t>(1, lines.size());
    score(DataShape::Prose, 0.35 + (words >= 5.0 ? 0.15 : 0.0) + (sentences > 0 ? 0.1 : 0.0));

    std::stable_sort(scores.begin(), scores.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });
    if (scores.size() > 3) scores.resize(3);
    profile.candidates = scores;
    profile.shape = scores.front().first;
    profile.confidence = scores.front().second;
    // Two close candidates mean the guess is uncertain.
    if (scores.size() > 1 && scores[0].second - scores[1].second < 0.1) profile.confidence *= 0.85;

    if (profile.shape == DataShape::Csv || profile.shape == DataShape::Tsv) {
        profile.delimiter = table.delimiter;
        profile.columns = table.columns;
        profile.numeric_columns = table.numeric_columns;
        profile.header = table.header;
        profile.validated = sample.complete;
    }
    if (profile.shape == DataShape::Markdown) {
        profile.tags.insert(profile.tags.end(), markdown_tags.begin(), markdown_tags.end());
    }
    if (!sample.complete) profile.tags.emplace_back("sampled");
    return profile;
}

std::string data_shape_name(DataShape shape) {
    switch (shape) {
        case DataShape::Empty: return "empty";
        case DataShape::Token: return "token";
        case DataShape::Prose: return "prose";
        case DataShape::Json: return "json";
        case DataShape::Ndjson: return "ndjson";
        case DataShape::Csv: return "csv";
        case DataShape::Tsv: return "tsv";
        case DataShape::Markdown: return "markdown";
        case DataShape::Code: return "code";
        case DataShape::Log: return "log";
        case DataShape::KeyValue: return "key_value";
        case DataShape::Yaml: return "yaml";
        case DataShape::Html: return "html";
        case DataShape::Xml: return "xml";
        case DataShape::Sql: return "sql";
        case DataShape::UrlList: return "url_list";
        case DataShape::PathList: return "path_list";
        case DataShape::NumberSeries: return "numbers";
        case DataShape::Binary: return "binary";
    }
    return "unknown";
}

std::string data_shape_description(DataShape shape) {
    switch (shape) {
        case DataShape::Empty: return "empty clipboard";
        case DataShape::Token: return "a single short value such as a color, ID, token, or number";
        case DataShape::Prose: return "natural-language text";
        case DataShape::Json: return "a JSON document";
        case DataShape::Ndjson: return "newline-delimited JSON records";
        case DataShape::Csv: return "comma or semicolon separated table rows";
        case DataShape::Tsv: return "tab separated table rows";
        case DataShape::Markdown: return "Markdown formatted text";
        case DataShape::Code: return "program source code";
        case DataShape::Log: return "timestamped log lines";
        case DataShape::KeyValue: return "key: value or key=value settings";
        case DataShape::Yaml: return "a YAML document";
        case DataShape::Html: return "HTML markup";
        case DataShape::Xml: return "XML markup";
        case DataShape::Sql: return "SQL statements";
        case DataShape::UrlList: return "a list of URLs";
        case DataShape::PathList: return "a list of file paths";
        case DataShape::NumberSeries: return "a series of numbers";
        case DataShape::Binary: return "binary data";
    }
    return "unknown data";
}

}  // namespace pasteit
