#include "ui/prompt_parameters.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/imgui_widgets.hpp"
#include "ui/theme.hpp"

#include <imgui.h>
#endif

namespace pasteit {

namespace {
constexpr const char* kLanguages[] = {
    "auto", "English", "Simplified Chinese", "Traditional Chinese", "Japanese", "Korean", "French", "German",
    "Spanish", "Portuguese", "Italian", "Russian", "Arabic", "Hindi", "Vietnamese", "Thai", "Indonesian", "Malay",
};
}  // namespace

bool is_language_parameter(std::string_view name) {
    return name.find("language") != std::string_view::npos || name == "lang" || name == "from" || name == "to";
}

std::string default_prompt_parameter(std::string_view name) {
    if (name == "source_language") return "auto";
    if (name == "target_language") return "Simplified Chinese";
    return {};
}

void fill_prompt_parameter_defaults(const std::vector<std::string>& names, std::map<std::string, std::string>& values) {
    for (const auto& name : names) {
        if (!values.contains(name)) values[name] = default_prompt_parameter(name);
    }
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
bool draw_prompt_parameter_fields(const char* id, const std::vector<std::string>& names,
                                  std::map<std::string, std::string>& values) {
    bool changed = false;
    if (names.empty() || !begin_form(id, 150.0F)) return false;
    for (const auto& name : names) {
        auto& value = values[name];
        form_row(name);
        ImGui::PushID(name.c_str());
        if (is_language_parameter(name)) {
            // Free text plus a dropdown of common languages.
            const float arrow = ImGui::GetFrameHeight();
            changed |= input_text_string("##value", value, false, 0, 0.0F,
                                         ImGui::GetContentRegionAvail().x - arrow - ImGui::GetStyle().ItemSpacing.x);
            ImGui::SameLine();
            if (ImGui::BeginCombo("##languages", nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft |
                                                            ImGuiComboFlags_HeightLarge)) {
                for (const auto* language : kLanguages) {
                    if (std::string_view{language} == "auto" && name.find("target") != std::string::npos) continue;
                    if (ImGui::Selectable(language, value == language)) {
                        value = language;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            changed |= input_text_string("##value", value);
        }
        ImGui::PopID();
    }
    end_form();
    return changed;
}
#endif

}  // namespace pasteit
