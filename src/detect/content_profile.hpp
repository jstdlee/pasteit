#pragma once

#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pastit {

// General shape of clipboard data, judged from a bounded sample (first rows,
// a middle probe, and the tail) plus the total length.
enum class DataShape {
    Empty,
    Token,  // one short value: color, UUID, JWT, IP, number, word
    Prose,
    Json,
    Ndjson,
    Csv,
    Tsv,
    Markdown,
    Code,
    Log,
    KeyValue,
    Yaml,
    Html,
    Xml,
    Sql,
    UrlList,
    PathList,
    NumberSeries,
    Binary,
};

struct ContentSample {
    std::string head;    // first rows (<= 16 KiB)
    std::string middle;  // probe from the middle when the text is larger
    std::string tail;    // last 4 KiB when the text is larger
    std::uint64_t total_bytes = 0;
    bool complete = false;  // head holds the whole text
};

struct ContentProfile {
    DataShape shape = DataShape::Empty;
    double confidence = 0.0;  // 0..1
    std::vector<std::pair<DataShape, double>> candidates;  // best first, up to 3
    std::vector<std::string> tags;
    std::uint64_t total_bytes = 0;
    std::size_t sampled_lines = 0;
    std::size_t estimated_lines = 0;
    // Tables (CSV/TSV/pipe): delimiter, columns, header row guess.
    char delimiter = 0;
    std::size_t columns = 0;
    std::size_t numeric_columns = 0;
    bool header = false;
    std::string language;  // code language guess, e.g. "py"
    bool validated = false;  // confirmed by a full parse, not only sampled

    bool is(DataShape value, double minimum = 0.6) const { return shape == value && confidence >= minimum; }
    bool has_tag(std::string_view tag) const;
};

inline constexpr std::size_t kProfileHeadBytes = 16 * 1024;
inline constexpr std::size_t kProfileTailBytes = 4 * 1024;

ContentSample sample_text(std::string_view text);
ContentSample sample_file(const std::filesystem::path& path);

ContentProfile profile_content(ContentKind kind, const ContentSample& sample);
inline ContentProfile profile_content(ContentKind kind, std::string_view text) {
    return profile_content(kind, sample_text(text));
}

std::string data_shape_name(DataShape shape);
std::string data_shape_description(DataShape shape);

}  // namespace pastit
