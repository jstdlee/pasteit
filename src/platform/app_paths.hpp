#pragma once

#include <filesystem>

namespace pastit {

std::filesystem::path executable_directory();
std::filesystem::path app_settings_path();
std::filesystem::path app_data_dir();

}  // namespace pastit
