#include "detect/resume_detector.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <regex>
#include <sstream>

namespace pastit {
namespace {

std::string trim_copy(std::string_view input) {
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

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void add_field(ResumeExtraction& extraction, const std::string& kind, const std::string& value, std::size_t start) {
    if (value.empty() || extraction.find(kind).has_value()) {
        return;
    }
    extraction.fields.push_back(ResumeField{
        .kind = kind,
        .value = value,
        .source_range = {start, start + value.size()},
    });
}

}  // namespace

std::optional<ResumeField> ResumeExtraction::find(const std::string& kind) const {
    for (const auto& field : fields) {
        if (field.kind == kind) {
            return field;
        }
    }
    return std::nullopt;
}

ResumeExtraction detect_resume_fields(const std::string& text) {
    ResumeExtraction extraction;
    static const std::regex email_pattern(R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})");
    static const std::regex phone_pattern(R"((\+?\d[\d ()\-]{6,}\d))");

    std::smatch match;
    if (std::regex_search(text, match, email_pattern)) {
        add_field(extraction, "email", match.str(0), static_cast<std::size_t>(match.position(0)));
    }
    if (std::regex_search(text, match, phone_pattern)) {
        add_field(extraction, "phone", trim_copy(match.str(0)), static_cast<std::size_t>(match.position(0)));
    }

    std::istringstream lines(text);
    std::string line;
    std::size_t offset = 0;
    bool next_line_is_skills = false;
    while (std::getline(lines, line)) {
        const auto trimmed = trim_copy(line);
        const auto line_start = text.find(line, offset);
        const auto value_start = line_start == std::string::npos ? offset : line_start + line.find(trimmed);
        const auto lowered = lower_copy(trimmed);

        if (!trimmed.empty() && !extraction.find("name").has_value() && lowered != "skills" &&
            !std::regex_search(trimmed, email_pattern) && !std::regex_search(trimmed, phone_pattern) &&
            trimmed.find("://") == std::string::npos) {
            add_field(extraction, "name", trimmed, value_start);
        }

        if (next_line_is_skills && !trimmed.empty()) {
            add_field(extraction, "skills", trimmed, value_start);
            next_line_is_skills = false;
        }
        if (lowered == "skills" || lowered == "technical skills") {
            next_line_is_skills = true;
        }

        offset = (line_start == std::string::npos ? offset + line.size() : line_start + line.size()) + 1;
    }

    // A name plus an email is just a contact card; a resume also has
    // section headings or a skills block.
    static const std::regex heading_pattern(
        R"(^\s*(experience|work experience|work history|employment|education|skills|technical skills|projects|summary|certifications)\s*:?\s*$)",
        std::regex_constants::icase | std::regex_constants::multiline);
    const auto headings = static_cast<std::size_t>(std::distance(
        std::sregex_iterator(text.begin(), text.end(), heading_pattern), std::sregex_iterator()));
    extraction.is_resume = extraction.find("email").has_value() &&
                           (headings >= 2 || (extraction.find("phone").has_value() &&
                                              (headings >= 1 || extraction.find("skills").has_value())));
    return extraction;
}

}  // namespace pastit
