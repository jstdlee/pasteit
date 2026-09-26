#include "detect/fast_content_detector.hpp"
#include "ai/mermaid_prompt.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <ctime>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>

namespace pastit {
namespace {

// Fast detectors inspect a bounded prefix so large clipboard text cannot make
// regex-based signal checks scale with unbounded input. QR keeps its separate
// <= 2048-byte payload rule below.
constexpr std::size_t kMaxDetectorBytes = 8192;

std::string trim(std::string_view input) {
    auto begin = input.begin();
    auto end = input.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

std::string lower(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char ch : input) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    std::istringstream input(std::string{text});
    while (std::getline(input, current)) {
        lines.push_back(trim(current));
    }
    return lines;
}

void add_field(std::vector<ContactField>& fields, std::string kind, std::string label, std::string value) {
    value = trim(value);
    if (value.empty()) {
        return;
    }
    const auto duplicate = std::any_of(fields.begin(), fields.end(), [&](const ContactField& field) {
        return field.kind == kind && field.value == value;
    });
    if (!duplicate) {
        fields.push_back(ContactField{std::move(kind), std::move(label), std::move(value)});
    }
}

int digit_count(std::string_view input) {
    return static_cast<int>(std::count_if(input.begin(), input.end(), [](unsigned char ch) {
        return std::isdigit(ch);
    }));
}

bool has_capitalized_words(std::string_view line) {
    static const std::regex pattern(R"(^[A-Z][A-Za-z'.-]+(?:\s+[A-Z][A-Za-z'.-]+){1,3}$)");
    return std::regex_match(std::string{line}, pattern);
}

bool looks_like_address(std::string_view line) {
    static const std::regex street_word(
        R"(\b(street|st\.?|road|rd\.?|avenue|ave\.?|lane|ln\.?|drive|dr\.?|boulevard|blvd\.?|way|place|pl\.?)\b)",
        std::regex_constants::icase);
    return digit_count(line) > 0 && std::regex_search(std::string{line}, street_word);
}

std::vector<ContactField> detect_contact_fields(std::string_view text) {
    std::vector<ContactField> fields;
    const std::string value{text};

    static const std::regex email_pattern(R"(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))");
    for (std::sregex_iterator it(value.begin(), value.end(), email_pattern), end; it != end; ++it) {
        add_field(fields, "email", "Email", (*it)[1].str());
    }

    const auto lines = split_lines(text);
    static const std::regex phone_pattern(R"((\+?\d[\d\s().-]{6,}\d))");
    for (const auto& line : lines) {
        std::smatch match;
        if (std::regex_search(line, match, phone_pattern) && digit_count(match[1].str()) >= 7) {
            add_field(fields, "phone", "Phone", match[1].str());
        }
        if (looks_like_address(line)) {
            add_field(fields, "address", "Address", line);
        }

        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const auto label = lower(trim(std::string_view{line}.substr(0, separator)));
        const auto field_value = trim(std::string_view{line}.substr(separator + 1));
        if (label == "name") {
            add_field(fields, "name", "Name", field_value);
        } else if (label == "org" || label == "organization" || label == "company") {
            add_field(fields, "organization", "Organization", field_value);
        } else if (label == "title" || label == "role") {
            add_field(fields, "title", "Title", field_value);
        }
    }

    if (!fields.empty()) {
        for (const auto& line : lines) {
            if (line.find('@') == std::string::npos && digit_count(line) == 0 && has_capitalized_words(line)) {
                add_field(fields, "name", "Name", line);
                break;
            }
        }
    }

    return fields;
}

bool parse_int(std::string_view value, int& out) {
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool is_valid_ipv4(std::string_view value) {
    std::array<int, 4> octets{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < octets.size(); ++index) {
        const auto dot = value.find('.', start);
        const auto end = dot == std::string_view::npos ? value.size() : dot;
        if (end == start) {
            return false;
        }
        int octet = 0;
        if (!parse_int(value.substr(start, end - start), octet) || octet < 0 || octet > 255) {
            return false;
        }
        octets[index] = octet;
        start = end + 1;
        if (index < octets.size() - 1 && dot == std::string_view::npos) {
            return false;
        }
    }
    return start == value.size() + 1;
}

std::string strip_ip_punctuation(std::string_view token) {
    while (!token.empty() && std::string_view{"[](){}<>,;\"'"}.find(token.front()) != std::string_view::npos) {
        token.remove_prefix(1);
    }
    while (!token.empty() && std::string_view{"[](){}<>,;.\"'"}.find(token.back()) != std::string_view::npos) {
        token.remove_suffix(1);
    }
    return std::string{token};
}

bool is_hex_group(std::string_view group) {
    return !group.empty() && group.size() <= 4 &&
           std::all_of(group.begin(), group.end(), [](unsigned char ch) {
               return std::isxdigit(ch);
           });
}

bool is_valid_ipv6(std::string_view raw) {
    const auto value = strip_ip_punctuation(raw);
    if (value.find("://") != std::string::npos || std::count(value.begin(), value.end(), ':') < 2) {
        return false;
    }
    if (!std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isxdigit(ch) || ch == ':';
        })) {
        return false;
    }

    const bool has_double_colon = value.find("::") != std::string::npos;
    if (has_double_colon && value.find("::", value.find("::") + 1) != std::string::npos) {
        return false;
    }

    std::vector<std::string_view> groups;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto colon = value.find(':', start);
        const auto end = colon == std::string::npos ? value.size() : colon;
        groups.push_back(std::string_view{value}.substr(start, end - start));
        if (colon == std::string::npos) {
            break;
        }
        start = colon + 1;
    }

    int non_empty_groups = 0;
    for (const auto group : groups) {
        if (group.empty()) {
            continue;
        }
        if (!is_hex_group(group)) {
            return false;
        }
        ++non_empty_groups;
    }

    return has_double_colon ? non_empty_groups <= 7 : non_empty_groups == 8;
}

bool detect_ip(std::string_view text) {
    const std::string value{text};
    std::set<std::string> candidates;

    static const std::regex ipv4_pattern(R"((^|[^0-9.])((?:[0-9]{1,3}\.){3}[0-9]{1,3})($|[^0-9.]))");
    for (std::sregex_iterator it(value.begin(), value.end(), ipv4_pattern), end; it != end; ++it) {
        if (is_valid_ipv4((*it)[2].str())) {
            candidates.insert((*it)[2].str());
        }
    }

    std::istringstream input(value);
    std::string token;
    while (input >> token) {
        if (is_valid_ipv6(token)) {
            candidates.insert(strip_ip_punctuation(token));
        }
    }

    return candidates.size() == 1;
}

bool detect_github_url(std::string_view text) {
    static const std::regex pattern(
        R"((^|[\s(<\[])(https://github\.com/[A-Za-z0-9][A-Za-z0-9-]{0,38}/[A-Za-z0-9._-]+(?:\.git)?(?:[?#][^\s]*)?)(?=$|[\s)\]>},.;]))",
        std::regex_constants::icase);
    return std::regex_search(std::string{text}, pattern);
}

bool detect_diagram(std::string_view text) {
    if (has_supported_mermaid_header(text)) return true;
    const std::string value{text};
    static const std::regex diagram_header(
        R"((^|[\r\n])\s*(flowchart|graph|sequenceDiagram|classDiagram|erDiagram|timeline)\b)",
        std::regex_constants::icase);
    static const std::regex uml_header(R"((^|[\r\n])\s*@startuml\b)", std::regex_constants::icase);
    static const std::regex class_relation(R"(\b[A-Z][A-Za-z0-9_]*\s+(extends|inherits)\s+[A-Z][A-Za-z0-9_]*\b)");
    static const std::regex er_signal(
        R"(\b(entity\s+relationship|one-to-many|many-to-many)\b)",
        std::regex_constants::icase);
    if (std::regex_search(value, diagram_header) || std::regex_search(value, uml_header) ||
        std::regex_search(value, er_signal)) {
        return true;
    }
    if (std::regex_search(value, class_relation)) {
        return true;
    }

    static const std::regex compact_arrow(R"(^\s*[A-Za-z0-9_]+\s*(?:->|<-|-->|<--|--)\s*[A-Za-z0-9_]+\s*$)");
    static const std::regex arrow_chain(R"([A-Za-z0-9_]+\s*(?:->|<-|-->|<--|--)\s*[A-Za-z0-9_]+(?:\s*(?:->|<-|-->|<--|--)\s*[A-Za-z0-9_]+)+)");
    return std::regex_search(value, compact_arrow) || std::regex_search(value, arrow_chain);
}

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month) {
    switch (month) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31;
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return is_leap_year(year) ? 29 : 28;
    }
    return 0;
}

bool valid_utc_offset(std::string_view zone) {
    if (zone == "Z") {
        return true;
    }
    if (zone.size() != 6 || (zone.front() != '+' && zone.front() != '-') || zone[3] != ':') {
        return false;
    }
    int hour = 0;
    int minute = 0;
    if (!parse_int(zone.substr(1, 2), hour) || !parse_int(zone.substr(4, 2), minute)) {
        return false;
    }
    if (hour < 0 || hour > 14 || minute < 0 || minute > 59) {
        return false;
    }
    return hour != 14 || minute == 0;
}

bool valid_compact_utc_offset(std::string_view zone) {
    if (zone == "GMT" || zone == "UTC" || zone == "gmt" || zone == "utc") {
        return true;
    }
    if (zone.size() != 5 || (zone.front() != '+' && zone.front() != '-')) {
        return false;
    }
    int hour = 0;
    int minute = 0;
    if (!parse_int(zone.substr(1, 2), hour) || !parse_int(zone.substr(3, 2), minute)) {
        return false;
    }
    if (hour < 0 || hour > 14 || minute < 0 || minute > 59) {
        return false;
    }
    return hour != 14 || minute == 0;
}

int month_from_name(std::string_view name) {
    const auto folded = lower(name);
    constexpr std::array<std::string_view, 12> months = {
        "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec",
    };
    for (std::size_t index = 0; index < months.size(); ++index) {
        if (folded.rfind(months[index], 0) == 0) {
            return static_cast<int>(index + 1);
        }
    }
    return 0;
}

int expanded_year(int year) {
    if (year >= 100) {
        return year;
    }
    return year >= 70 ? 1900 + year : 2000 + year;
}

bool valid_calendar_date(int year, int month, int day) {
    return year > 0 && month >= 1 && month <= 12 && day >= 1 && day <= days_in_month(year, month);
}

bool valid_24_hour_time(int hour, int minute, int second = 0) {
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
}

bool valid_common_time(int hour, int minute, std::string_view meridiem) {
    if (minute < 0 || minute > 59) {
        return false;
    }
    if (!meridiem.empty()) {
        return hour >= 1 && hour <= 12;
    }
    return hour >= 0 && hour <= 23;
}

std::int64_t utc_epoch(std::tm& value) {
#if defined(_WIN32)
    return static_cast<std::int64_t>(_mkgmtime(&value));
#else
    return static_cast<std::int64_t>(timegm(&value));
#endif
}

std::string format_utc(std::int64_t epoch_seconds) {
    const auto time_value = static_cast<std::time_t>(epoch_seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time_value);
#else
    gmtime_r(&time_value, &utc);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::optional<DateTimeValue> detect_date_time_value(std::string_view text) {
    const std::string value{text};

    static const std::regex date_only_pattern(R"(^\s*([0-9]{4})-([0-9]{2})-([0-9]{2})\s*$)");
    std::smatch date_only;
    if (std::regex_match(value, date_only, date_only_pattern)) {
        int year = 0, month = 0, day = 0;
        parse_int(date_only[1].str(), year);
        parse_int(date_only[2].str(), month);
        parse_int(date_only[3].str(), day);
        if (valid_calendar_date(year, month, day)) {
            const auto date = trim(value);
            return DateTimeValue{date, date, {}, 0, false};
        }
    }

    static const std::regex iso_pattern(
        R"(\b([0-9]{4})-([0-9]{2})-([0-9]{2})[T ]([0-9]{2}):([0-9]{2})(?::([0-9]{2}))?(Z|[+-][0-9]{2}:[0-9]{2})\b)");
    for (std::sregex_iterator it(value.begin(), value.end(), iso_pattern), end; it != end; ++it) {
        const auto& match = *it;
        DateTimeValue out;
        out.original = match[0].str();
        out.source_zone = match[7].str() == "Z" ? "UTC" : match[7].str();

        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        parse_int(match[1].str(), year);
        parse_int(match[2].str(), month);
        parse_int(match[3].str(), day);
        parse_int(match[4].str(), hour);
        parse_int(match[5].str(), minute);
        if (match[6].matched) {
            parse_int(match[6].str(), second);
        }

        const auto zone = match[7].str();
        if (valid_calendar_date(year, month, day) && valid_24_hour_time(hour, minute, second) &&
            valid_utc_offset(zone)) {
            std::tm parts{};
            parts.tm_year = year - 1900;
            parts.tm_mon = month - 1;
            parts.tm_mday = day;
            parts.tm_hour = hour;
            parts.tm_min = minute;
            parts.tm_sec = second;
            auto epoch = utc_epoch(parts);
            if (zone != "Z") {
                int zone_hour = 0;
                int zone_minute = 0;
                parse_int(std::string_view{zone}.substr(1, 2), zone_hour);
                parse_int(std::string_view{zone}.substr(4, 2), zone_minute);
                const auto offset = static_cast<std::int64_t>(zone_hour * 3600 + zone_minute * 60);
                epoch += zone.front() == '-' ? offset : -offset;
            }
            out.epoch_seconds = epoch;
            out.has_epoch = true;
            out.normalized = format_utc(epoch);
            return out;
        }
    }

    static const std::regex unix_pattern(R"((^|[^0-9])([0-9]{10}(?:[0-9]{3})?)(?=$|[^0-9]))");
    std::optional<DateTimeValue> unix_value;
    for (std::sregex_iterator it(value.begin(), value.end(), unix_pattern), end; it != end; ++it) {
        const auto timestamp = (*it)[2].str();
        std::int64_t epoch = 0;
        const auto result = std::from_chars(timestamp.data(), timestamp.data() + timestamp.size(), epoch);
        if (result.ec == std::errc{} && result.ptr == timestamp.data() + timestamp.size()) {
            if (timestamp.size() == 13) {
                epoch /= 1000;
            }
            if (epoch >= 946684800 && epoch <= 4102444800LL) {
                if (unix_value.has_value() && unix_value->original != timestamp) return std::nullopt;
                unix_value = DateTimeValue{timestamp, format_utc(epoch), "Unix", epoch, true};
            }
        }
    }
    if (unix_value.has_value()) return unix_value;

    static const std::regex rfc_pattern(
        R"(\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun),\s+([0-9]{1,2})\s+((?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]*)\s+([0-9]{4})\s+([0-9]{2}):([0-9]{2})(?::([0-9]{2}))?\s+(GMT|UTC|[+-][0-9]{4})\b)",
        std::regex_constants::icase);
    for (std::sregex_iterator it(value.begin(), value.end(), rfc_pattern), end; it != end; ++it) {
        const auto& match = *it;
        int day = 0;
        int year = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        parse_int(match[1].str(), day);
        const auto month = month_from_name(match[2].str());
        parse_int(match[3].str(), year);
        parse_int(match[4].str(), hour);
        parse_int(match[5].str(), minute);
        if (match[6].matched) {
            parse_int(match[6].str(), second);
        }
        const auto zone = match[7].str();
        if (valid_calendar_date(year, month, day) && valid_24_hour_time(hour, minute, second) &&
            valid_compact_utc_offset(zone)) {
            return DateTimeValue{match[0].str(), match[0].str(), zone, 0, false};
        }
    }

    static const std::regex numeric_common_pattern(
        R"(\b([0-9]{1,2})[/-]([0-9]{1,2})[/-]([0-9]{2,4})\s+([0-9]{1,2}):([0-9]{2})(?:\s*(AM|PM))?\b)",
        std::regex_constants::icase);
    for (std::sregex_iterator it(value.begin(), value.end(), numeric_common_pattern), end; it != end; ++it) {
        const auto& match = *it;
        int month = 0;
        int day = 0;
        int year = 0;
        int hour = 0;
        int minute = 0;
        parse_int(match[1].str(), month);
        parse_int(match[2].str(), day);
        parse_int(match[3].str(), year);
        parse_int(match[4].str(), hour);
        parse_int(match[5].str(), minute);
        year = expanded_year(year);
        const auto meridiem = match[6].matched ? match[6].str() : std::string{};
        if (valid_calendar_date(year, month, day) && valid_common_time(hour, minute, meridiem)) {
            return DateTimeValue{match[0].str(), match[0].str(), "", 0, false};
        }
    }

    static const std::regex month_common_pattern(
        R"(\b((?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)[a-z]*)\s+([0-9]{1,2}),?\s+([0-9]{4})\s+([0-9]{1,2}):([0-9]{2})(?:\s*(AM|PM))?\b)",
        std::regex_constants::icase);
    for (std::sregex_iterator it(value.begin(), value.end(), month_common_pattern), end; it != end; ++it) {
        const auto& match = *it;
        const auto month = month_from_name(match[1].str());
        int day = 0;
        int year = 0;
        int hour = 0;
        int minute = 0;
        parse_int(match[2].str(), day);
        parse_int(match[3].str(), year);
        parse_int(match[4].str(), hour);
        parse_int(match[5].str(), minute);
        const auto meridiem = match[6].matched ? match[6].str() : std::string{};
        if (valid_calendar_date(year, month, day) && valid_common_time(hour, minute, meridiem)) {
            return DateTimeValue{match[0].str(), match[0].str(), "", 0, false};
        }
    }

    return std::nullopt;
}

bool detect_code(std::string_view text) {
    const std::string value{text};
    static const std::regex code_pattern(
        R"((#include\s*<|^\s*(import|from)\s+\w+|^\s*(def|class|function)\s+\w+|\b(int|auto|const|let|var|fn)\s+\w+|[{};]\s*$|^\s*[$>]\s+\w+))",
        std::regex_constants::icase | std::regex_constants::multiline);
    return std::regex_search(value, code_pattern);
}

}  // namespace

FastContentSignals detect_fast_content(ContentKind kind, std::string_view text) {
    FastContentSignals signals;
    const auto bounded = text.substr(0, std::min(text.size(), kMaxDetectorBytes));

    signals.contact_fields = detect_contact_fields(bounded);
    signals.contact = !signals.contact_fields.empty();
    signals.code = detect_code(bounded);
    signals.diagram = detect_diagram(bounded);
    signals.ip = detect_ip(bounded);
    signals.github_url = detect_github_url(bounded);
    signals.date_time_value = detect_date_time_value(bounded);
    signals.date_time = signals.date_time_value.has_value();
    signals.qr = kind != ContentKind::Image && text.size() <= 2048 && !trim(text).empty();

    return signals;
}

}  // namespace pastit
