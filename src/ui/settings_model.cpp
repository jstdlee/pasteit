#include "ui/settings_model.hpp"
#include "config/settings_store.hpp"
namespace pastit {
void SettingsModel::cancel(){working_=original_;}
bool SettingsModel::save(AppSettings& destination,std::string& error){
 auto valid_dir=[&](const auto& path){std::error_code ec;return path.empty()||std::filesystem::is_directory(path,ec);};
 if(!valid_dir(working_.default_image_directory)||!valid_dir(working_.default_text_directory)){error="A default save path is not a directory.";return false;}
 if(working_.djev.endpoint.empty()||working_.djev.model_id.empty()){error="Djev endpoint and model are required.";return false;}
 if(working_.general_llm.endpoint.empty()!=working_.general_llm.model_id.empty()){error="General LLM endpoint and model must be configured together.";return false;}
 normalize_settings(working_);destination=working_;original_=working_;error.clear();return true;
}
void SettingsModel::reset(SettingsSection section){const auto defaults=default_settings();switch(section){case SettingsSection::General:working_.language=defaults.language;working_.window_opacity=defaults.window_opacity;working_.theme=defaults.theme;break;case SettingsSection::Paths:working_.default_image_directory=defaults.default_image_directory;working_.default_text_directory=defaults.default_text_directory;break;case SettingsSection::Djev:working_.djev=defaults.djev;break;case SettingsSection::GeneralLlm:working_.general_llm=defaults.general_llm;break;case SettingsSection::PromptTemplates:working_.prompt_templates=defaults.prompt_templates;break;case SettingsSection::FastActions:working_.renderers=defaults.renderers;working_.downloads=defaults.downloads;working_.hash=defaults.hash;working_.terminal=defaults.terminal;working_.date_time=defaults.date_time;working_.annotation=defaults.annotation;break;}}

bool save_settings_draft(const SettingsStore& store, const AppSettings& applied,
                         const AppSettings& draft, AppSettings& saved, std::string& error) {
    SettingsModel validator(applied);
    validator.working() = draft;
    if (!validator.save(saved, error)) {
        return false;
    }
    return store.save(saved, error);
}

bool save_settings_on_exit(const SettingsStore& store, const AppSettings& applied,
                           const AppSettings& draft, std::string& error) {
    AppSettings saved = applied;
    if (!save_settings_draft(store, applied, draft, saved, error)) {
        const std::string validation_error = error;
        std::string save_error;
        if (!store.save(applied, save_error)) {
            error = validation_error + " Failed to save applied settings: " + save_error;
        }
        return false;
    }
    return true;
}
}
