#include "config/app_settings.hpp"

#include "ai/prompt_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace pasteit {
namespace {
std::filesystem::path home_path(std::string_view suffix) {
#if defined(_WIN32)
    const DWORD required = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (required > 1) {
        std::wstring home(required, L'\0');
        const DWORD copied = GetEnvironmentVariableW(L"USERPROFILE", home.data(), required);
        if (copied > 0 && copied < required) {
            home.resize(copied);
            return std::filesystem::path{home} / std::string{suffix};
        }
    }
    return std::filesystem::temp_directory_path() / std::string{suffix};
#else
    const char* home = std::getenv("HOME");
    return (home == nullptr ? std::filesystem::temp_directory_path() : std::filesystem::path{home}) / suffix;
#endif
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
        {.id="builtin-translate", .name="Translate", .system_prompt="Translate {text}. Source language: {source_language}. Target language: {target_language}.", .temperature=0.1, .enabled=true, .built_in=true},
        {.id="builtin-rewrite", .name="Rewrite", .system_prompt="Rewrite for clarity and fluency while preserving meaning: {text}", .temperature=0.7, .enabled=true, .built_in=true},
        {.id="builtin-summarize", .name="Summarize", .system_prompt="Summarize factually and concisely: {text}", .temperature=0.3, .enabled=true, .built_in=true},
        {.id="builtin-explain-text", .name="Explain text", .system_prompt="Explain this text clearly and concisely: {text}", .temperature=0.4, .enabled=true, .built_in=true},
        {.id="builtin-explain-code", .name="Explain code", .system_prompt="Explain what this code does, including important inputs, outputs, and edge cases: {text}", .temperature=0.2, .enabled=true, .built_in=true},
    };
}

std::vector<PipelineRecipe> default_pipeline_recipes() {
    return {
        {"builtin-count", "Count occurrences", "sort | uniq -c | sort -nr", "lines", true, true},
        {"builtin-unique", "Unique lines", "sort -u", "lines", true, true},
        {"builtin-line-count", "Line count", "wc -l", "lines", true, true},
        {"builtin-first-column", "First column", "cut -d , -f 1", "csv", true, true},
        {"builtin-sum-column", "Sum column 2", "awk -F, 'NR > 1 { s += $2 } END { print s }'", "csv", true, true},
        {"builtin-errors", "Errors only", "grep -i 'error|fail|fatal|exception'", "log", true, true},
        {"builtin-json-keys", "JSON keys", "jq -c 'if type == \"array\" then .[0] | keys else keys end'", "json,ndjson", true, true},
    };
}

AppSettings default_settings() {
    AppSettings settings;
    settings.default_image_directory = home_path("Pictures");
    settings.default_text_directory = home_path("Documents");
    settings.djev.endpoint = "http://127.0.0.1:8011";
    settings.djev.model_id = "typed-decisions";
    settings.downloads.resume_directory = home_path("Downloads");
    settings.hash.default_algorithms = {"sha256", "sha512"};
#if defined(_WIN32)
    settings.terminal.command = {"cmd.exe"};
#else
    settings.terminal.command = {"x-terminal-emulator"};
#endif
    settings.annotation.save_directory = home_path("Pictures");
    settings.prompt_templates = default_prompt_templates();
    settings.prompt_optimizer_system = default_prompt_optimizer_system();
    settings.pipelines.recipes = default_pipeline_recipes();
    return settings;
}

void normalize_settings(AppSettings& settings) {
    settings.window_opacity = std::clamp(settings.window_opacity, 0.55F, 1.0F);
    // Built-in templates still on the old one-size default (0.2/0.3) move to
    // the per-purpose default; edited temperatures are kept.
    for (auto& prompt : settings.prompt_templates) {
        if (!prompt.built_in || (std::fabs(prompt.temperature - 0.2) > 1e-6 && std::fabs(prompt.temperature - 0.3) > 1e-6)) continue;
        for (const auto& fresh : default_prompt_templates()) {
            if (fresh.id == prompt.id) prompt.temperature = fresh.temperature;
        }
    }
    for (auto& prompt : settings.prompt_templates) prompt.temperature = std::clamp(prompt.temperature, 0.0, 2.0);
    // The retired autojev service (port 8000) was the saved default before the
    // structured systemone endpoint; move untouched old defaults forward.
    if ((settings.djev.endpoint == "http://127.0.0.1:8000/v1/systemone" ||
         settings.djev.endpoint == "http://127.0.0.1:8000/v1/autojev" ||
         settings.djev.endpoint == "http://127.0.0.1:8000") &&
        (settings.djev.model_id == "autojev" || settings.djev.model_id == "jev-latest")) {
        settings.djev.endpoint = "http://127.0.0.1:8011";
        settings.djev.model_id = "typed-decisions";
    }
    if (settings.prompt_optimizer_system.find_first_not_of(" \t\r\n") == std::string::npos) {
        settings.prompt_optimizer_system = default_prompt_optimizer_system();
    }
    if (settings.schema_version < 1) settings.schema_version = 1;
    const auto hotkey = parse_hotkey(settings.global_hotkey);
    settings.global_hotkey = format_hotkey(hotkey.value_or(default_hotkey()));
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
}  // namespace pasteit
