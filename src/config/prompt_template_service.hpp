#pragma once

#include "config/app_settings.hpp"

#include <functional>
#include <optional>

namespace pastit {
class PromptTemplateService {
public:
    using IdGenerator = std::function<std::string()>;
    PromptTemplateService(std::vector<PromptTemplate>& templates, IdGenerator generator = {});
    std::optional<PromptTemplate> create(std::string name, std::string prompt, double temperature, std::string& error);
    std::optional<PromptTemplate> find(std::string_view id) const;
    bool update(std::string_view id, std::string name, std::string prompt, double temperature, std::string& error);
    bool erase(std::string_view id, std::string& error);
    std::optional<PromptTemplate> duplicate(std::string_view id, std::string& error);
    bool set_enabled(std::string_view id, bool enabled, std::string& error);
    void restore_defaults();
private:
    std::vector<PromptTemplate>& templates_;
    IdGenerator ids_;
};
}  // namespace pastit
