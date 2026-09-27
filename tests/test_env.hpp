#pragma once

// Portable environment helpers for tests (POSIX setenv/unsetenv, or the
// MSVC _putenv_s equivalents on Windows).
#include <cstdlib>
#include <filesystem>
#include <string>

namespace pasteit_test {

inline void set_env(const char* name, const char* value, bool overwrite = true) {
    if (!overwrite && std::getenv(name) != nullptr) return;
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

inline void unset_env(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");  // an empty value removes the variable
#else
    unsetenv(name);
#endif
}

// Removes a temporary test folder without throwing. On Windows a file that
// is still open (e.g. a stream in the same scope) cannot be deleted; a
// leftover temp folder must not fail the test.
inline void remove_tree(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

}  // namespace pasteit_test
