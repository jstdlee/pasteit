#include "history/path_target_resolver.hpp"

#include "storage/path_history.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <unordered_set>

namespace pasteit {
namespace {

bool path_like_clipboard_item(const ClipboardItem& item) {
    if (item.kind == ContentKind::Path) {
        return true;
    }
    return std::any_of(item.tags.begin(), item.tags.end(), [](SemanticTag tag) {
        return tag == SemanticTag::Path || tag == SemanticTag::FileUri;
    });
}

std::filesystem::path normalized_path(std::filesystem::path path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    if (path.is_relative()) {
        auto absolute = std::filesystem::absolute(path, error);
        if (!error) {
            path = std::move(absolute);
        }
        error.clear();
    }

    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error && !canonical.empty()) {
        return canonical.lexically_normal();
    }
    return path.lexically_normal();
}

bool is_existing_directory(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

bool is_existing_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::vector<std::filesystem::path> source_paths_from(std::string_view source_text) {
    auto paths = paths_from_uri_list(source_text);
    if (!paths.empty()) {
        return paths;
    }

    if (const auto uri_path = path_from_file_uri(std::string{source_text})) {
        paths.push_back(*uri_path);
        return paths;
    }

    std::filesystem::path path = path_from_utf8_string(source_text);
    if (path.is_absolute()) {
        paths.push_back(std::move(path));
    }
    return paths;
}

std::optional<std::filesystem::path> destination_for_source_path(const std::filesystem::path& source_path) {
    const auto normalized = normalized_path(source_path);
    if (normalized.empty()) {
        return std::nullopt;
    }
    if (is_existing_directory(normalized)) {
        const auto parent = normalized.parent_path();
        return parent.empty() ? std::nullopt : std::optional<std::filesystem::path>{parent};
    }

    const auto parent = normalized.parent_path();
    if (parent.empty() || parent == normalized) {
        return std::nullopt;
    }
    if (is_existing_directory(parent) || is_existing_file(normalized)) {
        return parent;
    }
    return std::nullopt;
}

void add_candidate(std::vector<DestinationCandidate>& candidates,
                   std::unordered_set<std::string>& seen,
                   const std::filesystem::path& path,
                   DestinationRole role,
                   bool keep_invalid = false) {
    const auto normalized = normalized_path(path);
    if (normalized.empty()) {
        return;
    }

    const bool exists = is_existing_directory(normalized);
    if (!exists && !keep_invalid) {
        return;
    }

    const auto key = path_to_utf8_string(normalized);
    if (!seen.insert(key).second) {
        return;
    }
    candidates.push_back(DestinationCandidate{.path = normalized, .role = role, .exists = exists});
}

}  // namespace

std::vector<DestinationCandidate> resolve_file_targets(
    const ClipboardItem& current_item,
    std::string_view source_text,
    std::optional<std::filesystem::path> manual,
    std::optional<std::filesystem::path> focused,
    std::optional<std::filesystem::path> configured_default,
    const std::vector<PathLocation>& recent) {
    std::vector<DestinationCandidate> candidates;
    std::unordered_set<std::string> seen;

    if (manual) {
        add_candidate(candidates, seen, *manual, DestinationRole::Manual, true);
    }

    if (path_like_clipboard_item(current_item)) {
        for (const auto& source_path : source_paths_from(source_text)) {
            if (const auto destination = destination_for_source_path(source_path)) {
                add_candidate(candidates, seen, *destination, DestinationRole::SourceParent);
            }
        }
    }

    if (focused) {
        add_candidate(candidates, seen, *focused, DestinationRole::FocusedDirectory);
    }
    if (configured_default) {
        add_candidate(candidates, seen, *configured_default, DestinationRole::ConfiguredDefault);
    }
    for (const auto& location : recent) {
        if (location.kind == PathKind::Directory && location.exists) {
            add_candidate(candidates, seen, location.path, DestinationRole::Recent);
        }
    }

    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (!error) {
        add_candidate(candidates, seen, temporary, DestinationRole::Temporary);
    }

    return candidates;
}

}  // namespace pasteit
