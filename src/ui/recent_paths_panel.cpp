#include "ui/recent_paths_panel.hpp"
#include "ui/theme.hpp"
#include "ui/icons.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/multi_viewport.hpp"
#include "ui/imgui_widgets.hpp"
#include <imgui.h>
#endif

namespace pasteit {
namespace {

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
bool compact_button(const char* label, const std::string& ref) {
    const auto id = std::string(label) + "##recent-" + ref;
    return ImGui::SmallButton(id.c_str());
}

void draw_path_type_icon(const RecentPathRow& row) {
    const bool folder = row.type == "folder";
    const auto tint = !row.exists ? palette().text_muted : category_color(folder ? ActionCategory::Save : ActionCategory::Paste);
    if (icon_cell(folder ? icon::kFolder : icon::kFile, tint)) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    constexpr ImVec2 size{18.0F, 18.0F};
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    if (row.type == "folder") {
        draw->AddRectFilled(ImVec2(origin.x + 1.0F, origin.y + 5.0F),
                            ImVec2(origin.x + 17.0F, origin.y + 17.0F), color, 2.0F);
        draw->AddRectFilled(ImVec2(origin.x + 2.0F, origin.y + 3.0F),
                            ImVec2(origin.x + 9.0F, origin.y + 7.0F), color, 1.5F);
        draw->AddLine(ImVec2(origin.x + 5.0F, origin.y + 12.0F),
                      ImVec2(origin.x + 13.0F, origin.y + 12.0F), accent, 1.5F);
    } else {
        const ImVec2 outline[] = {
            {origin.x + 3.0F, origin.y + 1.0F}, {origin.x + 11.0F, origin.y + 1.0F},
            {origin.x + 16.0F, origin.y + 6.0F}, {origin.x + 16.0F, origin.y + 17.0F},
            {origin.x + 3.0F, origin.y + 17.0F},
        };
        draw->AddPolyline(outline, 5, color, ImDrawFlags_Closed, 1.5F);
        draw->AddLine(ImVec2(origin.x + 11.0F, origin.y + 1.0F),
                      ImVec2(origin.x + 11.0F, origin.y + 6.0F), accent, 1.5F);
        draw->AddLine(ImVec2(origin.x + 11.0F, origin.y + 6.0F),
                      ImVec2(origin.x + 16.0F, origin.y + 6.0F), accent, 1.5F);
    }
    ImGui::Dummy(size);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", row.type_label.c_str());
}

void detail_text(const char* label, const std::string& value) {
    copyable_text(std::string{label} + ": " + value, true);
}
#endif

}  // namespace

RecentPathCommand render_recent_paths_panel(RecentPathsState& state, const RecentPathsModel& model,
                                            UiLanguage language) {
#if defined(PASTEIT_HAS_DESKTOP_DEPS)
    RecentPathCommand command;
    constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                           ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("recent-paths-table", 6, flags, ImVec2(0.0F, 0.0F))) {
        ImGui::TableSetupColumn(tr(language, UiTextKey::Type).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Path).c_str(), ImGuiTableColumnFlags_WidthStretch, 4.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Source).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Uses).c_str(), ImGuiTableColumnFlags_WidthFixed, 48.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Seen).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Actions).c_str(), ImGuiTableColumnFlags_WidthFixed, icon_buttons_width(3));
        ImGui::TableHeadersRow();

        for (const auto& row : model.rows) {
            ImGui::PushID(row.ref.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            draw_path_type_icon(row);
            ImGui::TableNextColumn();
            const auto selectable_id = row.display_path + "##recent-row-" + row.ref;
            // AllowOverlap lets the row's icon buttons take their own clicks;
            // otherwise the full-width row swallowed Open and showed the view.
            if (ImGui::Selectable(selectable_id.c_str(), row.selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                // Double-click opens the file or folder itself; a single click shows details.
                command = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ? open_recent_path(model, row.ref)
                                                                            : view_recent_path(state, row.ref);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tr(language, UiTextKey::RecentRowHint).c_str());
            ImGui::TableNextColumn();
            copyable_text(row.source);
            ImGui::TableNextColumn();
            if (row.use_count > 0) ImGui::Text("%u", row.use_count);
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            copyable_text(row.last_seen_label);
            ImGui::TableNextColumn();
            if (icon_button("view", icon::kEye, tr(language, UiTextKey::View))) {
                command = view_recent_path(state, row.ref);
            }
            ImGui::SameLine();
            if (icon_button("copy", icon::kCopy, tr(language, UiTextKey::Copy))) {
                const RecentPathsModel& current = model;
                command = copy_recent_path(current, row.ref);
            }
            ImGui::SameLine();
            if (!row.can_open) {
                ImGui::BeginDisabled();
            }
            if (icon_button("open", icon::kFolderOpen, tr(language, UiTextKey::Open))) {
                command = open_recent_path(model, row.ref);
            }
            if (!row.can_open) {
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
        ImGui::SetNextWindowSize(ImVec2(700.0F, 360.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin((tr(UiTextKey::RecentPathDetail) + "##recent-path-detail").c_str(), &detail_open, ImGuiWindowFlags_NoSavedSettings)) {
            if (ImGui::Button(tr(language, UiTextKey::CopyPath).c_str())) {
                command = copy_recent_path(model, model.detail->ref);
            }
            ImGui::SameLine();
            if (!model.detail->can_open) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(tr(language, UiTextKey::Open).c_str())) {
                command = open_recent_path(model, model.detail->ref);
            }
            if (!model.detail->can_open) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_open_parent) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(tr(language, UiTextKey::OpenParent).c_str())) {
                command = open_recent_path_parent(model, model.detail->ref);
            }
            if (!model.detail->can_open_parent) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_copy_here) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(tr(language, UiTextKey::CopyHere).c_str())) {
                command = copy_here_recent_path(model, model.detail->ref);
            }
            if (!model.detail->can_copy_here) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_move_here) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(tr(language, UiTextKey::MoveHere).c_str())) {
                command = move_here_recent_path(model, model.detail->ref);
            }
            if (!model.detail->can_move_here) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_use_as_destination) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(tr(language, UiTextKey::UseAsDestination).c_str())) {
                command = use_recent_path_as_destination(model, model.detail->ref);
            }
            if (!model.detail->can_use_as_destination) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr(language, UiTextKey::Close).c_str())) {
                detail_open = false;
            }
            ImGui::Separator();
            ImGui::BeginChild("recent-path-detail-body", ImVec2(0.0F, 0.0F), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            detail_text(tr(language, UiTextKey::Reference).c_str(), model.detail->ref);
            detail_text(tr(language, UiTextKey::Path).c_str(), model.detail->full_path);
            detail_text(tr(language, UiTextKey::Parent).c_str(), model.detail->parent_path);
            detail_text(tr(language, UiTextKey::Type).c_str(), model.detail->type_label);
            detail_text(tr(language, UiTextKey::Source).c_str(), model.detail->source);
            detail_text(tr(language, UiTextKey::Seen).c_str(), model.detail->last_seen_label);
            detail_text(tr(language, UiTextKey::CopyHereAction).c_str(), model.detail->copy_here_action_id);
            detail_text(tr(language, UiTextKey::MoveHereAction).c_str(), model.detail->move_here_action_id);
            ImGui::EndChild();
        }
        ImGui::End();
        if (!detail_open) command = close_recent_path_detail(state);
    }
    return command;
#else
    (void)state;
    (void)model;
    (void)language;
    return {};
#endif
}

}  // namespace pasteit
