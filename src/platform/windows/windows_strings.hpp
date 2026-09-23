#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pastit {

std::wstring utf8_to_wide(std::string_view text);
std::string wide_to_utf8(std::wstring_view text);
std::wstring path_to_wide(const std::filesystem::path& path);
std::string path_to_utf8(const std::filesystem::path& path);
std::wstring windows_command_line(const std::vector<std::string>& argv);

}  // namespace pastit
