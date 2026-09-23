#include "platform/app_paths.hpp"

#include <system_error>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pastit {
namespace {

std::filesystem::path fallback_directory() {
    std::error_code error;
    auto current = std::filesystem::current_path(error);
    if (!error && !current.empty()) {
        return current;
    }
    auto temporary = std::filesystem::temp_directory_path(error);
    if (!error && !temporary.empty()) {
        return temporary;
    }
    return ".";
}

}  // namespace

std::filesystem::path executable_directory() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return fallback_directory();
        }
        if (copied < buffer.size() - 1) {
            return std::filesystem::path{std::wstring{buffer.data(), copied}}.parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::vector<char> buffer(1024);
    for (;;) {
        const ssize_t copied = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (copied < 0) {
            return fallback_directory();
        }
        if (static_cast<std::size_t>(copied) < buffer.size()) {
            return std::filesystem::path{std::string{buffer.data(), static_cast<std::size_t>(copied)}}.parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return fallback_directory();
#endif
}

std::filesystem::path app_settings_path() {
    return executable_directory() / "settings.json";
}

std::filesystem::path app_data_dir() {
    return executable_directory() / "data";
}

}  // namespace pastit
