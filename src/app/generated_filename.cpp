#include "app/generated_filename.hpp"
#include "transform/text_transforms.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string_view>

namespace pastit {
namespace {

std::string ascii_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string normalized_mime(std::string mime) {
    const auto semicolon = mime.find(';');
    if (semicolon != std::string::npos) {
        mime.erase(semicolon);
    }
    return ascii_lower(std::move(mime));
}

bool has_mime(const ClipboardItem& item, std::string_view mime) {
    for (const auto& candidate : item.mime_types) {
        if (normalized_mime(candidate) == mime) {
            return true;
        }
    }
    return false;
}

std::string image_extension_for(const ClipboardItem& item) {
    if (has_mime(item, "image/png")) {
        return ".png";
    }
    if (has_mime(item, "image/jpeg") || has_mime(item, "image/jpg")) {
        return ".jpg";
    }
    if (has_mime(item, "image/gif")) {
        return ".gif";
    }
    if (has_mime(item, "image/webp")) {
        return ".webp";
    }
    if (has_mime(item, "image/bmp")) {
        return ".bmp";
    }
    if (has_mime(item, "image/tiff")) {
        return ".tiff";
    }
    return ".bin";
}

std::string trimmed(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool reliable_extension(std::string_view extension) {
    if (extension.size() < 2 || extension.size() > 11 || extension.front() != '.') {
        return false;
    }
    for (std::size_t index = 1; index < extension.size(); ++index) {
        const auto ch = static_cast<unsigned char>(extension[index]);
        if (!std::isalnum(ch)) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> url_suffix_extension(std::string url) {
    url = trimmed(std::move(url));
    if (url.empty()) {
        return std::nullopt;
    }
    const auto fragment = url.find('#');
    if (fragment != std::string::npos) {
        url.erase(fragment);
    }
    const auto query = url.find('?');
    if (query != std::string::npos) {
        url.erase(query);
    }
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    const auto slash = url.find_last_of('/');
    const auto filename = slash == std::string::npos ? url : url.substr(slash + 1);
    const auto extension = ascii_lower(path_to_utf8_string(path_from_utf8_string(filename).extension()));
    if (!reliable_extension(extension)) {
        return std::nullopt;
    }
    return extension;
}

std::string extension_for(ActionKind action_kind, const ClipboardItem& item) {
    switch (action_kind) {
        case ActionKind::SaveImageFile:
            return image_extension_for(item);
        case ActionKind::SaveJsonFile:
        case ActionKind::SaveJsonPrettyFile:
        case ActionKind::SaveResumeFile:
            return ".json";
        case ActionKind::SaveEmailFile:
            return ".eml";
        case ActionKind::SaveUrlFile:
        case ActionKind::DownloadUrl:
            return url_suffix_extension(item.preview).value_or(".bin");
        case ActionKind::SaveTextFile:
            return ".txt";
        case ActionKind::SaveContactVCard:
            return ".vcf";
        case ActionKind::SaveCodeFile:
            return "." + guess_code_extension(item.preview);
        default:
            if (item.kind == ContentKind::Image) {
                return image_extension_for(item);
            }
            if (item.kind == ContentKind::Json || has_mime(item, "application/json")) {
                return ".json";
            }
            if (item.kind == ContentKind::Email || has_mime(item, "message/rfc822")) {
                return ".eml";
            }
            if (item.kind == ContentKind::Url) {
                return ".url";
            }
            if (item.kind == ContentKind::Text || has_mime(item, "text/plain")) {
                return ".txt";
            }
            return ".bin";
    }
}

std::string local_hour_prefix(std::chrono::system_clock::time_point when) {
    const auto time = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
#if defined(_MSC_VER)
    if (localtime_s(&local, &time) != 0) {
        throw std::runtime_error("failed to convert generated filename time");
    }
#else
    if (localtime_r(&time, &local) == nullptr) {
        throw std::runtime_error("failed to convert generated filename time");
    }
#endif

    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d%H");
    return out.str();
}

}  // namespace

std::string generated_filename(
    ActionKind action_kind,
    const ClipboardItem& item,
    std::chrono::system_clock::time_point when,
    const std::filesystem::path& directory) {
    const auto prefix = local_hour_prefix(when);
    const auto extension = extension_for(action_kind, item);

    for (std::size_t serial = 1;; ++serial) {
        std::ostringstream name;
        name << prefix << '-' << std::setw(2) << std::setfill('0') << serial << extension;
        if (!std::filesystem::exists(directory / name.str())) {
            return name.str();
        }
    }
}

}  // namespace pastit
