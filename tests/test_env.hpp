#pragma once

// Portable environment helpers for tests (POSIX setenv/unsetenv, or the
// MSVC _putenv_s equivalents on Windows).
#include <cstdlib>
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

}  // namespace pasteit_test
