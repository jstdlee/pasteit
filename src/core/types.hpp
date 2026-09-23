#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pastit {

enum class ContentKind {
    Unknown,
    Text,
    Url,
    Email,
    Image,
    Path,
    Json,
    DateTime,
};

enum class SemanticTag {
    PlainText,
    Url,
    Email,
    Image,
    Path,
    FileUri,
    Json,
    Resume,
    DateTime,
};

enum class PathKind {
    File,
    Directory,
};

struct ClipboardItem {
    std::string ref;
    std::string content_hash;
    std::vector<std::string> mime_types;
    ContentKind kind = ContentKind::Unknown;
    std::filesystem::path blob_path;
    std::string preview;
    std::uint64_t size_bytes = 0;
    std::int64_t captured_at_ms = 0;
    std::string source_app;
    std::vector<SemanticTag> tags;
};

struct PathLocation {
    std::string ref;
    std::filesystem::path path;
    PathKind kind = PathKind::Directory;
    std::int64_t last_seen_ms = 0;
    std::string source;
    bool exists = false;
};

}  // namespace pastit
