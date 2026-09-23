#include "ui/mermaid_preview_panel.hpp"

#include "ui/imgui_widgets.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {
FastActionPanelModel build_mermaid_preview_panel_model(const RendererResultState& state) {
    const auto& active = state.active();
    const auto source = state.copy_text();
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt Mermaid Preview##" + active.action_id, {720.0F, 520.0F});
    model.toolbar = {
        {.id = "source", .label = "Source", .value = source, .kind = PanelCommandKind::ShowSource},
        {.id = "preview", .label = "Preview", .value = active.output_path ? active.output_path->string() : active.error,
         .enabled = active.available && active.output_path.has_value() && std::filesystem::is_regular_file(*active.output_path),
         .kind = PanelCommandKind::ShowPreview},
        {.id = "copy_source", .label = "Copy source", .value = source, .kind = PanelCommandKind::CopyText},
        {.id = "save_source", .label = "Save source", .value = source, .kind = PanelCommandKind::SaveText},
    };
    model.preview_available = active.available && active.output_path.has_value() &&
                              std::filesystem::is_regular_file(*active.output_path);
    if (model.preview_available) {
        model.toolbar.push_back({.id = "save_rendered",
                                 .label = active.output_path->extension() == ".png" ? "Save image" : "Save rendered HTML",
                                 .value = active.output_path->string(), .kind = PanelCommandKind::SaveRenderedOutput});
    }
    model.status_text = active.error;
    model.primary_text = source;
    model.fallback_text = source;
    if ((active.status == RendererResultStatus::Failed || active.status == RendererResultStatus::Unavailable) &&
        active.generated_mermaid && !active.generated_mermaid->original_clipboard_source.empty()) {
        const auto& original = active.generated_mermaid->original_clipboard_source;
        model.toolbar.push_back({.id = "copy_original", .label = "Copy original", .value = original,
                                 .kind = PanelCommandKind::CopyText});
        model.rows.push_back({.label = "Original clipboard source", .value = original,
                              .copy_command = {.id = "copy_original", .label = "Copy original", .value = original,
                                               .kind = PanelCommandKind::CopyText}});
    }
    return model;
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)
void draw_mermaid_preview_panel(const RendererResultState& state, RendererPreviewPanelState& panel,
                                bool& open, bool& focus_pending,
                                unsigned int texture_id, int texture_width, int texture_height,
                                const std::function<bool(const std::filesystem::path&)>& open_path) {
    auto model = build_mermaid_preview_panel_model(state);
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
            if (index != 0) ImGui::SameLine();
            const auto& command = model.toolbar[index];
            if (!command.enabled) ImGui::BeginDisabled();
            if (ImGui::Button(command.label.c_str())) {
                if (command.kind == PanelCommandKind::ShowSource) {
                    panel.select_view(command, model.preview_available);
                } else if (command.kind == PanelCommandKind::ShowPreview &&
                           panel.select_view(command, model.preview_available)) {
                    if (state.active().output_path->extension() != ".png") {
                        panel.open_preview(command.value, open_path);
                    }
                } else if (command.kind == PanelCommandKind::CopyText) {
                    ImGui::SetClipboardText(command.value.c_str());
                } else if (command.kind == PanelCommandKind::SaveText) {
                    panel.save(command, ".mmd");
                } else if (command.kind == PanelCommandKind::SaveRenderedOutput) {
                    panel.save(command, state.active().output_path->extension() == ".png" ? ".png" : ".html");
                }
            }
            if (!command.enabled) ImGui::EndDisabled();
        }
        input_text_string("Destination", panel.destination);
        ImGui::Checkbox("Confirm overwrite", &panel.confirm_overwrite);
        if (!panel.status_text.empty()) copyable_text(panel.status_text, true);
        if (!model.preview_available) {
            copyable_text(model.status_text.empty() ? "Mermaid renderer unavailable; source is shown below." : model.status_text,
                          true);
        } else if (panel.mode == RendererPreviewPanelState::Mode::Preview) {
            if (state.active().output_path->extension() == ".png" && texture_id != 0 &&
                texture_width > 0 && texture_height > 0) {
                ImGui::BeginChild("mermaid-inline-preview", ImVec2(0.0F, 280.0F), ImGuiChildFlags_Borders);
                const auto available = ImGui::GetContentRegionAvail();
                const float scale = std::min({1.0F, available.x / static_cast<float>(texture_width),
                                              available.y / static_cast<float>(texture_height)});
                ImGui::Image((ImTextureID)(intptr_t)texture_id,
                             ImVec2(texture_width * scale, texture_height * scale));
                ImGui::EndChild();
            } else {
                copyable_text("Offline HTML opens in your browser; source remains below.", true);
                copyable_text(state.active().output_path->string(), true);
            }
        }
        std::string editable = model.primary_text;
        input_text_string("##mermaid-source", editable, true, ImGuiInputTextFlags_ReadOnly);
        for (const auto& row : model.rows) {
            copyable_text(row.label, true);
            std::string original = row.value;
            input_text_string("##mermaid-original-source", original, true, ImGuiInputTextFlags_ReadOnly);
        }
    }
    ImGui::End();
}
#endif

}  // namespace pastit
