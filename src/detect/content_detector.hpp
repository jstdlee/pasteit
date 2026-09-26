#pragma once

#include "core/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

struct DetectionResult {
    ContentKind kind = ContentKind::Text;
    std::vector<SemanticTag> tags;
    bool valid_json = false;
    std::filesystem::path path;
    PathKind path_kind = PathKind::File;
};

DetectionResult detect_content(const std::vector<std::string>& mime_types, std::string_view text);

}  // namespace pasteit
