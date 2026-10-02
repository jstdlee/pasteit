#pragma once

#include "config/app_settings.hpp"

#include <functional>
#include <optional>

namespace pasteit {
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
    bool set_thinking(std::string_view id, bool thinking, std::string& error);
    // Adds back deleted built-in templates; edited ones keep their text.
    void restore_defaults();
    // A built-in template's name, prompt, temperature and thinking back to
    // the shipped values (enabled stays). False for user templates.
    bool reset_to_default(std::string_view id);
private:
    std::vector<PromptTemplate>& templates_;
    IdGenerator ids_;
};
}  // namespace pasteit
