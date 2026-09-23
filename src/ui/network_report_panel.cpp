#include "ui/network_report_panel.hpp"

#include "ui/imgui_widgets.hpp"

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {

FastActionPanelModel build_network_report_panel_model(const NetworkReportState& state) {
    const auto& active = state.active();
    const auto report = state.copy_text();
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt Network Report##" + active.action_id, {680.0F, 460.0F});
    model.toolbar = {
        {.id = "copy_report", .label = "Copy report", .value = report},
        {.id = "copy_target", .label = "Copy target", .value = active.target},
    };
    model.status_text = active.error;
    model.primary_text = report;
    model.selectable_read_only_multiline = true;
    return model;
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_network_report_panel(const NetworkReportState& state, bool& open, bool& focus_pending) {
    auto model = build_network_report_panel_model(state);
    if (focus_pending) {
        ImGui::SetNextWindowFocus();
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    }
    ImGui::SetNextWindowSize(ImVec2(model.viewport.initial_width, model.viewport.initial_height), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(model.viewport.title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        if (focus_pending) {
            ImGui::SetWindowFocus();
            focus_pending = false;
        }
        for (std::size_t index = 0; index < model.toolbar.size(); ++index) {
            if (index != 0) ImGui::SameLine();
            const auto& command = model.toolbar[index];
            if (ImGui::Button(command.label.c_str())) ImGui::SetClipboardText(command.value.c_str());
        }
        if (!model.status_text.empty()) copyable_text(model.status_text, true);
        std::string body = model.primary_text;
        input_text_string("##network-report-body", body, true, ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
}
#endif

}  // namespace pastit
