#include "detect/content_detector.hpp"

#include "storage/path_history.hpp"
#include "util/json.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace pasteit {
namespace {

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

bool has_mime_prefix(const std::vector<std::string>& mime_types, std::string_view prefix) {
    return std::any_of(mime_types.begin(), mime_types.end(), [prefix](const std::string& mime) {
        return mime.rfind(prefix, 0) == 0;
    });
}

bool has_mime(const std::vector<std::string>& mime_types, std::string_view target) {
    return std::any_of(mime_types.begin(), mime_types.end(), [target](const std::string& mime) {
        return mime == target;
    });
}

bool is_http_url(std::string_view value) {
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

bool is_email_address(const std::string& value) {
    static const std::regex pattern(R"(^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$)");
    return std::regex_match(value, pattern);
}

PathKind path_kind_for(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        return PathKind::Directory;
    }
    return PathKind::File;
}

}  // namespace

DetectionResult detect_content(const std::vector<std::string>& mime_types, std::string_view text) {
    DetectionResult result;
    const auto value = trim(text);

    if (has_mime_prefix(mime_types, "image/")) {
        result.kind = ContentKind::Image;
        result.tags = {SemanticTag::Image};
        return result;
    }

    if (has_mime(mime_types, "application/json") || (!value.empty() && (value.front() == '{' || value.front() == '['))) {
        if (is_valid_json(value)) {
            result.kind = ContentKind::Json;
            result.tags = {SemanticTag::Json};
            result.valid_json = true;
            return result;
        }
    }

    if (const auto uri_path = path_from_file_uri(value)) {
        result.kind = ContentKind::Path;
        result.tags = {SemanticTag::Path, SemanticTag::FileUri};
        result.path = *uri_path;
        result.path_kind = path_kind_for(*uri_path);
        return result;
    }

    const std::filesystem::path maybe_path = path_from_utf8_string(value);
    if (!value.empty() && maybe_path.is_absolute()) {
        result.kind = ContentKind::Path;
        result.tags = {SemanticTag::Path};
        result.path = maybe_path.lexically_normal();
        result.path_kind = path_kind_for(result.path);
        return result;
    }

    if (is_http_url(value)) {
        result.kind = ContentKind::Url;
        result.tags = {SemanticTag::Url};
        return result;
    }

    if (value.rfind("mailto:", 0) == 0) {
        result.kind = ContentKind::Email;
        result.tags = {SemanticTag::Email, SemanticTag::Url};
        return result;
    }

    if (is_email_address(value)) {
        result.kind = ContentKind::Email;
        result.tags = {SemanticTag::Email};
        return result;
    }

    result.kind = ContentKind::Text;
    result.tags = {SemanticTag::PlainText};
    result.valid_json = false;
    return result;
}

}  // namespace pasteit
