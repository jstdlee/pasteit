#include "util/replace_file.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pastit {

void replace_file(const std::filesystem::path& replacement,
                  const std::filesystem::path& destination, std::error_code& error) {
    error.clear();
#if defined(_WIN32)
    if (!MoveFileExW(replacement.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD move_error = GetLastError();
        error = std::error_code(static_cast<int>(move_error), std::system_category());
    }
#else
    std::filesystem::rename(replacement, destination, error);
#endif
}

}  // namespace pastit
