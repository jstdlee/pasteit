#pragma once

#include "core/types.hpp"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace pasteit {

enum class DestinationRole {
    Manual,
    SourceParent,
    FocusedDirectory,
    ConfiguredDefault,
    Recent,
    Temporary,
};

struct DestinationCandidate {
    std::filesystem::path path;
    DestinationRole role = DestinationRole::Temporary;
    bool exists = false;
};

std::vector<DestinationCandidate> resolve_file_targets(
    const ClipboardItem& current_item,
    std::string_view source_text,
    std::optional<std::filesystem::path> manual,
    std::optional<std::filesystem::path> focused,
    std::optional<std::filesystem::path> configured_default,
    const std::vector<PathLocation>& recent);

}  // namespace pasteit
