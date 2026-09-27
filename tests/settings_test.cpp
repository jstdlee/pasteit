#include "config/settings_store.hpp"
#include "djev/djev_client.hpp"
#include "platform/app_paths.hpp"

#include <cassert>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>

namespace {

class ScopedEnv {
public:
    ScopedEnv(const char* name, const std::filesystem::path& value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            previous_ = existing;
        }
        setenv(name, value.string().c_str(), 1);
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    ~ScopedEnv() {
        if (previous_.has_value()) {
            setenv(name_.c_str(), previous_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    std::optional<std::string> previous_;
};

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "pasteit-settings-test";
    std::filesystem::remove_all(root);
    pasteit::SettingsStore store(root / "settings.json");

    const auto missing = store.load();
    assert(!missing.loaded_from_disk);
    assert(!missing.settings.prompt_templates.empty());
    assert(missing.settings.djev.model_id == "typed-decisions");

    {
        ScopedEnv xdg_config("XDG_CONFIG_HOME", root / "xdg-config");
        ScopedEnv xdg_data("XDG_DATA_HOME", root / "xdg-data");
        ScopedEnv home("HOME", root / "home");
        assert(pasteit::app_settings_path() == pasteit::executable_directory() / "settings.json");
        assert(pasteit::app_data_dir() == pasteit::executable_directory() / "data");
    }

    auto settings = pasteit::default_settings();
    settings.language = pasteit::UiLanguage::SimplifiedChinese;
    settings.window_opacity = 0.72F;
    settings.default_image_directory = "/tmp/images";
    settings.default_text_directory = "/tmp/text";
    settings.djev = {.endpoint = "http://127.0.0.1:8011", .model_id = "djev", .api_key = "djev-key"};
    settings.general_llm = {.endpoint = "http://127.0.0.1:1234", .model_id = "chat", .api_key = "llm-key"};
    settings.renderers.mermaid_cli_path = "/tools/mmdc";
    settings.renderers.mermaid_arguments = {"--theme", "forest"};
    settings.renderers.qrencode_path = "/tools/qrencode";
    settings.renderers.qr_error_correction = "H";
    settings.renderers.qr_margin = 4;
    settings.renderers.qr_scale = 8;
    settings.downloads.resume_directory = root / "downloads";
    settings.downloads.keep_part_files = false;
    settings.hash.default_algorithms = {"sha512"};
    settings.terminal.command = {"kgx", "--working-directory"};
    settings.terminal.profile = "PasteIt";
    settings.date_time.source_zone = "UTC";
    settings.date_time.target_zone = "Asia/Singapore";
    settings.date_time.use_24_hour_clock = false;
    settings.annotation.export_format = "png";
    settings.annotation.save_directory = root / "annotations";
    settings.pipelines.allowed_tools = {"jq"};
    settings.pipelines.recipes = {{"r1", "Top words", "tr ' ' '\\n' | sort | uniq -c", "prose,lines", false, false}};
    settings.privacy.replacement_style = "mask";
    settings.privacy.anonymize_before_llm = true;
    settings.privacy.always_hide = {"Project Falcon"};
    std::string error;
    assert(store.save(settings, error));
    const auto loaded = store.load();
    assert(loaded.settings.pipelines == settings.pipelines);
    assert(loaded.settings.privacy == settings.privacy);
    assert(loaded.loaded_from_disk);
    assert(loaded.warning.empty());
    assert(std::fabs(loaded.settings.window_opacity - 0.72F) < 0.001F);
    assert(loaded.settings.language == pasteit::UiLanguage::SimplifiedChinese);
    assert(loaded.settings.djev.api_key == "djev-key");
    assert(loaded.settings.general_llm.api_key == "llm-key");
    assert(loaded.settings.default_image_directory == "/tmp/images");
    assert(loaded.settings.prompt_templates.size() == settings.prompt_templates.size());
    assert(loaded.settings.renderers.mermaid_cli_path == "/tools/mmdc");
    assert((loaded.settings.renderers.mermaid_arguments == std::vector<std::string>{"--theme", "forest"}));
    assert(loaded.settings.renderers.qrencode_path == "/tools/qrencode");
    assert(loaded.settings.renderers.qr_error_correction == "H");
    assert(loaded.settings.renderers.qr_margin == 4);
    assert(loaded.settings.renderers.qr_scale == 8);
    assert(loaded.settings.downloads.resume_directory == root / "downloads");
    assert(!loaded.settings.downloads.keep_part_files);
    assert((loaded.settings.hash.default_algorithms == std::vector<std::string>{"sha512"}));
    assert((loaded.settings.terminal.command == std::vector<std::string>{"kgx", "--working-directory"}));
    assert(loaded.settings.terminal.profile == "PasteIt");
    assert(loaded.settings.date_time.source_zone == "UTC");
    assert(loaded.settings.date_time.target_zone == "Asia/Singapore");
    assert(!loaded.settings.date_time.use_24_hour_clock);
    assert(loaded.settings.annotation.export_format == "svg");
    assert(loaded.settings.annotation.save_directory == root / "annotations");

    std::ofstream(root / "settings.json", std::ios::trunc) << R"({"schema_version":1})";
    const auto partial = store.load();
    assert(partial.loaded_from_disk);
    assert(!partial.djev_endpoint_saved && !partial.djev_model_saved && !partial.djev_api_key_saved);

    settings.window_opacity = 9.0F;
    assert(store.save(settings, error));
    assert(std::fabs(store.load().settings.window_opacity - 1.0F) < 0.001F);

    // The retired autojev default (port 8000) moves to the systemone service.
    settings.window_opacity = 1.0F;
    settings.djev.endpoint = "http://127.0.0.1:8000/v1/systemone";
    settings.djev.model_id = "autojev";
    assert(store.save(settings, error));
    const auto migrated = store.load().settings.djev;
    assert(migrated.endpoint == "http://127.0.0.1:8011" && migrated.model_id == "typed-decisions");
    settings.djev.endpoint = "http://10.0.0.5:8000/v1/systemone";
    assert(store.save(settings, error));
    assert(store.load().settings.djev.endpoint == "http://10.0.0.5:8000/v1/systemone");

    std::ofstream(root / "settings.json", std::ios::trunc) << "{broken";
    const auto malformed = store.load();
    assert(!malformed.warning.empty());
    assert(malformed.settings.schema_version == pasteit::default_settings().schema_version);
    assert(pasteit::DjevClient::normalize_endpoint("http://localhost:8011") == "http://localhost:8011/v1/systemone");
    assert(pasteit::DjevClient::normalize_endpoint("http://localhost:8011/v1/systemone") == "http://localhost:8011/v1/systemone");
    std::filesystem::remove_all(root);
}
