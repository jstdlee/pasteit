#include "config/prompt_template_service.hpp"

#include <cassert>
#include <algorithm>
#include <string>

namespace {

auto find_prompt(std::vector<pasteit::PromptTemplate>& templates, const std::string& id) {
    return std::find_if(templates.begin(), templates.end(), [&](const auto& value) {
        return value.id == id;
    });
}

auto find_prompt_by_name(std::vector<pasteit::PromptTemplate>& templates, const std::string& name) {
    return std::find_if(templates.begin(), templates.end(), [&](const auto& value) {
        return value.name == name;
    });
}

}

int main() {
    auto settings = pasteit::default_settings();

    const auto explain_text = find_prompt_by_name(settings.prompt_templates, "Explain text");
    const auto explain_code = find_prompt_by_name(settings.prompt_templates, "Explain code");
    assert(explain_text != settings.prompt_templates.end());
    assert(explain_code != settings.prompt_templates.end());
    assert(explain_text->id == "builtin-explain-text");
    assert(explain_code->id == "builtin-explain-code");
    assert(explain_text->built_in);
    assert(explain_code->built_in);
    assert(explain_text->system_prompt.find("{text}") != std::string::npos);
    assert(explain_code->system_prompt.find("{text}") != std::string::npos);

    int next = 0;
    pasteit::PromptTemplateService service(settings.prompt_templates, [&] { return "id-" + std::to_string(++next); });

    const auto seeded_count = settings.prompt_templates.size();
    service.restore_defaults();
    assert(settings.prompt_templates.size() == seeded_count);

    std::string error;
    const auto custom = service.create("Polish", "Polish {text}", 0.3, error);
    assert(custom.has_value());
    assert(service.find(custom->id).has_value());
    assert(service.set_enabled(custom->id, false, error));
    assert(!service.find(custom->id)->enabled);
    assert(service.update(custom->id, "Polish prose", "Improve {text}", 0.4, error));
    const auto duplicate = service.duplicate(custom->id, error);
    assert(duplicate.has_value() && duplicate->id != custom->id);
    assert(service.erase(custom->id, error));
    assert(!service.find(custom->id).has_value());

    assert(service.erase("builtin-explain-text", error));
    assert(!service.find("builtin-explain-text").has_value());
    pasteit::PromptTemplateService second_service(settings.prompt_templates, [&] { return "id-" + std::to_string(++next); });
    assert(!second_service.find("builtin-explain-text").has_value());
    const auto before_restore = settings.prompt_templates.size();
    second_service.restore_defaults();
    assert(settings.prompt_templates.size() == before_restore + 1);
    assert(second_service.find("builtin-explain-text").has_value());
    assert(service.find(duplicate->id).has_value());
    assert(!service.create("", "prompt", 0.2, error).has_value());

    assert(second_service.update("builtin-explain-code", "Explain source code", "Custom explanation for {text}", 0.4, error));
    const auto edited_count = settings.prompt_templates.size();
    second_service.restore_defaults();
    assert(settings.prompt_templates.size() == edited_count);
    const auto edited_code = find_prompt(settings.prompt_templates, "builtin-explain-code");
    assert(edited_code != settings.prompt_templates.end());
    assert(edited_code->name == "Explain source code");
    assert(edited_code->system_prompt == "Custom explanation for {text}");
}
