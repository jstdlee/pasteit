#pragma once

#include <filesystem>
#include <system_error>

namespace pasteit {

// Replace a file with a completed sibling temporary file, including when the
// destination already exists on Windows.
void replace_file(const std::filesystem::path& replacement,
                  const std::filesystem::path& destination, std::error_code& error);

}  // namespace pasteit
