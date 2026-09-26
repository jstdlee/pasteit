#include "pipeline/pipeline.hpp"

#include "pipeline/process_runner.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>

namespace pastit {
namespace {

using Lines = std::vector<std::string>;

struct StageError {
    std::string message;
};

Lines split_lines(std::string_view text) {
    Lines lines;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.emplace_back(line);
        start = end + 1;
    }
    return lines;
}

std::string join_lines(const Lines& lines) {
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

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return std::string{text.substr(first, text.find_last_not_of(" \t\r") - first + 1)};
}

// Parsed short options: flags plus values for options that take one.
struct Options {
    std::set<char> flags;
    std::map<char, std::string> values;
    std::vector<std::string> operands;

    bool has(char flag) const { return flags.contains(flag); }
    std::string value(char option, std::string fallback = {}) const {
        const auto found = values.find(option);
        return found == values.end() ? fallback : found->second;
    }
};

Options parse_options(const std::vector<std::string>& argv, std::string_view with_value) {
    Options options;
    bool operands_only = false;
    for (std::size_t index = 1; index < argv.size(); ++index) {
        const auto& argument = argv[index];
        if (operands_only || argument.size() < 2 || argument[0] != '-' ||
            (std::isdigit(static_cast<unsigned char>(argument[1])) && with_value.find('n') != std::string_view::npos &&
             argv[0] != "cut")) {
            // "-5" for head/tail means -n 5.
            if (!operands_only && argument.size() >= 2 && argument[0] == '-' && std::isdigit(static_cast<unsigned char>(argument[1]))) {
                options.values['n'] = argument.substr(1);
                continue;
            }
            options.operands.push_back(argument);
            continue;
        }
        if (argument == "--") {
            operands_only = true;
            continue;
        }
        for (std::size_t position = 1; position < argument.size(); ++position) {
            const char flag = argument[position];
            if (with_value.find(flag) != std::string_view::npos) {
                if (position + 1 < argument.size()) {
                    options.values[flag] = argument.substr(position + 1);
                } else if (index + 1 < argv.size()) {
                    options.values[flag] = argv[++index];
                } else {
                    throw StageError{argv[0] + ": option -" + std::string(1, flag) + " needs a value"};
                }
                break;
            }
            options.flags.insert(flag);
        }
    }
    return options;
}

long long to_number(const std::string& text, const std::string& what) {
    long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) throw StageError{what + ": not a number: " + text};
    return value;
}

std::string unescape(std::string_view text) {
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\\' && index + 1 < text.size()) {
            const char next = text[++index];
            out += next == 'n' ? '\n' : next == 't' ? '\t' : next == 'r' ? '\r' : next;
        } else {
            out += text[index];
        }
    }
    return out;
}

std::vector<std::string> fields_of(const std::string& line, const std::string& separator) {
    std::vector<std::string> fields;
    if (separator.empty()) {
        std::istringstream words(line);
        for (std::string word; words >> word;) fields.push_back(word);
        return fields;
    }
    std::size_t start = 0;
    while (true) {
        const auto end = line.find(separator, start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + separator.size();
    }
    return fields;
}

// "1,3-5" -> {1,3,4,5}; open ranges like "2-" run to a large bound.
std::vector<std::size_t> parse_list(const std::string& list, const std::string& command) {
    std::vector<std::size_t> out;
    std::istringstream parts(list);
    for (std::string part; std::getline(parts, part, ',');) {
        const auto dash = part.find('-');
        if (dash == std::string::npos) {
            out.push_back(static_cast<std::size_t>(to_number(part, command)));
            continue;
        }
        const auto from = dash == 0 ? 1 : to_number(part.substr(0, dash), command);
        const auto to = dash + 1 == part.size() ? 4096 : to_number(part.substr(dash + 1), command);
        for (auto value = from; value <= to; ++value) out.push_back(static_cast<std::size_t>(value));
    }
    if (out.empty() || std::find(out.begin(), out.end(), 0) != out.end()) throw StageError{command + ": fields are numbered from 1"};
    return out;
}

double leading_number(std::string_view text) {
    const auto value = trim(text);
    double number = 0.0;
    std::from_chars(value.data(), value.data() + value.size(), number);
    return number;
}

std::string expand_tr_set(std::string_view set) {
    const auto text = unescape(set);
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index + 2 < text.size() && text[index + 1] == '-') {
            for (int ch = static_cast<unsigned char>(text[index]); ch <= static_cast<unsigned char>(text[index + 2]); ++ch) {
                out += static_cast<char>(ch);
            }
            index += 2;
        } else if (text.compare(index, 9, "[:upper:]") == 0) {
            for (char ch = 'A'; ch <= 'Z'; ++ch) out += ch;
            index += 8;
        } else if (text.compare(index, 9, "[:lower:]") == 0) {
            for (char ch = 'a'; ch <= 'z'; ++ch) out += ch;
            index += 8;
        } else if (text.compare(index, 9, "[:digit:]") == 0) {
            for (char ch = '0'; ch <= '9'; ++ch) out += ch;
            index += 8;
        } else if (text.compare(index, 9, "[:space:]") == 0) {
            out += " \t\n\r";
            index += 8;
        } else {
            out += text[index];
        }
    }
    return out;
}

std::string reverse_utf8(std::string_view line) {
    std::vector<std::string_view> characters;
    for (std::size_t index = 0; index < line.size();) {
        std::size_t width = 1;
        const auto byte = static_cast<unsigned char>(line[index]);
        if (byte >= 0xF0) width = 4;
        else if (byte >= 0xE0) width = 3;
        else if (byte >= 0xC0) width = 2;
        characters.push_back(line.substr(index, std::min(width, line.size() - index)));
        index += width;
    }
    std::string out;
    for (auto it = characters.rbegin(); it != characters.rend(); ++it) out += *it;
    return out;
}

std::size_t display_width(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char ch) {
        return (static_cast<unsigned char>(ch) & 0xC0U) != 0x80U;
    }));
}

std::string run_builtin(const std::vector<std::string>& argv, const std::string& input, const PipelineOptions& options) {
    const auto& name = argv.front();
    auto lines = split_lines(input);
    if (name == "sort") {
        const auto opts = parse_options(argv, "kt");
        const bool numeric = opts.has('n') || opts.has('g') || opts.has('h');
        const bool fold = opts.has('f');
        const auto separator = opts.value('t');
        const std::size_t key = opts.values.contains('k') ? static_cast<std::size_t>(to_number(opts.value('k').substr(0, opts.value('k').find_first_of(",.")), "sort -k")) : 0;
        const auto key_of = [&](const std::string& line) -> std::string {
            if (key == 0) return line;
            const auto fields = fields_of(line, separator);
            return key <= fields.size() ? fields[key - 1] : std::string{};
        };
        std::stable_sort(lines.begin(), lines.end(), [&](const std::string& left, const std::string& right) {
            const auto a = key_of(left);
            const auto b = key_of(right);
            if (numeric) return leading_number(a) < leading_number(b);
            return fold ? lower(a) < lower(b) : a < b;
        });
        if (opts.has('r')) std::reverse(lines.begin(), lines.end());
        if (opts.has('u')) {
            lines.erase(std::unique(lines.begin(), lines.end(), [&](const std::string& left, const std::string& right) {
                const auto a = key_of(left), b = key_of(right);
                return numeric ? leading_number(a) == leading_number(b) : fold ? lower(a) == lower(b) : a == b;
            }), lines.end());
        }
        return join_lines(lines);
    }
    if (name == "uniq") {
        const auto opts = parse_options(argv, "");
        const bool fold = opts.has('i');
        Lines out;
        for (std::size_t index = 0; index < lines.size();) {
            std::size_t end = index + 1;
            while (end < lines.size() && (fold ? lower(lines[end]) == lower(lines[index]) : lines[end] == lines[index])) ++end;
            const auto count = end - index;
            const bool keep = (!opts.has('d') || count > 1) && (!opts.has('u') || count == 1);
            if (keep) {
                if (opts.has('c')) {
                    char prefix[32];
                    std::snprintf(prefix, sizeof(prefix), "%7zu ", count);
                    out.push_back(prefix + lines[index]);
                } else {
                    out.push_back(lines[index]);
                }
            }
            index = end;
        }
        return join_lines(out);
    }
    if (name == "wc") {
        const auto opts = parse_options(argv, "");
        std::size_t words = 0;
        for (const auto& line : lines) {
            std::istringstream stream(line);
            for (std::string word; stream >> word;) ++words;
        }
        const std::size_t line_count = lines.size();
        std::vector<std::string> parts;
        const bool any = opts.has('l') || opts.has('w') || opts.has('c') || opts.has('m');
        if (!any || opts.has('l')) parts.push_back(std::to_string(line_count));
        if (!any || opts.has('w')) parts.push_back(std::to_string(words));
        if (opts.has('m')) parts.push_back(std::to_string(display_width(input)));
        if (!any || opts.has('c')) parts.push_back(std::to_string(input.size()));
        std::string out;
        for (std::size_t index = 0; index < parts.size(); ++index) out += (index ? " " : "") + parts[index];
        return out;
    }
    if (name == "head" || name == "tail") {
        const auto opts = parse_options(argv, "n");
        const auto count = static_cast<std::size_t>(std::max<long long>(0, to_number(opts.value('n', "10"), name)));
        if (lines.size() > count) {
            if (name == "head") lines.resize(count);
            else lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(count));
        }
        return join_lines(lines);
    }
    if (name == "grep") {
        const auto opts = parse_options(argv, "e");
        std::string pattern = opts.value('e');
        if (pattern.empty()) {
            if (opts.operands.empty()) throw StageError{"grep: missing pattern"};
            pattern = opts.operands.front();
        }
        auto flags = std::regex_constants::ECMAScript;
        if (opts.has('i')) flags |= std::regex_constants::icase;
        std::regex regex;
        try {
            regex = opts.has('F') ? std::regex(std::regex_replace(pattern, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)"), flags)
                                  : std::regex(pattern, flags);
        } catch (const std::regex_error&) {
            throw StageError{"grep: invalid pattern: " + pattern};
        }
        Lines out;
        std::size_t matched = 0;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const bool found = std::regex_search(lines[index], regex);
            if (found == opts.has('v')) continue;
            ++matched;
            if (opts.has('c')) continue;
            if (opts.has('o') && !opts.has('v')) {
                for (std::sregex_iterator it(lines[index].begin(), lines[index].end(), regex), end; it != end; ++it) {
                    out.push_back((opts.has('n') ? std::to_string(index + 1) + ":" : "") + it->str());
                }
            } else {
                out.push_back((opts.has('n') ? std::to_string(index + 1) + ":" : "") + lines[index]);
            }
        }
        return opts.has('c') ? std::to_string(matched) : join_lines(out);
    }
    if (name == "cut") {
        const auto opts = parse_options(argv, "dfc");
        Lines out;
        if (opts.values.contains('c')) {
            const auto positions = parse_list(opts.value('c'), "cut");
            for (const auto& line : lines) {
                std::string selected;
                std::size_t character = 0;
                for (std::size_t index = 0; index < line.size();) {
                    std::size_t width = 1;
                    const auto byte = static_cast<unsigned char>(line[index]);
                    if (byte >= 0xF0) width = 4; else if (byte >= 0xE0) width = 3; else if (byte >= 0xC0) width = 2;
                    ++character;
                    if (std::find(positions.begin(), positions.end(), character) != positions.end()) selected += line.substr(index, width);
                    index += width;
                }
                out.push_back(selected);
            }
            return join_lines(out);
        }
        if (!opts.values.contains('f')) throw StageError{"cut: use -f LIST with -d DELIM, or -c LIST"};
        const auto delimiter = unescape(opts.value('d', "\t"));
        const auto wanted = parse_list(opts.value('f'), "cut");
        for (const auto& line : lines) {
            const auto fields = fields_of(line, delimiter);
            std::string selected;
            bool first = true;
            for (const auto field : wanted) {
                if (field > fields.size()) continue;
                if (!first) selected += delimiter;
                selected += fields[field - 1];
                first = false;
            }
            out.push_back(selected);
        }
        return join_lines(out);
    }
    if (name == "tr") {
        const auto opts = parse_options(argv, "");
        std::string out;
        if (opts.has('d')) {
            if (opts.operands.empty()) throw StageError{"tr: -d needs a set"};
            const auto set = expand_tr_set(opts.operands[0]);
            for (const char ch : input) if (set.find(ch) == std::string::npos) out += ch;
        } else {
            if (opts.operands.size() < 2) throw StageError{"tr: needs SET1 and SET2"};
            const auto from = expand_tr_set(opts.operands[0]);
            const auto to = expand_tr_set(opts.operands[1]);
            if (to.empty()) throw StageError{"tr: SET2 is empty"};
            for (const char ch : input) {
                const auto at = from.find(ch);
                out += at == std::string::npos ? ch : to[std::min(at, to.size() - 1)];
            }
        }
        if (opts.has('s')) {
            out.erase(std::unique(out.begin(), out.end(), [](char left, char right) { return left == right && std::isspace(static_cast<unsigned char>(left)); }),
                      out.end());
        }
        return out;
    }
    if (name == "sed") {
        const auto opts = parse_options(argv, "e");
        std::vector<std::string> scripts;
        if (opts.values.contains('e')) scripts.push_back(opts.value('e'));
        for (const auto& operand : opts.operands) scripts.push_back(operand);
        if (scripts.empty()) throw StageError{"sed: missing script, e.g. 's/old/new/g'"};
        std::string text = input;
        for (const auto& script : scripts) {
            if (script.size() < 4 || script[0] != 's') throw StageError{"sed: only s/REGEX/REPLACEMENT/FLAGS is supported"};
            const char delimiter = script[1];
            std::vector<std::string> parts{""};
            for (std::size_t index = 2; index < script.size(); ++index) {
                if (script[index] == '\\' && index + 1 < script.size() && script[index + 1] == delimiter) {
                    parts.back() += delimiter;
                    ++index;
                } else if (script[index] == delimiter) {
                    parts.emplace_back();
                } else {
                    parts.back() += script[index];
                }
            }
            if (parts.size() < 2) throw StageError{"sed: incomplete s command"};
            const std::string flags = parts.size() > 2 ? parts[2] : "";
            auto regex_flags = std::regex_constants::ECMAScript;
            if (flags.find('i') != std::string::npos || flags.find('I') != std::string::npos) regex_flags |= std::regex_constants::icase;
            std::regex regex;
            try {
                regex = std::regex(parts[0], regex_flags);
            } catch (const std::regex_error&) {
                throw StageError{"sed: invalid pattern: " + parts[0]};
            }
            // sed's \1 and & become ECMAScript's $1 and $&.
            std::string replacement;
            for (std::size_t index = 0; index < parts[1].size(); ++index) {
                const char ch = parts[1][index];
                if (ch == '\\' && index + 1 < parts[1].size() && std::isdigit(static_cast<unsigned char>(parts[1][index + 1]))) {
                    replacement += '$';
                    replacement += parts[1][++index];
                } else if (ch == '&') {
                    replacement += "$&";
                } else if (ch == '$') {
                    replacement += "$$";
                } else {
                    replacement += ch;
                }
            }
            const auto mode = flags.find('g') != std::string::npos ? std::regex_constants::format_default
                                                                   : std::regex_constants::format_first_only;
            Lines out;
            for (const auto& line : split_lines(text)) out.push_back(std::regex_replace(line, regex, replacement, mode));
            text = join_lines(out);
        }
        return text;
    }
    if (name == "nl") {
        Lines out;
        std::size_t number = 0;
        for (const auto& line : lines) {
            if (trim(line).empty()) {
                out.push_back(line);
                continue;
            }
            char prefix[32];
            std::snprintf(prefix, sizeof(prefix), "%6zu\t", ++number);
            out.push_back(prefix + line);
        }
        return join_lines(out);
    }
    if (name == "rev") {
        for (auto& line : lines) line = reverse_utf8(line);
        return join_lines(lines);
    }
    if (name == "tac") {
        std::reverse(lines.begin(), lines.end());
        return join_lines(lines);
    }
    if (name == "column") {
        const auto opts = parse_options(argv, "s");
        const auto separator = unescape(opts.value('s'));
        std::vector<std::vector<std::string>> rows;
        std::vector<std::size_t> widths;
        for (const auto& line : lines) {
            rows.push_back(fields_of(line, separator));
            for (std::size_t index = 0; index < rows.back().size(); ++index) {
                if (widths.size() <= index) widths.push_back(0);
                widths[index] = std::max(widths[index], display_width(rows.back()[index]));
            }
        }
        Lines out;
        for (const auto& row : rows) {
            std::string line;
            for (std::size_t index = 0; index < row.size(); ++index) {
                line += row[index];
                if (index + 1 < row.size()) line += std::string(widths[index] - display_width(row[index]) + 2, ' ');
            }
            out.push_back(line);
        }
        return join_lines(out);
    }
    if (name == "trim") {
        for (auto& line : lines) line = trim(line);
        return join_lines(lines);
    }
    if (name == "upper" || name == "lower") {
        std::string out = input;
        for (auto& ch : out) ch = static_cast<char>(name == "upper" ? std::toupper(static_cast<unsigned char>(ch)) : std::tolower(static_cast<unsigned char>(ch)));
        return out;
    }
    if (name == "join") {
        const auto separator = argv.size() > 1 ? unescape(argv[1]) : std::string{", "};
        std::string out;
        for (std::size_t index = 0; index < lines.size(); ++index) out += (index ? separator : "") + lines[index];
        return out;
    }
    if (name == "anonymize") {
        if (!options.anonymize) throw StageError{"anonymize: not available"};
        return options.anonymize(input);
    }
    throw StageError{name + ": unknown command"};
}

}  // namespace

const std::vector<std::string>& builtin_pipeline_commands() {
    static const std::vector<std::string> commands{"sort", "uniq", "wc", "head", "tail", "grep", "cut", "tr", "sed",
                                                   "nl", "rev", "tac", "column", "trim", "upper", "lower", "join",
                                                   "anonymize"};
    return commands;
}

std::string pipeline_command_help(std::string_view name) {
    static const std::map<std::string, std::string, std::less<>> help{
        {"sort", "sort [-n] [-r] [-u] [-f] [-k N] [-t SEP]  sort lines"},
        {"uniq", "uniq [-c] [-d] [-u] [-i]  collapse adjacent duplicates"},
        {"wc", "wc [-l] [-w] [-c] [-m]  count lines, words, bytes, characters"},
        {"head", "head [-n N]  first N lines"},
        {"tail", "tail [-n N]  last N lines"},
        {"grep", "grep [-i] [-v] [-o] [-c] [-n] [-F] PATTERN  filter lines by regex"},
        {"cut", "cut -d SEP -f LIST | -c LIST  select fields or characters"},
        {"tr", "tr SET1 SET2 | tr -d SET [-s]  translate or delete characters"},
        {"sed", "sed 's/REGEX/REPLACEMENT/g'  substitute text"},
        {"nl", "nl  number non-empty lines"},
        {"rev", "rev  reverse each line"},
        {"tac", "tac  reverse line order"},
        {"column", "column [-s SEP]  align into columns"},
        {"trim", "trim  strip spaces around each line"},
        {"upper", "upper  UPPER CASE"},
        {"lower", "lower  lower case"},
        {"join", "join [SEP]  join lines with SEP (default \", \")"},
        {"anonymize", "anonymize  replace personal data with placeholders"},
    };
    const auto found = help.find(name);
    return found == help.end() ? std::string{name} + "  external tool (must be allowed in Settings)" : found->second;
}

PipelineParse parse_pipeline(std::string_view command) {
    PipelineParse parse;
    PipelineStage stage;
    std::string token;
    bool in_token = false;
    char quote = 0;
    const auto end_token = [&] {
        if (in_token) stage.argv.push_back(token);
        token.clear();
        in_token = false;
    };
    for (std::size_t index = 0; index < command.size(); ++index) {
        const char ch = command[index];
        if (quote != 0) {
            if (ch == quote) {
                quote = 0;
            } else if (quote == '"' && ch == '\\' && index + 1 < command.size() &&
                       (command[index + 1] == '"' || command[index + 1] == '\\')) {
                token += command[++index];
            } else {
                token += ch;
            }
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            in_token = true;
        } else if (ch == '\\' && index + 1 < command.size()) {
            token += command[++index];
            in_token = true;
        } else if (ch == '|') {
            end_token();
            if (stage.argv.empty()) {
                parse.error = "Empty stage before '|'";
                return parse;
            }
            parse.stages.push_back(std::move(stage));
            stage = {};
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            end_token();
        } else if (ch == ';' || ch == '&' || ch == '>' || ch == '<' || ch == '`' || (ch == '$' && index + 1 < command.size() && command[index + 1] == '(')) {
            parse.error = std::string("'") + ch + "' is not supported; pipelines only chain commands with '|'";
            return parse;
        } else {
            token += ch;
            in_token = true;
        }
    }
    if (quote != 0) {
        parse.error = "Unclosed quote";
        return parse;
    }
    end_token();
    if (!stage.argv.empty()) parse.stages.push_back(std::move(stage));
    else if (!parse.stages.empty()) parse.error = "Empty stage after '|'";
    if (parse.stages.empty() && parse.error.empty()) parse.error = "Enter a command, e.g. sort | uniq -c";
    return parse;
}

PipelineResult run_pipeline(std::string_view input, std::string_view command, const PipelineOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    PipelineResult result;
    const auto parse = parse_pipeline(command);
    if (!parse.error.empty()) {
        result.error = parse.error;
        return result;
    }
    const auto& builtins = builtin_pipeline_commands();
    std::string data{input};
    for (const auto& stage : parse.stages) {
        const auto& name = stage.argv.front();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        const auto remaining = options.timeout - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        if (remaining.count() <= 0) {
            result.error = "Timed out";
            return result;
        }
        if (std::find(builtins.begin(), builtins.end(), name) != builtins.end()) {
            try {
                data = run_builtin(stage.argv, data, options);
            } catch (const StageError& error) {
                result.error = error.message;
                return result;
            }
        } else {
            if (std::find(options.allowed_tools.begin(), options.allowed_tools.end(), name) == options.allowed_tools.end()) {
                result.error = name + ": not a built-in command; add it to the allowed tools in Settings to run it";
                return result;
            }
            if (!find_executable(name)) {
                result.error = name + ": not found on PATH";
                return result;
            }
            const auto run = run_process_with_input(stage.argv, data, remaining, options.max_output);
            if (run.timed_out) {
                result.error = name + ": timed out";
                return result;
            }
            if (!run.started || run.exit_code != 0) {
                result.error = name + ": " + (run.error_output.empty() ? "exited with code " + std::to_string(run.exit_code)
                                                                      : trim(run.error_output));
                return result;
            }
            result.truncated |= run.truncated;
            data = run.output;
            if (!data.empty() && data.back() == '\n') data.pop_back();
        }
        if (data.size() > options.max_output) {
            data.resize(options.max_output);
            result.truncated = true;
        }
    }
    result.ok = true;
    result.output = std::move(data);
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    return result;
}

}  // namespace pastit
