#pragma once

#include "config/hotkey.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace pasteit {

// Stored as numbers; keep the order.
enum class UiLanguage { System, English, SimplifiedChinese, Japanese, Korean };

struct ProviderSettings {
    std::string endpoint;
    std::string model_id;
    std::string api_key;

    bool operator==(const ProviderSettings&) const = default;
};

struct PromptTemplate {
    std::string id;
    std::string name;
    std::string system_prompt;
    double temperature = 0.2;
    bool enabled = true;
    bool built_in = false;
    // Off by default: reasoning models are asked to skip thinking so answers come fast.
    bool thinking = false;

    bool operator==(const PromptTemplate&) const = default;
};

struct RendererSettings {
    std::filesystem::path mermaid_cli_path;
    std::vector<std::string> mermaid_arguments;
    std::filesystem::path qrencode_path;
    std::string qr_error_correction = "M";
    int qr_margin = 2;
    int qr_scale = 6;

    bool operator==(const RendererSettings&) const = default;
};

struct DownloadSettings {
    std::filesystem::path resume_directory;
    bool keep_part_files = true;

    bool operator==(const DownloadSettings&) const = default;
};

struct HashSettings {
    std::vector<std::string> default_algorithms;

    bool operator==(const HashSettings&) const = default;
};

struct TerminalSettings {
    std::vector<std::string> command;
    std::string profile;

    bool operator==(const TerminalSettings&) const = default;
};

struct DateTimeSettings {
    std::string source_zone = "UTC";
    std::string target_zone = "UTC";
    bool use_24_hour_clock = true;

    bool operator==(const DateTimeSettings&) const = default;
};

struct AnnotationSettings {
    std::string export_format = "svg";
    std::filesystem::path save_directory;

    bool operator==(const AnnotationSettings&) const = default;
};

// A saved command pipeline offered as an action for matching content.
// applies_to lists shape names (csv, json, log, ...), "lines" for any text
// with several lines, or "any".
struct PipelineRecipe {
    std::string id;
    std::string name;
    std::string command;
    std::string applies_to = "lines";
    bool enabled = true;
    bool built_in = false;

    bool operator==(const PipelineRecipe&) const = default;
};

struct PipelineCustomCommand {
    std::string name;
    std::string command;

    bool operator==(const PipelineCustomCommand&) const = default;
};

struct PipelineSettings {
    // External programs pipelines may run (by argv, never via a shell).
    std::vector<std::string> allowed_tools{"gawk", "awk", "jq"};
    bool allow_any_program = false;
    std::vector<PipelineCustomCommand> custom_commands;
    std::vector<PipelineRecipe> recipes;

    bool operator==(const PipelineSettings&) const = default;
};

struct PrivacySettings {
    std::string replacement_style = "placeholder";  // placeholder | mask | fake | redact
    bool anonymize_before_llm = false;
    bool allow_page_fetch = false;
    std::vector<std::string> disabled_categories;
    std::vector<std::string> always_hide;  // custom words, e.g. client names
    std::vector<std::string> never_hide;

    bool operator==(const PrivacySettings&) const = default;
};

// Stored as numbers; System follows the desktop's light/dark preference.
enum class UiTheme { Dark = 0, Light = 1, TokyoNight = 2, System = 3 };

struct AppSettings {
    int schema_version = 1;
    UiLanguage language = UiLanguage::System;
    float window_opacity = 0.94F;
    UiTheme theme = UiTheme::Dark;
    // Global shortcut that opens the popup, canonical text (see config/hotkey.hpp).
    std::string global_hotkey{kDefaultHotkey};
    std::filesystem::path default_image_directory;
    std::filesystem::path default_text_directory;
    ProviderSettings djev;
    ProviderSettings general_llm;
    // Instructions for Prompt templates > Optimize with LLM (General LLM page).
    std::string prompt_optimizer_system;
    RendererSettings renderers;
    DownloadSettings downloads;
    HashSettings hash;
    TerminalSettings terminal;
    DateTimeSettings date_time;
    AnnotationSettings annotation;
    std::vector<PromptTemplate> prompt_templates;
    PipelineSettings pipelines;
    PrivacySettings privacy;

    bool operator==(const AppSettings&) const = default;
};

std::vector<PromptTemplate> default_prompt_templates();
std::vector<PipelineRecipe> default_pipeline_recipes();
AppSettings default_settings();
void normalize_settings(AppSettings& settings);

}  // namespace pasteit
