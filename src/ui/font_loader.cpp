#include "ui/font_loader.hpp"
namespace pastit {std::filesystem::path first_existing_font(const std::vector<std::filesystem::path>& candidates){for(const auto& path:candidates){std::error_code error;if(std::filesystem::is_regular_file(path,error))return path;}return {};}}
