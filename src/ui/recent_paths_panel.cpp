#include "ui/recent_paths_panel.hpp"

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include "ui/multi_viewport.hpp"
#include "ui/imgui_widgets.hpp"
#include <imgui.h>
#endif

namespace pastit {
namespace {

#if defined(PASTIT_HAS_DESKTOP_DEPS)
bool compact_button(const char* label, const std::string& ref) {
    const auto id = std::string(label) + "##recent-" + ref;
    return ImGui::SmallButton(id.c_str());
}

void detail_text(const char* label, const std::string& value) {
    copyable_text(std::string{label} + ": " + value, true);
}
#endif

}  // namespace

RecentPathCommand render_recent_paths_panel(RecentPathsState& state, const RecentPathsModel& model,
                                            UiLanguage language) {
#if defined(PASTIT_HAS_DESKTOP_DEPS)
    RecentPathCommand command;
    constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                           ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("recent-paths-table", 5, flags, ImVec2(0.0F, 0.0F))) {
        ImGui::TableSetupColumn(tr(language, UiTextKey::Type).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Path).c_str(), ImGuiTableColumnFlags_WidthStretch, 4.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Source).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Seen).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn(tr(language, UiTextKey::Actions).c_str(), ImGuiTableColumnFlags_WidthFixed, 150.0F);
        ImGui::TableHeadersRow();

        for (const auto& row : model.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            copyable_text(row.type_label);
            ImGui::TableNextColumn();
            const auto selectable_id = row.display_path + "##recent-row-" + row.ref;
            if (ImGui::Selectable(selectable_id.c_str(), row.selected, ImGuiSelectableFlags_SpanAllColumns)) {
                command = view_recent_path(state, row.ref);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                command = view_recent_path(state, row.ref);
            }
            ImGui::TableNextColumn();
            copyable_text(row.source);
            ImGui::TableNextColumn();
            copyable_text(row.last_seen_label);
            ImGui::TableNextColumn();
            if (compact_button(tr(language, UiTextKey::View).c_str(), row.ref)) {
                command = view_recent_path(state, row.ref);
            }
            ImGui::SameLine();
            if (compact_button(tr(language, UiTextKey::Copy).c_str(), row.ref)) {
                const RecentPathsModel& current = model;
                command = copy_recent_path(current, row.ref);
            }
            ImGui::SameLine();
            if (!row.can_open) {
                ImGui::BeginDisabled();
            }
            if (compact_button(tr(language, UiTextKey::Open).c_str(), row.ref)) {
                command = open_recent_path(model, row.ref);
            }
            if (!row.can_open) {
                ImGui::EndDisabled();
            }
        }
        ImGui::EndTable();
    }

    if (model.detail) {
        bool detail_open = true;
        const auto window_class = independent_window_class();
        ImGui::SetNextWindowClass(&window_class);
        ImGui::SetNextWindowSize(ImVec2(700.0F, 360.0F), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Recent Path Detail", &detail_open, ImGuiWindowFlags_NoSavedSettings)) {
            if (ImGui::SmallButton(tr(language, UiTextKey::CopyPath).c_str())) {
                command = copy_recent_path(model, model.detail->ref);
            }
            ImGui::SameLine();
            if (!model.detail->can_open) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton(tr(language, UiTextKey::Open).c_str())) {
                command = open_recent_path(model, model.detail->ref);
            }
            if (!model.detail->can_open) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_open_parent) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton(tr(language, UiTextKey::OpenParent).c_str())) {
                command = open_recent_path_parent(model, model.detail->ref);
            }
            if (!model.detail->can_open_parent) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_copy_here) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton(tr(language, UiTextKey::CopyHere).c_str())) {
                command = copy_here_recent_path(model, model.detail->ref);
            }
            if (!model.detail->can_copy_here) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_move_here) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton(tr(language, UiTextKey::MoveHere).c_str())) {
                command = move_here_recent_path(model, model.detail->ref);
            }
            if (!model.detail->can_move_here) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (!model.detail->can_use_as_destination) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton(tr(language, UiTextKey::UseAsDestination).c_str())) {
                command = use_recent_path_as_destination(model, model.detail->ref);
            }
            if (!model.detail->can_use_as_destination) {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr(language, UiTextKey::Close).c_str())) {
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

}  // namespace pastit
