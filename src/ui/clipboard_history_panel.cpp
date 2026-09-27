#include "ui/clipboard_history_panel.hpp"
#include "ui/theme.hpp"
#include "ui/icons.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/multi_viewport.hpp"
#include "ui/imgui_widgets.hpp"
#include <imgui.h>

#include <algorithm>
#endif

namespace pasteit {
namespace {

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
ImTextureID imgui_texture_id(std::uintptr_t handle) {
    return static_cast<ImTextureID>(handle);
}

bool compact_button(const char* label, const std::string& ref) {
    const auto id = std::string(label) + "##clipboard-" + ref;
    return ImGui::SmallButton(id.c_str());
}

void draw_content_type_icon(ContentKind kind) {
    // Lucide glyphs in a per-kind color; the drawn shapes below are the
    // fallback when the icon font is missing.
    const char* glyph = kind == ContentKind::Text ? icon::kFileText : kind == ContentKind::Url ? icon::kLink
                      : kind == ContentKind::Email ? icon::kMail : kind == ContentKind::Image ? icon::kMedia
                      : kind == ContentKind::Path ? icon::kFolder : kind == ContentKind::Json ? icon::kBraces : icon::kInfo;
    const ActionCategory tint = kind == ContentKind::Url ? ActionCategory::Open : kind == ContentKind::Email ? ActionCategory::Network
                              : kind == ContentKind::Image ? ActionCategory::Media : kind == ContentKind::Path ? ActionCategory::Save
                              : kind == ContentKind::Json ? ActionCategory::Convert : ActionCategory::Paste;
    if (icon_cell(glyph, category_color(tint))) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    constexpr ImVec2 size{18.0F, 18.0F};
    const ImVec2 max{origin.x + size.x, origin.y + size.y};
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    switch (kind) {
        case ContentKind::Text:
            draw->AddRect(origin, max, color, 2.0F, 0, 2.0F);
            draw->AddLine(ImVec2(origin.x + 4.0F, origin.y + 5.0F), ImVec2(max.x - 4.0F, origin.y + 5.0F), accent, 1.5F);
            draw->AddLine(ImVec2(origin.x + 4.0F, origin.y + 9.0F), ImVec2(max.x - 4.0F, origin.y + 9.0F), accent, 1.5F);
            draw->AddLine(ImVec2(origin.x + 4.0F, origin.y + 13.0F), ImVec2(max.x - 7.0F, origin.y + 13.0F), accent, 1.5F);
            break;
        case ContentKind::Url:
            draw->AddCircle(ImVec2(origin.x + 7.0F, origin.y + 10.0F), 4.5F, color, 16, 2.0F);
            draw->AddLine(ImVec2(origin.x + 9.5F, origin.y + 7.5F), ImVec2(max.x - 2.0F, origin.y + 2.0F), accent, 2.0F);
            draw->AddLine(ImVec2(max.x - 6.0F, origin.y + 2.0F), ImVec2(max.x - 2.0F, origin.y + 2.0F), accent, 2.0F);
            draw->AddLine(ImVec2(max.x - 2.0F, origin.y + 2.0F), ImVec2(max.x - 2.0F, origin.y + 6.0F), accent, 2.0F);
            break;
        case ContentKind::Email:
            draw->AddRect(origin, max, color, 2.0F, 0, 2.0F);
            draw->AddLine(origin, ImVec2(origin.x + size.x * 0.5F, origin.y + size.y * 0.55F), accent, 1.5F);
            draw->AddLine(ImVec2(max.x, origin.y), ImVec2(origin.x + size.x * 0.5F, origin.y + size.y * 0.55F), accent, 1.5F);
            break;
        case ContentKind::Image:
            draw->AddRect(origin, max, color, 2.0F, 0, 2.0F);
            draw->AddCircle(ImVec2(origin.x + 13.0F, origin.y + 5.0F), 1.8F, accent, 10, 1.5F);
            draw->AddTriangleFilled(ImVec2(origin.x + 3.0F, max.y - 3.0F), ImVec2(origin.x + 8.0F, origin.y + 8.0F), ImVec2(origin.x + 12.0F, max.y - 3.0F), accent);
            draw->AddTriangleFilled(ImVec2(origin.x + 8.0F, max.y - 3.0F), ImVec2(origin.x + 13.0F, origin.y + 9.0F), ImVec2(max.x - 2.0F, max.y - 3.0F), color);
            break;
        case ContentKind::Path:
            draw->AddRectFilled(ImVec2(origin.x + 1.0F, origin.y + 5.0F), max, color, 2.0F);
            draw->AddRectFilled(ImVec2(origin.x + 2.0F, origin.y + 3.0F), ImVec2(origin.x + 9.0F, origin.y + 7.0F), color, 1.5F);
            draw->AddLine(ImVec2(origin.x + 5.0F, origin.y + 12.0F), ImVec2(origin.x + 13.0F, origin.y + 12.0F), accent, 1.5F);
            break;
        case ContentKind::Json:
            draw->AddRect(origin, max, color, 2.0F, 0, 2.0F);
            draw->AddCircleFilled(ImVec2(origin.x + 6.0F, origin.y + 9.0F), 1.8F, accent);
            draw->AddCircleFilled(ImVec2(origin.x + 12.0F, origin.y + 5.0F), 1.8F, accent);
            draw->AddCircleFilled(ImVec2(origin.x + 12.0F, origin.y + 13.0F), 1.8F, accent);
            draw->AddLine(ImVec2(origin.x + 7.5F, origin.y + 8.0F), ImVec2(origin.x + 10.5F, origin.y + 6.0F), accent, 1.2F);
            draw->AddLine(ImVec2(origin.x + 7.5F, origin.y + 10.0F), ImVec2(origin.x + 10.5F, origin.y + 12.0F), accent, 1.2F);
            break;
        case ContentKind::Unknown:
            draw->AddCircle(ImVec2(origin.x + size.x * 0.5F, origin.y + size.y * 0.5F), 7.0F, color, 16, 2.0F);
            draw->AddLine(ImVec2(origin.x + 9.0F, origin.y + 5.0F), ImVec2(origin.x + 9.0F, origin.y + 10.0F), accent, 2.0F);
            draw->AddCircleFilled(ImVec2(origin.x + 9.0F, origin.y + 14.0F), 1.0F, accent);
            break;
    }
    ImGui::Dummy(size);
}

void show_clipboard_detail(const ClipboardHistoryDetail& detail, UiLanguage language,
                           const ClipboardTextureLookup& texture_lookup) {
    copyable_text(detail.type_label);
    ImGui::Separator();
    copyable_text(tr(language, UiTextKey::Reference) + ": " + detail.ref);
    copyable_text(tr(language, UiTextKey::Source) + ": " + detail.source);
    copyable_text(tr(language, UiTextKey::Size) + ": " + detail.size_label);
    copyable_text(tr(language, UiTextKey::Captured) + ": " + detail.captured_label);
    copyable_text(tr(language, UiTextKey::MimeTypes) + ": " + detail.mime_summary);
    if (detail.is_image && texture_lookup) {
        if (const auto texture = texture_lookup(detail.ref); texture && texture->handle != 0) {
            const auto width = static_cast<float>(std::max(texture->width, 1));
            const auto height = static_cast<float>(std::max(texture->height, 1));
            const auto display = ImGui::GetIO().DisplaySize;
            const auto max_width = std::max(120.0F, std::min(720.0F, display.x * 0.72F));
            const auto max_height = std::max(120.0F, std::min(520.0F, display.y * 0.65F));
            const auto scale = std::min({1.0F, max_width / width, max_height / height});
            ImGui::Image(imgui_texture_id(texture->handle), ImVec2(width * scale, height * scale));
        }
    } else {
        ImGui::BeginChild("clipboard-history-preview", ImVec2(0.0F, 0.0F), true);
        auto text = detail.preview;
        input_text_string("##clipboard-history-preview-text", text, true, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndChild();
    }
}
#endif

}  // namespace

ClipboardHistoryCommand render_clipboard_history_panel(ClipboardHistoryState& state,
                                                       const ClipboardHistoryModel& model,
                                                       UiLanguage language,
                                                       ClipboardTextureLookup texture_lookup) {
#if defined(PASTEIT_HAS_DESKTOP_DEPS)
    ClipboardHistoryCommand command;
    constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                           ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("clipboard-history-table", 6, flags, ImVec2(0.0F, 0.0F))) {
        ImGui::TableSetupColumn(tr(language, UiTextKey::Type).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Preview).c_str(), ImGuiTableColumnFlags_WidthStretch, 4.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Source).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Size).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Captured).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Actions).c_str(), ImGuiTableColumnFlags_WidthFixed, 96.0F);
        ImGui::TableHeadersRow();

        for (std::size_t index = 0; index < model.rows.size(); ++index) {
            const auto& row = model.rows[index];
            // Scope every cell to the row: source, size and time cells are
            // click-to-copy widgets keyed by their text, which repeats across rows.
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            draw_content_type_icon(row.kind);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", row.type_label.c_str());
            }
            ImGui::TableNextColumn();
            const auto selectable_id = row.preview + "##clipboard-row-" + row.ref;
            if (ImGui::Selectable(selectable_id.c_str(), row.selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                command = view_clipboard_history_item(state, row.ref);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                command = view_clipboard_history_item(state, row.ref);
            }
            ImGui::TableNextColumn();
            copyable_text(row.source);
            ImGui::TableNextColumn();
            copyable_text(row.size_label);
            ImGui::TableNextColumn();
            copyable_text(row.captured_label);
            ImGui::TableNextColumn();
            if (icon_button("view", icon::kEye, tr(language, UiTextKey::View))) {
                command = view_clipboard_history_item(state, row.ref);
            }
            ImGui::SameLine();
            if (!row.can_use) {
                ImGui::BeginDisabled();
            }
            // Puts the item back on the system clipboard (text or image).
            if (icon_button("copy", icon::kCopy, tr(language, UiTextKey::Copy))) {
                command = use_clipboard_history_item(row.ref);
            }
            if (!row.can_use) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!row.can_save) {
                ImGui::BeginDisabled();
            }
            if (icon_button("save", icon::kSave, tr(language, UiTextKey::Save))) {
                command = save_clipboard_history_item(row.ref);
            }
            if (!row.can_save) {
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (model.detail) {
        bool detail_open = true;
        const auto window_class = independent_window_class();
        ImGui::SetNextWindowClass(&window_class);
        const auto rows = static_cast<float>(multiline_editor_visible_rows(model.detail->preview));
        const auto detail_height = std::clamp(250.0F + rows * ImGui::GetTextLineHeightWithSpacing(), 360.0F, 760.0F);
        ImGui::SetNextWindowSize(ImVec2(720.0F, detail_height), ImGuiCond_FirstUseEver);
        if (ImGui::Begin((tr(UiTextKey::ClipboardHistoryDetail) + "##clipboard-history-detail").c_str(), &detail_open, ImGuiWindowFlags_NoSavedSettings)) {
            if (!model.detail->can_use) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(with_icon(icon::kCopy, tr(language, UiTextKey::Copy)).c_str())) {
                command = use_clipboard_history_item(model.detail->ref);
            }
            if (!model.detail->can_use) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_save) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(tr(language, UiTextKey::Save).c_str())) {
                command = save_clipboard_history_item(model.detail->ref);
            }
            if (!model.detail->can_save) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr(language, UiTextKey::Close).c_str())) {
                detail_open = false;
            }
            ImGui::Separator();
            show_clipboard_detail(*model.detail, language, texture_lookup);
        }
        ImGui::End();
        if (!detail_open) command = close_clipboard_history_detail(state);
    }
    return command;
#else
    (void)state;
    (void)model;
    (void)language;
    (void)texture_lookup;
    return {};
#endif
}

}  // namespace pasteit
