#include "config/app_settings.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace pastit {
namespace {
std::filesystem::path home_path(std::string_view suffix) {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return (home == nullptr ? std::filesystem::temp_directory_path() : std::filesystem::path{home}) / suffix;
}

bool is_hash_algorithm(std::string_view value) {
    return value == "sha256" || value == "sha512";
}

bool is_qr_error_correction(std::string_view value) {
    return value == "L" || value == "M" || value == "Q" || value == "H";
}

std::string lowercase_ascii(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool is_annotation_format(std::string_view value) {
    return value == "svg";
}
}

std::vector<PromptTemplate> default_prompt_templates() {
    return {
        {.id="builtin-translate", .name="Translate", .system_prompt="Translate {text}. Source language: {source_language}. Target language: {target_language}.", .temperature=0.2, .enabled=true, .built_in=true},
        {.id="builtin-rewrite", .name="Rewrite", .system_prompt="Rewrite for clarity and fluency while preserving meaning: {text}", .temperature=0.3, .enabled=true, .built_in=true},
        {.id="builtin-summarize", .name="Summarize", .system_prompt="Summarize factually and concisely: {text}", .temperature=0.2, .enabled=true, .built_in=true},
        {.id="builtin-explain-text", .name="Explain text", .system_prompt="Explain this text clearly and concisely: {text}", .temperature=0.2, .enabled=true, .built_in=true},
        {.id="builtin-explain-code", .name="Explain code", .system_prompt="Explain what this code does, including important inputs, outputs, and edge cases: {text}", .temperature=0.2, .enabled=true, .built_in=true},
    };
}

AppSettings default_settings() {
    AppSettings settings;
    settings.default_image_directory = home_path("Pictures");
    settings.default_text_directory = home_path("Documents");
    settings.djev.endpoint = "http://127.0.0.1:8011";
    settings.djev.model_id = "jev-latest";
    settings.downloads.resume_directory = home_path("Downloads");
    settings.hash.default_algorithms = {"sha256", "sha512"};
#if defined(_WIN32)
    settings.terminal.command = {"cmd.exe"};
#else
    settings.terminal.command = {"x-terminal-emulator"};
#endif
    settings.annotation.save_directory = home_path("Pictures");
    settings.prompt_templates = default_prompt_templates();
    return settings;
}

void normalize_settings(AppSettings& settings) {
    settings.window_opacity = std::clamp(settings.window_opacity, 0.55F, 1.0F);
    if (settings.schema_version < 1) settings.schema_version = 1;
    if (!is_qr_error_correction(settings.renderers.qr_error_correction)) {
        settings.renderers.qr_error_correction = "M";
    }
    settings.renderers.qr_margin = std::clamp(settings.renderers.qr_margin, 0, 10);
    settings.renderers.qr_scale = std::clamp(settings.renderers.qr_scale, 1, 64);
    settings.hash.default_algorithms.erase(
        std::remove_if(settings.hash.default_algorithms.begin(), settings.hash.default_algorithms.end(),
                       [](const std::string& value) { return !is_hash_algorithm(value); }),
        settings.hash.default_algorithms.end());
    if (settings.hash.default_algorithms.empty()) {
        settings.hash.default_algorithms = {"sha256", "sha512"};
    }
    settings.terminal.command.erase(
        std::remove_if(settings.terminal.command.begin(), settings.terminal.command.end(),
                       [](const std::string& value) { return value.empty(); }),
        settings.terminal.command.end());
    if (settings.terminal.command.empty()) {
#if defined(_WIN32)
        settings.terminal.command = {"cmd.exe"};
#else
        settings.terminal.command = {"x-terminal-emulator"};
#endif
    }
    if (settings.date_time.source_zone.empty()) settings.date_time.source_zone = "UTC";
    if (settings.date_time.target_zone.empty()) settings.date_time.target_zone = "UTC";
    settings.annotation.export_format = lowercase_ascii(settings.annotation.export_format);
    if (!is_annotation_format(settings.annotation.export_format)) {
        settings.annotation.export_format = "svg";
    }
}
}  // namespace pastit
