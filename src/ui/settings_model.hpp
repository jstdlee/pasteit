#pragma once
#include "config/app_settings.hpp"
#include <string>
namespace pastit {
class SettingsStore;
enum class SettingsSection{General,Paths,Djev,GeneralLlm,PromptTemplates,FastActions};
class SettingsModel{
public:explicit SettingsModel(const AppSettings& saved):original_(saved),working_(saved){}AppSettings& working(){return working_;}const AppSettings& working()const{return working_;}void cancel();bool save(AppSettings& destination,std::string& error);void reset(SettingsSection section);
private:AppSettings original_,working_;
};
bool save_settings_draft(const SettingsStore& store, const AppSettings& applied,
                         const AppSettings& draft, AppSettings& saved, std::string& error);
bool save_settings_on_exit(const SettingsStore& store, const AppSettings& applied,
                           const AppSettings& draft, std::string& error);
}
