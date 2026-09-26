#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pasteit {

struct ResumeField {
    std::string kind;
    std::string value;
    std::pair<std::size_t, std::size_t> source_range{0, 0};
};

struct ResumeExtraction {
    bool is_resume = false;
    std::vector<ResumeField> fields;

    std::optional<ResumeField> find(const std::string& kind) const;
};

ResumeExtraction detect_resume_fields(const std::string& text);

}  // namespace pasteit
