#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pastit {

class PathHistory {
public:
    std::optional<PathLocation> observe(const std::string& value, const std::string& source, std::int64_t now_ms);
    PathLocation observe_path(const std::filesystem::path& path, PathKind kind, const std::string& source, std::int64_t now_ms);
    std::vector<PathLocation> recent(std::size_t limit) const;
    std::vector<PathLocation> destination_directories(std::size_t limit) const;
    std::optional<PathLocation> find(const std::string& ref) const;
    void retain_latest(std::size_t limit);
    void clear();

private:
    std::vector<PathLocation> locations_;
    std::uint64_t next_ref_ = 1;
};

std::optional<std::filesystem::path> path_from_file_uri(const std::string& value);
std::vector<std::filesystem::path> paths_from_uri_list(std::string_view value);

}  // namespace pastit
