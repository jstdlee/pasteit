#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace pasteit {

inline std::string path_to_utf8_string(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {encoded.begin(), encoded.end()};
}

inline std::filesystem::path path_from_utf8_string(std::string_view text) {
    return std::filesystem::u8path(text.begin(), text.end());
}

}  // namespace pasteit
