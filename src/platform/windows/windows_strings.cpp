#include "platform/windows/windows_strings.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pasteit {

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring path_to_wide(const std::filesystem::path& path) {
    return path.wstring();
}

std::string path_to_utf8(const std::filesystem::path& path) {
    return wide_to_utf8(path.wstring());
}

std::wstring windows_command_line(const std::vector<std::string>& argv) {
    std::wstring command;
    for (const auto& arg : argv) {
        if (!command.empty()) {
            command.push_back(L' ');
        }
        const auto wide = utf8_to_wide(arg);
        const bool needs_quotes = wide.empty() || wide.find_first_of(L" \t\n\v\"") != std::wstring::npos;
        if (!needs_quotes) {
            command += wide;
            continue;
        }

        command.push_back(L'"');
        unsigned int backslashes = 0;
        for (const wchar_t ch : wide) {
            if (ch == L'\\') {
                ++backslashes;
                continue;
            }
            if (ch == L'"') {
                command.append(backslashes * 2U + 1U, L'\\');
                command.push_back(ch);
                backslashes = 0;
                continue;
            }
            command.append(backslashes, L'\\');
            backslashes = 0;
            command.push_back(ch);
        }
        command.append(backslashes * 2U, L'\\');
        command.push_back(L'"');
    }
    return command;
}

}  // namespace pasteit
