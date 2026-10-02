#include "ui/diff_view.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

#include <algorithm>
#include <cstdio>

namespace pasteit {

void open_diff_view(DiffViewState& state, std::string title, std::string left, std::string right) {
    state = {};
    state.open = true;
    state.focus_pending = true;
    state.title = std::move(title);
    state.diff = diff_texts(left, right);
    state.left = std::move(left);
    state.right = std::move(right);
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
namespace {

ImVec4 tinted(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

// One side of a row: the line with its changed characters on a stronger tint.
void draw_line(const std::string& line, const std::vector<DiffSpan>& spans, ImVec4 highlight) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const float height = ImGui::GetTextLineHeight();
    for (const auto& [first, last] : spans) {
        if (first >= line.size()) continue;
        const auto end = std::min(last, line.size());
        const float x0 = ImGui::CalcTextSize(line.data(), line.data() + first).x;
        const float x1 = ImGui::CalcTextSize(line.data(), line.data() + end).x;
        // Changed whitespace still gets a visible mark.
        draw->AddRectFilled(ImVec2(origin.x + x0, origin.y), ImVec2(origin.x + std::max(x1, x0 + 3.0F), origin.y + height),
                            ImGui::GetColorU32(highlight), 3.0F);
    }
    ImGui::TextUnformatted(line.data(), line.data() + line.size());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::GetItemRectSize().x > ImGui::GetColumnWidth()) {
        ImGui::SetTooltip("%s", line.c_str());
    }
}

}  // namespace

void draw_diff_view(DiffViewState& state, UiLanguage language, const std::function<void(std::string_view)>& copy_text,
                    const std::function<void()>& on_preview) {
    if (!state.open) return;
    const auto title = with_icon(icon::kCompare, tr(language, UiTextKey::Compare) + " \xC2\xB7 " + state.title) + "###diff-view";
    if (begin_tool_window(title, &state.open, ImVec2(960.0F, 600.0F), &state.focus_pending)) {
        const auto& p = palette();
        const auto& diff = state.diff;
        const float digits = ImGui::CalcTextSize("0000").x + ImGui::GetStyle().CellPadding.x * 2.0F;
        const auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame |
                           ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("diff-table", 4, flags, ImVec2(0.0F, -footer_height()))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##left-number", ImGuiTableColumnFlags_WidthFixed, digits);
            ImGui::TableSetupColumn(tr(language, UiTextKey::Original).c_str(), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##right-number", ImGuiTableColumnFlags_WidthFixed, digits);
            ImGui::TableSetupColumn(tr(language, UiTextKey::Result).c_str(), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            const ImVec4 removed_row = tinted(p.danger, 0.10F);
            const ImVec4 added_row = tinted(p.success, 0.12F);
            const ImVec4 removed_mark = tinted(p.danger, 0.30F);
            const ImVec4 added_mark = tinted(p.success, 0.34F);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(diff.rows.size()));
            while (clipper.Step()) {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    const auto& row = diff.rows[static_cast<std::size_t>(index)];
                    ImGui::TableNextRow();
                    const bool has_left = row.left_number > 0;
                    const bool has_right = row.right_number > 0;
                    const bool left_changed = row.kind == DiffRowKind::Removed || row.kind == DiffRowKind::Changed;
                    const bool right_changed = row.kind == DiffRowKind::Added || row.kind == DiffRowKind::Changed;
                    char number[16];
                    ImGui::TableNextColumn();
                    if (left_changed) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(removed_row));
                    if (has_left) {
                        std::snprintf(number, sizeof number, "%d", row.left_number);
                        ImGui::TextColored(p.text_muted, "%s", number);
                    }
                    ImGui::TableNextColumn();
                    if (left_changed) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(removed_row));
                    if (has_left) draw_line(row.left, row.kind == DiffRowKind::Changed ? row.left_spans : std::vector<DiffSpan>{}, removed_mark);
                    ImGui::TableNextColumn();
                    if (right_changed) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(added_row));
                    if (has_right) {
                        std::snprintf(number, sizeof number, "%d", row.right_number);
                        ImGui::TextColored(p.text_muted, "%s", number);
                    }
                    ImGui::TableNextColumn();
                    if (right_changed) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(added_row));
                    if (has_right) draw_line(row.right, row.kind == DiffRowKind::Changed ? row.right_spans : std::vector<DiffSpan>{}, added_mark);
                }
            }
            ImGui::EndTable();
        }
        // Summary: lines removed and added, as in a unified diff header.
        std::string status = "\xE2\x88\x92" + std::to_string(diff.removed) + "  +" + std::to_string(diff.added) + " " +
                             tr(language, UiTextKey::DiffLines);
        if (diff.approximate) status += "  \xC2\xB7  " + tr(language, UiTextKey::DiffApproximate);
        if (!state.status.empty()) status += "  \xC2\xB7  " + state.status;
        const int clicked = footer_buttons({{with_icon(icon::kCopy, tr(language, UiTextKey::CopyResult))},
                                            {with_icon(icon::kEye, tr(language, UiTextKey::PreviewInInput)), true}},
                                           status);
        if (clicked == 0 && copy_text) {
            copy_text(state.right);
            state.status = tr(language, UiTextKey::Copied);
        }
        if (clicked == 1) {
            if (on_preview) on_preview();
            state.open = false;
        }
    }
    ImGui::End();
}
#endif

}  // namespace pasteit
