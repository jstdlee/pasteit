#include "storage/path_history.hpp"

#include <algorithm>
#include <cmath>
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace pasteit {
namespace {

std::string next_path_ref(std::uint64_t value) {
    std::ostringstream out;
    out << "path_" << std::setw(2) << std::setfill('0') << value;
    return out.str();
}

std::string decode_uri_component(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const auto hex = std::string{value.substr(index + 1, 2)};
            char* end = nullptr;
            const auto decoded = std::strtol(hex.c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                out.push_back(static_cast<char>(decoded));
                index += 2;
                continue;
            }
        }
        out.push_back(value[index]);
    }
    return out;
}

std::filesystem::path normalize_path(std::filesystem::path path) {
    if (path.is_relative()) {
        path = std::filesystem::absolute(path);
    }
    return path.lexically_normal();
}

PathKind kind_for(const std::filesystem::path& path, PathKind fallback) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        return PathKind::Directory;
    }
    if (std::filesystem::is_regular_file(path, error)) {
        return PathKind::File;
    }
    return fallback;
}

bool exists_for(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

}  // namespace

std::optional<std::filesystem::path> path_from_file_uri(const std::string& value) {
    constexpr std::string_view prefix = "file://";
    if (value.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    auto rest = value.substr(prefix.size());
    if (rest.rfind("localhost/", 0) == 0) {
        rest = rest.substr(std::string{"localhost"}.size());
    }
#if defined(_WIN32)
    if (rest.size() >= 3 && rest[0] == '/' && std::isalpha(static_cast<unsigned char>(rest[1])) != 0 &&
        rest[2] == ':') {
        rest.erase(rest.begin());
    }
#endif
    if (rest.empty() || rest.front() != '/') {
#if defined(_WIN32)
        if (rest.size() >= 2 && std::isalpha(static_cast<unsigned char>(rest[0])) != 0 && rest[1] == ':') {
            return normalize_path(path_from_utf8_string(decode_uri_component(rest)));
        }
        if (rest.find('/') != std::string::npos) {
            auto decoded = decode_uri_component(rest);
            std::replace(decoded.begin(), decoded.end(), '/', '\\');
            return normalize_path(path_from_utf8_string("\\\\" + decoded));
        }
#endif
        return std::nullopt;
    }
    return normalize_path(path_from_utf8_string(decode_uri_component(rest)));
}

std::vector<std::filesystem::path> paths_from_uri_list(std::string_view value) {
    std::vector<std::filesystem::path> paths;
    std::size_t begin=0;
    while(begin<=value.size()){
        const auto end=value.find_first_of("\r\n",begin);auto line=value.substr(begin,end==std::string_view::npos?value.size()-begin:end-begin);
        while(!line.empty()&&std::isspace(static_cast<unsigned char>(line.front())))line.remove_prefix(1);
        while(!line.empty()&&std::isspace(static_cast<unsigned char>(line.back())))line.remove_suffix(1);
        if(!line.empty()&&line.front()!='#'){
            if(const auto path=path_from_file_uri(std::string{line}))paths.push_back(*path);
            else if(line.front()=='/')paths.push_back(path_from_utf8_string(line));
        }
        if(end==std::string_view::npos)break;begin=end+1;while(begin<value.size()&&(value[begin]=='\r'||value[begin]=='\n'))++begin;
    }
    return paths;
}

std::optional<PathLocation> PathHistory::observe(const std::string& value, const std::string& source, std::int64_t now_ms) {
    std::filesystem::path path;
    if (const auto uri_path = path_from_file_uri(value)) {
        path = *uri_path;
    } else {
        path = path_from_utf8_string(value);
        if (!path.is_absolute()) {
            return std::nullopt;
        }
        path = normalize_path(path);
    }

    auto kind = kind_for(path, PathKind::File);
    if (kind == PathKind::File) {
        const auto file = observe_path(path, PathKind::File, source, now_ms);
        if (!path.parent_path().empty()) {
            (void)observe_path(path.parent_path(), PathKind::Directory, source, now_ms);
        }
        return file;
    }
    return observe_path(path, kind, source, now_ms);
}

std::vector<PathLocation> PathHistory::destination_directories(std::size_t limit) const {
    auto values = recent(locations_.size());
    std::erase_if(values, [](const auto& value) { return value.kind != PathKind::Directory || !value.exists; });
    if (values.size() > limit) values.resize(limit);
    return values;
}

PathLocation PathHistory::observe_path(const std::filesystem::path& path, PathKind kind, const std::string& source, std::int64_t now_ms) {
    const auto normalized = normalize_path(path);
    for (auto& location : locations_) {
        if (location.path == normalized) {
            location.kind = kind;
            location.last_seen_ms = now_ms;
            location.source = source;
            location.exists = exists_for(normalized);
            return location;
        }
    }

    PathLocation location;
    location.ref = next_path_ref(next_ref_++);
    location.path = normalized;
    location.kind = kind;
    location.last_seen_ms = now_ms;
    location.source = source;
    location.exists = exists_for(normalized);
    locations_.push_back(location);
    return location;
}

double path_rank_score(const PathLocation& location, std::int64_t now_ms) {
    constexpr double kDay = 24.0 * 60.0 * 60.0 * 1000.0;
    const auto age = [&](std::int64_t then) { return std::max(0.0, static_cast<double>(now_ms - then)); };
    const double use = location.use_weight * std::pow(0.5, age(location.last_used_ms) / (14.0 * kDay));
    const double lifetime = 0.3 * std::log1p(static_cast<double>(location.use_count));
    const double seen = location.last_seen_ms > 0 ? 0.5 * std::pow(0.5, age(location.last_seen_ms) / (2.0 * kDay)) : 0.0;
    return use + lifetime + seen;
}

void sort_paths_by_rank(std::vector<PathLocation>& paths, std::int64_t now_ms) {
    std::stable_sort(paths.begin(), paths.end(), [now_ms](const PathLocation& left, const PathLocation& right) {
        if (left.exists != right.exists) return left.exists && !right.exists;
        const double left_score = path_rank_score(left, now_ms);
        const double right_score = path_rank_score(right, now_ms);
        if (std::abs(left_score - right_score) > 1e-9) return left_score > right_score;
        return std::max(left.last_used_ms, left.last_seen_ms) > std::max(right.last_used_ms, right.last_seen_ms);
    });
}

void PathHistory::record_use(const std::filesystem::path& directory, std::int64_t now_ms) {
    if (directory.empty()) return;
    (void)observe_path(directory, PathKind::Directory, "pasteit", now_ms);
    const auto normalized = normalize_path(directory);
    for (auto& location : locations_) {
        if (location.path != normalized) continue;
        constexpr double kDay = 24.0 * 60.0 * 60.0 * 1000.0;
        const double age = std::max(0.0, static_cast<double>(now_ms - location.last_used_ms));
        location.use_weight = location.use_weight * std::pow(0.5, age / (14.0 * kDay)) + 1.0;
        ++location.use_count;
        location.last_used_ms = now_ms;
        return;
    }
}

void PathHistory::restore_use(const std::filesystem::path& path, double weight, std::uint32_t count,
                              std::int64_t last_used_ms) {
    const auto normalized = normalize_path(path);
    for (auto& location : locations_) {
        if (location.path != normalized) continue;
        location.use_weight = std::isfinite(weight) ? std::max(0.0, weight) : 0.0;
        location.use_count = count;
        location.last_used_ms = last_used_ms;
        return;
    }
}

std::vector<PathLocation> PathHistory::recent(std::size_t limit) const {
    auto out = locations_;
    std::erase_if(out, [](const auto& value) { return value.source == "proc-fd"; });
    // Rank relative to the newest activity so ordering is stable over time.
    std::int64_t now_ms = 0;
    for (const auto& value : out) now_ms = std::max({now_ms, value.last_seen_ms, value.last_used_ms});
    sort_paths_by_rank(out, now_ms);
    if (out.size() > limit) {
        out.resize(limit);
    }
    return out;
}

std::optional<PathLocation> PathHistory::find(const std::string& ref) const {
    for (const auto& location : locations_) {
        if (location.ref == ref) {
            return location;
        }
    }
    return std::nullopt;
}

void PathHistory::retain_latest(std::size_t limit) {
    locations_ = recent(locations_.size());
    if (locations_.size() > limit) locations_.resize(limit);
}

void PathHistory::clear() {
    locations_.clear();
}

}  // namespace pasteit
