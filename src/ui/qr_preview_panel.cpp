#include "ui/qr_preview_panel.hpp"

#include "ui/imgui_widgets.hpp"

#include <filesystem>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {

FastActionPanelModel build_qr_preview_panel_model(const RendererResultState& state) {
    const auto& active = state.active();
    const auto payload = active.payload.empty() ? active.source : active.payload;
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt QR Preview##" + active.action_id, {520.0F, 420.0F});
    model.toolbar = {
        {.id = "copy_payload", .label = "Copy payload", .value = payload, .kind = PanelCommandKind::CopyText},
        {.id = "save_payload", .label = "Save payload", .value = payload, .kind = PanelCommandKind::SaveText},
        {.id = "source", .label = "Source", .value = payload, .kind = PanelCommandKind::ShowSource},
        {.id = "preview", .label = "Preview", .value = active.output_path ? active.output_path->string() : active.error,
         .enabled = active.available && active.output_path.has_value() && std::filesystem::is_regular_file(*active.output_path),
         .kind = PanelCommandKind::ShowPreview},
    };
    model.preview_available = active.available && active.output_path.has_value() &&
                              std::filesystem::is_regular_file(*active.output_path);
    if (model.preview_available) {
        model.toolbar.push_back({.id = "save_rendered", .label = "Save rendered PNG",
                                 .value = active.output_path->string(), .kind = PanelCommandKind::SaveRenderedOutput});
    }
    model.status_text = active.error;
    model.primary_text = payload;
    model.fallback_text = payload;
    return model;
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_qr_preview_panel(const RendererResultState& state, RendererPreviewPanelState& panel,
                           bool& open, bool& focus_pending,
                           const std::function<bool(const std::filesystem::path&)>& open_path) {
    auto model = build_qr_preview_panel_model(state);
    panel.poll();
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
            if (index != 0 && index != 2) ImGui::SameLine();
            const auto& command = model.toolbar[index];
            if (!command.enabled) ImGui::BeginDisabled();
            if (ImGui::Button(command.label.c_str())) {
                if (command.kind == PanelCommandKind::CopyText) {
                    ImGui::SetClipboardText(command.value.c_str());
                } else if (command.kind == PanelCommandKind::SaveText) {
                    panel.save(command, ".txt");
                } else if (command.kind == PanelCommandKind::ShowSource) {
                    panel.select_view(command, model.preview_available);
                } else if (command.kind == PanelCommandKind::ShowPreview &&
                           panel.select_view(command, model.preview_available)) {
                    panel.open_preview(command.value, open_path);
                } else if (command.kind == PanelCommandKind::SaveRenderedOutput) {
                    panel.save(command, ".png");
                }
            }
            if (!command.enabled) ImGui::EndDisabled();
        }
        input_text_string("Destination", panel.destination);
        ImGui::Checkbox("Confirm overwrite", &panel.confirm_overwrite);
        if (!panel.status_text.empty()) copyable_text(panel.status_text, true);
        if (!model.preview_available) {
            copyable_text(model.status_text.empty() ? "QR renderer unavailable; payload is shown below." : model.status_text,
                          true);
        } else if (panel.mode == RendererPreviewPanelState::Mode::Preview) {
            copyable_text("Rendered PNG opens in the system viewer; payload remains below.", true);
            copyable_text(state.active().output_path->string(), true);
        }
        std::string editable = model.primary_text;
        input_text_string("##qr-payload", editable, true, ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::End();
}
#endif

}  // namespace pastit
