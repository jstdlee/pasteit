#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace pastit {

enum class UiLanguage { System, English, SimplifiedChinese };

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

enum class UiTheme { Dark = 0, Light = 1 };

struct AppSettings {
    int schema_version = 1;
    UiLanguage language = UiLanguage::System;
    float window_opacity = 0.94F;
    UiTheme theme = UiTheme::Dark;
    std::filesystem::path default_image_directory;
    std::filesystem::path default_text_directory;
    ProviderSettings djev;
    ProviderSettings general_llm;
    RendererSettings renderers;
    DownloadSettings downloads;
    HashSettings hash;
    TerminalSettings terminal;
    DateTimeSettings date_time;
    AnnotationSettings annotation;
    std::vector<PromptTemplate> prompt_templates;

    bool operator==(const AppSettings&) const = default;
};

std::vector<PromptTemplate> default_prompt_templates();
AppSettings default_settings();
void normalize_settings(AppSettings& settings);

}  // namespace pastit
