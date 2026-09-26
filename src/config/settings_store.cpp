#include "config/settings_store.hpp"

#include "platform/app_paths.hpp"
#include "util/json.hpp"
#include "util/path_utf8.hpp"
#include "util/replace_file.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace pastit {
namespace {
std::string string_value(const JsonValue* value, std::string fallback = {}) {
    return value != nullptr && value->string() != nullptr ? *value->string() : std::move(fallback);
}
bool bool_value(const JsonValue* value, bool fallback) {
    const auto parsed = value == nullptr ? std::optional<bool>{} : value->boolean();
    return parsed.value_or(fallback);
}
double number_value(const JsonValue* value, double fallback) {
    const auto parsed = value == nullptr ? std::optional<double>{} : value->number();
    return parsed.value_or(fallback);
}
int int_value(const JsonValue* value, int fallback) {
    return static_cast<int>(number_value(value, static_cast<double>(fallback)));
}
std::vector<std::string> string_array_value(const JsonValue* value, std::vector<std::string> fallback) {
    if (value == nullptr || value->array() == nullptr) return fallback;
    std::vector<std::string> result;
    for (const auto& item : *value->array()) {
        if (const auto* text = item.string(); text != nullptr) {
            result.push_back(*text);
        }
    }
    return result.empty() ? fallback : result;
}
ProviderSettings provider_from(const JsonValue* value, ProviderSettings fallback) {
    if (value == nullptr || value->object() == nullptr) return fallback;
    fallback.endpoint = string_value(value->get("endpoint"), fallback.endpoint);
    fallback.model_id = string_value(value->get("model_id"), fallback.model_id);
    fallback.api_key = string_value(value->get("api_key"), fallback.api_key);
    return fallback;
}
void write_provider(std::ostream& out, const ProviderSettings& value) {
    out << "{\"endpoint\":" << json_quote(value.endpoint) << ",\"model_id\":" << json_quote(value.model_id)
        << ",\"api_key\":" << json_quote(value.api_key) << '}';
}
void write_string_array(std::ostream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ',';
        out << json_quote(values[i]);
    }
    out << ']';
}

}  // namespace

std::filesystem::path SettingsStore::default_path() {
    return app_settings_path();
}

SettingsLoadResult SettingsStore::load() const {
    auto result = SettingsLoadResult{};
    result.settings = default_settings();
    std::ifstream input(path_, std::ios::binary);
    if (!input) return result;
    std::ostringstream buffer; buffer << input.rdbuf();
    const auto root = parse_json(buffer.str());
    if (!root || root->object() == nullptr) {
        result.warning = "Settings file is malformed; defaults were loaded.";
        return result;
    }
    result.loaded_from_disk = true;
    auto& s = result.settings;
    s.schema_version = static_cast<int>(number_value(root->get("schema_version"), s.schema_version));
    s.language = static_cast<UiLanguage>(static_cast<int>(number_value(root->get("language"), 0)));
    s.window_opacity = static_cast<float>(number_value(root->get("window_opacity"), s.window_opacity));
    s.theme = number_value(root->get("theme"), 0) == 1 ? UiTheme::Light : UiTheme::Dark;
    s.default_image_directory = path_from_utf8_string(string_value(
        root->get("default_image_directory"), path_to_utf8_string(s.default_image_directory)));
    s.default_text_directory = path_from_utf8_string(string_value(
        root->get("default_text_directory"), path_to_utf8_string(s.default_text_directory)));
    if (const auto* provider=root->get("djev");provider&&provider->object()) {
        result.djev_endpoint_saved=provider->get("endpoint")&&provider->get("endpoint")->string()&& !provider->get("endpoint")->string()->empty();
        result.djev_model_saved=provider->get("model_id")&&provider->get("model_id")->string()&& !provider->get("model_id")->string()->empty();
        result.djev_api_key_saved=provider->get("api_key")&&provider->get("api_key")->string()&& !provider->get("api_key")->string()->empty();
    }
    s.djev = provider_from(root->get("djev"), s.djev);
    s.general_llm = provider_from(root->get("general_llm"), s.general_llm);
    if (const auto* renderers = root->get("renderers"); renderers && renderers->object()) {
        s.renderers.mermaid_cli_path = path_from_utf8_string(string_value(
            renderers->get("mermaid_cli_path"), path_to_utf8_string(s.renderers.mermaid_cli_path)));
        s.renderers.mermaid_arguments = string_array_value(renderers->get("mermaid_arguments"),
                                                           s.renderers.mermaid_arguments);
        s.renderers.qrencode_path = path_from_utf8_string(string_value(
            renderers->get("qrencode_path"), path_to_utf8_string(s.renderers.qrencode_path)));
        s.renderers.qr_error_correction = string_value(renderers->get("qr_error_correction"),
                                                       s.renderers.qr_error_correction);
        s.renderers.qr_margin = int_value(renderers->get("qr_margin"), s.renderers.qr_margin);
        s.renderers.qr_scale = int_value(renderers->get("qr_scale"), s.renderers.qr_scale);
    }
    if (const auto* downloads = root->get("downloads"); downloads && downloads->object()) {
        s.downloads.resume_directory = path_from_utf8_string(string_value(
            downloads->get("resume_directory"), path_to_utf8_string(s.downloads.resume_directory)));
        s.downloads.keep_part_files = bool_value(downloads->get("keep_part_files"), s.downloads.keep_part_files);
    }
    if (const auto* hash = root->get("hash"); hash && hash->object()) {
        s.hash.default_algorithms = string_array_value(hash->get("default_algorithms"),
                                                       s.hash.default_algorithms);
    }
    if (const auto* terminal = root->get("terminal"); terminal && terminal->object()) {
        s.terminal.command = string_array_value(terminal->get("command"), s.terminal.command);
        s.terminal.profile = string_value(terminal->get("profile"), s.terminal.profile);
    }
    if (const auto* date_time = root->get("date_time"); date_time && date_time->object()) {
        s.date_time.source_zone = string_value(date_time->get("source_zone"), s.date_time.source_zone);
        s.date_time.target_zone = string_value(date_time->get("target_zone"), s.date_time.target_zone);
        s.date_time.use_24_hour_clock = bool_value(date_time->get("use_24_hour_clock"),
                                                   s.date_time.use_24_hour_clock);
    }
    if (const auto* annotation = root->get("annotation"); annotation && annotation->object()) {
        s.annotation.export_format = string_value(annotation->get("export_format"), s.annotation.export_format);
        s.annotation.save_directory = path_from_utf8_string(string_value(
            annotation->get("save_directory"), path_to_utf8_string(s.annotation.save_directory)));
    }
    if (const auto* templates = root->get("prompt_templates"); templates && templates->array()) {
        s.prompt_templates.clear();
        for (const auto& item : *templates->array()) {
            if (!item.object()) continue;
            PromptTemplate value;
            value.id = string_value(item.get("id"));
            value.name = string_value(item.get("name"));
            value.system_prompt = string_value(item.get("system_prompt"));
            value.temperature = number_value(item.get("temperature"), 0.2);
            value.enabled = bool_value(item.get("enabled"), true);
            value.built_in = bool_value(item.get("built_in"), false);
            if (!value.id.empty() && !value.name.empty() && !value.system_prompt.empty()) s.prompt_templates.push_back(std::move(value));
        }
    }
    normalize_settings(s);
    return result;
}

bool SettingsStore::save(const AppSettings& source, std::string& error) const {
    auto settings = source; normalize_settings(settings);
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) { error = ec.message(); return false; }
    auto temporary = path_;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not open temporary settings file."; return false; }
    out << "{\n\"schema_version\":" << settings.schema_version
        << ",\n\"language\":" << static_cast<int>(settings.language)
        << ",\n\"window_opacity\":" << settings.window_opacity
        << ",\n\"theme\":" << static_cast<int>(settings.theme)
        << ",\n\"default_image_directory\":" << json_quote(path_to_utf8_string(settings.default_image_directory))
        << ",\n\"default_text_directory\":" << json_quote(path_to_utf8_string(settings.default_text_directory))
        << ",\n\"djev\":"; write_provider(out, settings.djev);
    out << ",\n\"general_llm\":"; write_provider(out, settings.general_llm);
    out << ",\n\"renderers\":{\"mermaid_cli_path\":" << json_quote(path_to_utf8_string(settings.renderers.mermaid_cli_path))
        << ",\"mermaid_arguments\":";
    write_string_array(out, settings.renderers.mermaid_arguments);
    out << ",\"qrencode_path\":" << json_quote(path_to_utf8_string(settings.renderers.qrencode_path))
        << ",\"qr_error_correction\":" << json_quote(settings.renderers.qr_error_correction)
        << ",\"qr_margin\":" << settings.renderers.qr_margin
        << ",\"qr_scale\":" << settings.renderers.qr_scale << '}';
    out << ",\n\"downloads\":{\"resume_directory\":" << json_quote(path_to_utf8_string(settings.downloads.resume_directory))
        << ",\"keep_part_files\":" << (settings.downloads.keep_part_files ? "true" : "false") << '}';
    out << ",\n\"hash\":{\"default_algorithms\":";
    write_string_array(out, settings.hash.default_algorithms);
    out << '}';
    out << ",\n\"terminal\":{\"command\":";
    write_string_array(out, settings.terminal.command);
    out << ",\"profile\":" << json_quote(settings.terminal.profile) << '}';
    out << ",\n\"date_time\":{\"source_zone\":" << json_quote(settings.date_time.source_zone)
        << ",\"target_zone\":" << json_quote(settings.date_time.target_zone)
        << ",\"use_24_hour_clock\":" << (settings.date_time.use_24_hour_clock ? "true" : "false") << '}';
    out << ",\n\"annotation\":{\"export_format\":" << json_quote(settings.annotation.export_format)
        << ",\"save_directory\":" << json_quote(path_to_utf8_string(settings.annotation.save_directory)) << '}';
    out << ",\n\"prompt_templates\":[";
    for (std::size_t i=0; i<settings.prompt_templates.size(); ++i) {
        const auto& t=settings.prompt_templates[i]; if (i) out << ',';
        out << "{\"id\":" << json_quote(t.id) << ",\"name\":" << json_quote(t.name)
            << ",\"system_prompt\":" << json_quote(t.system_prompt) << ",\"temperature\":" << t.temperature
            << ",\"enabled\":" << (t.enabled?"true":"false") << ",\"built_in\":" << (t.built_in?"true":"false") << '}';
    }
    out << "]\n}\n";
    out.flush();
    if (!out) { error = "Could not write settings file."; return false; }
    out.close();
    if (!out) { error = "Could not close settings file."; return false; }
    replace_file(temporary, path_, ec);
    if (ec) { std::filesystem::remove(temporary); error = ec.message(); return false; }
    error.clear(); return true;
}
}  // namespace pastit
