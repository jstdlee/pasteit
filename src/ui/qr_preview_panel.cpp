#include "ui/qr_preview_panel.hpp"
#include "ui/theme.hpp"
#include "ui/icons.hpp"
#include "ui/localization.hpp"

#include "ui/imgui_widgets.hpp"

#include <algorithm>
#include <cstdint>
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
                           unsigned int texture_id, int texture_width, int texture_height,
                           const std::function<bool(const std::filesystem::path&)>& open_path) {
    auto model = build_qr_preview_panel_model(state);
    panel.poll();
    if (begin_tool_window(model.viewport.title, &open, ImVec2(440.0F, 540.0F), &focus_pending)) {
        const auto& p = palette();
        const auto find = [&](std::string_view id) -> const FastActionPanelCommand* {
            for (const auto& command : model.toolbar) if (command.id == id) return &command;
            return nullptr;
        };
        // The code fills the window above the payload line and the buttons.
        const float reserved = footer_height() + ImGui::GetTextLineHeightWithSpacing() * 2.0F;
        const auto available = ImGui::GetContentRegionAvail();
        if (model.preview_available && texture_id != 0 && texture_width > 0 && texture_height > 0) {
            const float side = std::max(64.0F, std::min(available.x, available.y - reserved));
            const float scale = side / static_cast<float>(std::max(texture_width, texture_height));
            const ImVec2 size(texture_width * scale, texture_height * scale);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - size.x) * 0.5F);
            ImGui::Image((ImTextureID)(intptr_t)texture_id, size);
        } else {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextColored(p.warning, "%s",
                               model.status_text.empty() ? "QR image unavailable; the payload is shown below." : model.status_text.c_str());
            ImGui::PopTextWrapPos();
        }
        auto payload = model.primary_text.substr(0, 160);
        for (auto& ch : payload) if (ch == '\n') ch = ' ';
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(p.text_muted, "%s%s", payload.c_str(), model.primary_text.size() > 160 ? "\xE2\x80\xA6" : "");
        ImGui::PopTextWrapPos();
        if (!panel.status_text.empty()) ImGui::TextColored(p.success, "%s", panel.status_text.c_str());

        const auto* save_png = find("save_rendered");
        const int clicked = footer_buttons({{with_icon(icon::kFolderOpen, tr(UiTextKey::OpenPngExternally)), false, model.preview_available},
                                            {with_icon(icon::kSave, tr(UiTextKey::SavePng)), false, save_png != nullptr},
                                            {with_icon(icon::kCopy, tr(UiTextKey::CopyPayload)), true}});
        if (clicked == 0 && state.active().output_path) panel.open_preview(state.active().output_path->string(), open_path);
        if (clicked == 1) ImGui::OpenPopup("qr-save");
        if (clicked == 2) ImGui::SetClipboardText(model.primary_text.c_str());
        if (ImGui::BeginPopup("qr-save")) {
            ImGui::SetNextItemWidth(320.0F);
            input_text_hint("##qr-destination", tr(UiTextKey::Destination).c_str(), panel.destination);
            ImGui::Checkbox(tr(UiTextKey::ConfirmOverwrite).c_str(), &panel.confirm_overwrite);
            if (primary_button(tr(UiTextKey::Save)) && save_png != nullptr) {
                panel.save(*save_png, ".png");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}
#endif

}  // namespace pastit
