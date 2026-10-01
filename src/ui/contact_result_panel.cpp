#include "ui/contact_result_panel.hpp"
#include "ui/theme.hpp"

#include "ui/imgui_widgets.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/multi_viewport.hpp"

#include <imgui.h>
#endif

namespace pasteit {

FastActionPanelModel build_contact_result_panel_model(const ContactResultState& state) {
    const auto& active = state.active();
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt Contact Result##" + active.action_id, {560.0F, 360.0F});
    model.toolbar = {
        {.id = "copy_all", .label = "Copy all", .value = state.copy_text()},
        {.id = "copy_json", .label = "Copy JSON", .value = active.json},
    };
    model.status_text = active.error;
    for (const auto& field : active.fields) {
        model.rows.push_back(FastActionPanelRow{
            .label = field.label.empty() ? field.kind : field.label,
            .value = field.value,
            .selectable = true,
            .copy_command = {.id = "copy_" + field.kind, .label = "Copy", .value = field.value},
        });
    }
    model.primary_text = state.copy_text();
    return model;
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
void draw_contact_result_panel(const ContactResultState& state, bool& open, bool& focus_pending) {
    auto model = build_contact_result_panel_model(state);
    if (begin_tool_window(model.viewport.title, &open,
                          ImVec2(model.viewport.initial_width, model.viewport.initial_height), &focus_pending)) {
        for (std::size_t index = 0; index < model.toolbar.size(); ++index) {
            if (index != 0) ImGui::SameLine();
            const auto& command = model.toolbar[index];
            if (!command.enabled) ImGui::BeginDisabled();
            if (ImGui::Button(command.label.c_str())) ImGui::SetClipboardText(command.value.c_str());
            if (!command.enabled) ImGui::EndDisabled();
        }
        if (!model.status_text.empty()) copyable_text(model.status_text, true);
        if (ImGui::BeginTable("contact-fields", 3,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0.0F, 0.0F))) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 1.2F);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 3.0F);
            ImGui::TableSetupColumn("Copy", ImGuiTableColumnFlags_WidthFixed, 80.0F);
            ImGui::TableHeadersRow();
            for (const auto& row : model.rows) {
                ImGui::PushID(row.copy_command.id.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                copyable_text(row.label);
                ImGui::TableNextColumn();
                copyable_text(row.value);
                ImGui::TableNextColumn();
                if (ImGui::Button(row.copy_command.label.c_str())) {
                    ImGui::SetClipboardText(row.copy_command.value.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}
#endif

}  // namespace pasteit
