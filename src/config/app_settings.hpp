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
};

struct PromptTemplate {
    std::string id;
    std::string name;
    std::string system_prompt;
    double temperature = 0.2;
    bool enabled = true;
    bool built_in = false;
};

struct RendererSettings {
    std::filesystem::path mermaid_cli_path;
    std::vector<std::string> mermaid_arguments;
    std::filesystem::path qrencode_path;
    std::string qr_error_correction = "M";
    int qr_margin = 2;
    int qr_scale = 6;
};

struct DownloadSettings {
    std::filesystem::path resume_directory;
    bool keep_part_files = true;
};

struct HashSettings {
    std::vector<std::string> default_algorithms;
};

struct TerminalSettings {
    std::vector<std::string> command;
    std::string profile;
};

struct DateTimeSettings {
    std::string source_zone = "UTC";
    std::string target_zone = "UTC";
    bool use_24_hour_clock = true;
};

struct AnnotationSettings {
    std::string export_format = "svg";
    std::filesystem::path save_directory;
};

struct AppSettings {
    int schema_version = 1;
    UiLanguage language = UiLanguage::System;
    float window_opacity = 0.94F;
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
    std::map<std::string, double> action_preferences;
};

std::vector<PromptTemplate> default_prompt_templates();
AppSettings default_settings();
void normalize_settings(AppSettings& settings);

}  // namespace pastit
