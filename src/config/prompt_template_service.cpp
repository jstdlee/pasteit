#include "config/prompt_template_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>

namespace pasteit {
namespace {
std::string random_id() {
    static std::atomic<unsigned long long> sequence{0};
    return "prompt-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++sequence);
}

bool has_template(const std::vector<PromptTemplate>& templates, const PromptTemplate& value) {
    return std::any_of(templates.begin(), templates.end(), [&](const auto& existing) {
        return existing.id == value.id || existing.name == value.name;
    });
}
}
PromptTemplateService::PromptTemplateService(std::vector<PromptTemplate>& templates, IdGenerator generator)
    : templates_(templates), ids_(generator ? std::move(generator) : IdGenerator{random_id}) {}

std::optional<PromptTemplate> PromptTemplateService::find(std::string_view id) const {
    const auto it=std::find_if(templates_.begin(),templates_.end(),[&](const auto& t){return t.id==id;});
    return it==templates_.end()?std::nullopt:std::optional<PromptTemplate>{*it};
}
std::optional<PromptTemplate> PromptTemplateService::create(std::string name,std::string prompt,double temperature,std::string& error) {
    if(name.empty()||prompt.empty()){error="Name and prompt are required.";return std::nullopt;}
    PromptTemplate value{.id=ids_(),.name=std::move(name),.system_prompt=std::move(prompt),.temperature=temperature};
    if(value.id.empty()||find(value.id)){error="Template ID is empty or duplicated.";return std::nullopt;}
    templates_.push_back(value);error.clear();return value;
}
bool PromptTemplateService::update(std::string_view id,std::string name,std::string prompt,double temperature,std::string& error){
    if(name.empty()||prompt.empty()){error="Name and prompt are required.";return false;}
    auto it=std::find_if(templates_.begin(),templates_.end(),[&](const auto&t){return t.id==id;});
    if(it==templates_.end()){error="Template not found.";return false;}
    it->name=std::move(name);it->system_prompt=std::move(prompt);it->temperature=temperature;error.clear();return true;
}
bool PromptTemplateService::erase(std::string_view id,std::string& error){
    const std::string id_copy{id};
    const auto old=templates_.size();std::erase_if(templates_,[&](const auto&t){return t.id==id_copy;});
    if(old==templates_.size()){error="Template not found.";return false;}error.clear();return true;
}
std::optional<PromptTemplate> PromptTemplateService::duplicate(std::string_view id,std::string& error){
    auto source=find(id);if(!source){error="Template not found.";return std::nullopt;}
    source->id=ids_();source->built_in=false;
    if(source->id.empty()||find(source->id)){error="Template ID is empty or duplicated.";return std::nullopt;}
    templates_.push_back(*source);error.clear();return source;
}
bool PromptTemplateService::set_enabled(std::string_view id,bool enabled,std::string& error){
    auto it=std::find_if(templates_.begin(),templates_.end(),[&](const auto&t){return t.id==id;});
    if(it==templates_.end()){error="Template not found.";return false;}it->enabled=enabled;error.clear();return true;
}
bool PromptTemplateService::set_thinking(std::string_view id,bool thinking,std::string& error){
    auto it=std::find_if(templates_.begin(),templates_.end(),[&](const auto&t){return t.id==id;});
    if(it==templates_.end()){error="Template not found.";return false;}it->thinking=thinking;error.clear();return true;
}
void PromptTemplateService::restore_defaults(){
    for(auto value:default_prompt_templates()){
        if(has_template(templates_, value)) continue;
        templates_.push_back(std::move(value));
    }
}
}  // namespace pasteit
