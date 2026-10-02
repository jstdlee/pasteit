#include "platform/app_paths.hpp"

#include <cstdlib>
#include <system_error>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pasteit {
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

namespace {

// Portable layout (settings and data next to the executable) when that
// folder is writable: dev builds and unzipped releases. Installed copies
// (/opt, Program Files, a read-only AppImage mount) use per-user folders.
bool portable_layout() {
    static const bool portable = [] {
        if (std::getenv("APPIMAGE") != nullptr) return false;
        std::error_code error;
        const auto data = executable_directory() / "data";
        if (std::filesystem::is_directory(data, error)) return true;
        return std::filesystem::create_directories(data, error) && !error;
    }();
    return portable;
}

std::filesystem::path user_directory(bool data) {
#if defined(_WIN32)
    (void)data;
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && *appdata != '\0') {
        return std::filesystem::path{appdata} / "PasteIt";
    }
#else
    const char* xdg = std::getenv(data ? "XDG_DATA_HOME" : "XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') return std::filesystem::path{xdg} / "pasteit";
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path{home} / (data ? ".local/share" : ".config") / "pasteit";
    }
#endif
    return fallback_directory() / "pasteit";
}

}  // namespace

std::filesystem::path app_settings_path() {
    return portable_layout() ? executable_directory() / "settings.json" : user_directory(false) / "settings.json";
}

std::filesystem::path app_data_dir() {
#if defined(_WIN32)
    return portable_layout() ? executable_directory() / "data" : user_directory(true) / "data";
#else
    return portable_layout() ? executable_directory() / "data" : user_directory(true);
#endif
}

}  // namespace pasteit
