#include "ui/task_panel.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace pasteit {

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
namespace {

std::string clock_text(std::int64_t time_ms) {
    const std::time_t seconds = static_cast<std::time_t>(time_ms / 1000);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof buffer, "%H:%M:%S", &local);
    return buffer;
}

std::string human_units(std::uint64_t value, const std::string& kind) {
    if (kind != "download") return std::to_string(value);
    const char* units[] = {"B", "KB", "MB", "GB"};
    double size = static_cast<double>(value);
    int unit = 0;
    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, unit == 0 ? "%.0f %s" : "%.1f %s", size, units[unit]);
    return buffer;
}

const char* kind_icon(const std::string& kind) {
    if (kind == "download") return icon::kSave;
    if (kind == "network") return icon::kNetwork;
    if (kind == "llm") return icon::kAi;
    if (kind == "render") return icon::kMedia;
    if (kind == "hash") return icon::kHash;
    if (kind == "clone") return icon::kCode;
    if (kind == "fetch") return icon::kGlobe;
    if (kind == "test") return icon::kRefresh;
    return icon::kZap;
}

UiTextKey state_key(TaskState state) {
    switch (state) {
        case TaskState::Queued: return UiTextKey::TaskQueued;
        case TaskState::Running: return UiTextKey::TaskRunning;
        case TaskState::Paused: return UiTextKey::TaskPaused;
        case TaskState::Done: return UiTextKey::TaskDone;
        case TaskState::Failed: return UiTextKey::TaskFailed;
        case TaskState::Canceled: return UiTextKey::TaskCanceled;
    }
    return UiTextKey::TaskRunning;
}

// A 4 px bar: accent while running, warning when paused, success / danger
// when finished; indeterminate work shows a moving segment.
void task_bar(const TaskEntry& task, float width) {
    const auto& p = palette();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = 4.0F * scale;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(p.track), height * 0.5F);
    const ImVec4 fill = task.state == TaskState::Paused ? p.warning
                      : task.state == TaskState::Done   ? p.success
                      : task.state == TaskState::Failed ? p.danger
                      : task.state == TaskState::Canceled ? p.text_muted
                                                          : p.accent;
    if (task.state == TaskState::Running && task.total == 0) {
        const float t = static_cast<float>(std::fmod(ImGui::GetTime() * 0.8, 1.0));
        const float segment = width * 0.3F;
        const float x = pos.x - segment + (width + segment) * t;
        draw->PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);
        draw->AddRectFilled(ImVec2(x, pos.y), ImVec2(x + segment, pos.y + height), ImGui::GetColorU32(fill), height * 0.5F);
        draw->PopClipRect();
    } else {
        float fraction = task_finished(task.state) && task.state != TaskState::Canceled ? 1.0F
                       : task.total > 0 ? static_cast<float>(static_cast<double>(task.done) / static_cast<double>(task.total)) : 0.0F;
        fraction = std::clamp(fraction, 0.0F, 1.0F);
        if (fraction > 0.0F) {
            draw->AddRectFilled(pos, ImVec2(pos.x + std::max(width * fraction, height), pos.y + height), ImGui::GetColorU32(fill), height * 0.5F);
        }
    }
    ImGui::Dummy(ImVec2(width, height));
}

bool contains_folded(std::string_view text, std::string_view needle) {
    const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; };
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
}

}  // namespace

bool task_status_button(const TaskCenter& center, bool open, UiLanguage language) {
    const auto& p = palette();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const std::size_t active = center.active_count();
    std::string tip = tr(language, UiTextKey::TaskQueue);
    if (active > 0) {
        char summary[96];
        std::snprintf(summary, sizeof summary, " \xC2\xB7 %zu %s \xC2\xB7 %.0f%%", active, tr(language, UiTextKey::TaskRunning).c_str(),
                      center.overall_progress() * 100.0);
        tip += summary;
    }
    const bool clicked = icon_button("cluster-tasks", icon::kListCheck, tip, open, "Ctrl+J");
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 center_point((min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F);
    auto* draw = ImGui::GetWindowDrawList();
    if (active > 0) {
        // Real progress, never an endless spinner.
        const float radius = (max.x - min.x) * 0.5F - 1.5F * scale;
        draw->AddCircle(center_point, radius, ImGui::GetColorU32(p.track), 32, 2.0F * scale);
        const float progress = static_cast<float>(center.overall_progress());
        if (progress > 0.0F) {
            constexpr float kStart = -3.14159265F * 0.5F;
            draw->PathArcTo(center_point, radius, kStart, kStart + progress * 2.0F * 3.14159265F, 40);
            draw->PathStroke(ImGui::GetColorU32(center.any_paused() ? p.warning : p.accent), 0, 2.0F * scale);
        }
        // Count badge at the top right.
        const std::string count = std::to_string(std::min<std::size_t>(active, 99));
        ImGui::PushFont(ui_fonts().bold, ui_fonts().small * 0.85F);
        const ImVec2 text = ImGui::CalcTextSize(count.c_str());
        const float badge = std::max(text.x + 6.0F * scale, text.y + 2.0F * scale);
        const ImVec2 badge_min(max.x - badge * 0.7F, min.y - badge * 0.25F);
        draw->AddRectFilled(badge_min, ImVec2(badge_min.x + badge, badge_min.y + text.y + 2.0F * scale),
                            ImGui::GetColorU32(center.failed_count() > 0 ? p.danger : p.accent), badge * 0.5F);
        draw->AddText(ImVec2(badge_min.x + (badge - text.x) * 0.5F, badge_min.y + 1.0F * scale), IM_COL32(255, 255, 255, 255), count.c_str());
        ImGui::PopFont();
    } else if (center.failed_count() > 0 || center.unseen_warnings() > 0) {
        draw->AddCircleFilled(ImVec2(max.x - 5.0F * scale, min.y + 5.0F * scale), 3.5F * scale,
                              ImGui::GetColorU32(center.failed_count() > 0 ? p.danger : p.warning));
    }
    return clicked;
}

TaskCommand draw_task_popover(TaskPanelState& state, TaskCenter& center, std::int64_t now_ms, UiLanguage language,
                              std::string_view status_line, const std::function<void(std::string_view)>& copy_text,
                              const std::function<void()>& open_log_folder) {
    TaskCommand command;
    if (!state.open) return command;
    const auto& p = palette();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    if (!ImGui::IsPopupOpen("##tasks-popover")) ImGui::OpenPopup("##tasks-popover");
    ImGui::SetNextWindowPos(ImVec2(state.anchor_x, state.anchor_y), ImGuiCond_Always, ImVec2(1.0F, 0.0F));
    ImGui::SetNextWindowSizeConstraints(ImVec2(440.0F * scale, 0.0F), ImVec2(440.0F * scale, 520.0F * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0F * scale, 10.0F * scale));
    const bool visible = ImGui::BeginPopup("##tasks-popover", ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!visible) {
        state.open = false;
        return command;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    // Header: the app status in one sentence, then the tabs.
    ImGui::TextColored(p.text_muted, "%.*s", static_cast<int>(status_line.size()), status_line.data());
    const std::string tabs[] = {tr(language, UiTextKey::Tasks), tr(language, UiTextKey::Logs)};
    (void)segmented_control("##tasks-tabs", tabs, state.tab, true);
    ImGui::Spacing();
    if (state.tab == 0) {
        // Global actions.
        const bool any_active = center.active_count() > 0;
        const bool any_finished = std::any_of(center.tasks().begin(), center.tasks().end(),
                                              [](const TaskEntry& task) { return task_finished(task.state); });
        ImGui::BeginDisabled(!any_active);
        if (ImGui::Button(with_icon(icon::kPause, tr(language, UiTextKey::PauseAll)).c_str())) command = {TaskCommandKind::PauseAll, {}};
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kPlay, tr(language, UiTextKey::ResumeAll)).c_str())) command = {TaskCommandKind::ResumeAll, {}};
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kClose, tr(language, UiTextKey::CancelAll)).c_str())) command = {TaskCommandKind::CancelAll, {}};
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!any_finished);
        if (ImGui::Button(tr(language, UiTextKey::ClearDone).c_str())) command = {TaskCommandKind::ClearDone, {}};
        ImGui::EndDisabled();
        if (center.tasks().empty()) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 410.0F * scale);
            ImGui::TextColored(p.text_muted, "%s", tr(language, UiTextKey::NoTasks).c_str());
            ImGui::PopTextWrapPos();
        }
        // Sections in the order a user cares about them.
        const std::pair<UiTextKey, std::vector<TaskState>> sections[] = {
            {UiTextKey::TaskRunning, {TaskState::Running, TaskState::Paused}},
            {UiTextKey::TaskQueued, {TaskState::Queued}},
            {UiTextKey::TaskFailed, {TaskState::Failed}},
            {UiTextKey::TaskDone, {TaskState::Done, TaskState::Canceled}},
        };
        ImGui::BeginChild("##task-rows", ImVec2(410.0F * scale, 0.0F), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
        for (const auto& [title, states] : sections) {
            bool heading = false;
            for (const auto& task : center.tasks()) {
                if (std::find(states.begin(), states.end(), task.state) == states.end()) continue;
                if (!heading) {
                    separator_heading(tr(language, title));
                    heading = true;
                }
                ImGui::PushID(task.id.c_str());
                const float row_width = ImGui::GetContentRegionAvail().x;
                if (ui_fonts().icons) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(p.text_muted, "%s", kind_icon(task.kind));
                    ImGui::SameLine();
                }
                // Name, then the actions at the right.
                const int actions = (task.pausable && (task.state == TaskState::Running || task.state == TaskState::Paused) ? 1 : 0) +
                                    (task.cancellable && !task_finished(task.state) ? 1 : 0) +
                                    (task.state == TaskState::Failed || task.state == TaskState::Canceled ? 1 : 0) +
                                    (task_finished(task.state) ? 1 : 0);
                const float actions_width = actions > 0 ? icon_buttons_width(actions) : 0.0F;
                ImGui::AlignTextToFramePadding();
                const float name_width = row_width - actions_width - 40.0F * scale;
                ImGui::PushClipRect(ImGui::GetCursorScreenPos(),
                                    ImVec2(ImGui::GetCursorScreenPos().x + name_width, ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight()), true);
                ImGui::TextUnformatted(task.title.c_str());
                ImGui::PopClipRect();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", task.title.c_str());
                ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - actions_width));
                bool first_action = true;
                const auto action = [&](const char* id, const char* glyph, UiTextKey tip, TaskCommandKind kind) {
                    if (!first_action) ImGui::SameLine();
                    first_action = false;
                    if (icon_button(id, glyph, tr(language, tip))) command = {kind, task.id};
                };
                if (task.pausable && task.state == TaskState::Running) action("pause", icon::kPause, UiTextKey::Pause, TaskCommandKind::Pause);
                if (task.pausable && task.state == TaskState::Paused) action("resume", icon::kPlay, UiTextKey::Resume, TaskCommandKind::Resume);
                if (task.cancellable && !task_finished(task.state)) action("cancel", icon::kClose, UiTextKey::Cancel, TaskCommandKind::Cancel);
                if (task.state == TaskState::Failed || task.state == TaskState::Canceled) {
                    action("retry", icon::kRotateRight, UiTextKey::Retry, TaskCommandKind::Retry);
                }
                if (task_finished(task.state)) action("remove", icon::kTrash, UiTextKey::Remove, TaskCommandKind::Remove);
                task_bar(task, row_width);
                // Meta line: progress, rate, ETA, or the result / error.
                std::string meta = tr(language, state_key(task.state));
                if (task.total > 0 && !task_finished(task.state)) {
                    meta += " \xC2\xB7 " + human_units(task.done, task.kind) + " / " + human_units(task.total, task.kind);
                    if (task.rate > 0.0 && task.state == TaskState::Running) meta += " \xC2\xB7 " + human_units(static_cast<std::uint64_t>(task.rate), task.kind) + "/s";
                    if (const auto eta = center.eta_seconds(task, now_ms)) meta += " \xC2\xB7 " + format_eta(*eta);
                } else if (task_finished(task.state) && task.finished_ms > task.started_ms) {
                    meta += " \xC2\xB7 " + std::to_string(std::max<std::int64_t>(1, (task.finished_ms - task.started_ms + 999) / 1000)) + " s";
                }
                if (!task.detail.empty()) meta += " \xC2\xB7 " + task.detail;
                ImGui::PushFont(ui_fonts().regular, ui_fonts().small);
                const ImVec4 meta_color = task.state == TaskState::Failed ? p.danger : p.text_muted;
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::PushClipRect(at, ImVec2(at.x + row_width, at.y + ImGui::GetFontSize() + 2.0F), true);
                ImGui::TextColored(meta_color, "%s", meta.c_str());
                ImGui::PopClipRect();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !task.detail.empty()) ImGui::SetTooltip("%s", task.detail.c_str());
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0.0F, 4.0F * scale));
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    } else {
        // Logs: level chips, search, copy, open folder; newest first.
        static constexpr const char* kLevels[] = {"Debug", "Info", "Warning", "Error"};
        for (int level = 3; level >= 0; --level) {
            if (level != 3) ImGui::SameLine(0.0F, 4.0F * scale);
            const bool on = state.show_level[level];
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0F);
            ImGui::PushStyleColor(ImGuiCol_Button, on ? p.accent_soft : p.track);
            ImGui::PushStyleColor(ImGuiCol_Text, on ? p.accent : p.text_muted);
            if (ImGui::Button(kLevels[level])) state.show_level[level] = !on;
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }
        ImGui::SameLine();
        const float tools = icon_buttons_width(2);
        ImGui::SetNextItemWidth(std::max(80.0F * scale, ImGui::GetContentRegionAvail().x - tools - ImGui::GetStyle().ItemSpacing.x));
        char buffer[128];
        const auto copied = state.log_search.copy(buffer, sizeof buffer - 1);
        buffer[copied] = '\0';
        if (ImGui::InputTextWithHint("##log-search", tr(language, UiTextKey::FindText).c_str(), buffer, sizeof buffer)) state.log_search = buffer;
        std::string visible_text;
        std::vector<const LogEntry*> rows;
        for (auto it = center.logs().rbegin(); it != center.logs().rend(); ++it) {
            if (!state.show_level[static_cast<int>(it->level)]) continue;
            if (!state.log_search.empty() && !contains_folded(it->message, state.log_search)) continue;
            rows.push_back(&*it);
            visible_text += clock_text(it->time_ms) + " " + kLevels[static_cast<int>(it->level)] + " " + it->message + "\n";
        }
        ImGui::SameLine();
        if (icon_button("copy-logs", icon::kCopy, tr(language, UiTextKey::Copy)) && copy_text) copy_text(visible_text);
        ImGui::SameLine();
        if (icon_button("log-folder", icon::kFolderOpen, tr(language, UiTextKey::OpenFolder)) && open_log_folder) open_log_folder();
        ImGui::BeginChild("##log-rows", ImVec2(416.0F * scale, std::min(360.0F * scale, 24.0F * scale * static_cast<float>(rows.size() + 1))),
                          ImGuiChildFlags_None);
        if (rows.empty()) ImGui::TextColored(p.text_muted, "%s", tr(language, UiTextKey::NoLogs).c_str());
        for (const auto* entry : rows) {
            const ImVec4 color = entry->level == LogLevel::Error ? p.danger : entry->level == LogLevel::Warning ? p.warning : p.text_muted;
            const char* glyph = entry->level == LogLevel::Error ? icon::kCircleX : entry->level == LogLevel::Warning ? icon::kWarning : icon::kInfo;
            ImGui::PushFont(ui_fonts().regular, ui_fonts().small);
            ImGui::TextColored(p.text_muted, "%s", clock_text(entry->time_ms).c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", glyph);
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextUnformatted(entry->message.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        center.mark_logs_seen();
    }
    ImGui::EndPopup();
    return command;
}
#endif

}  // namespace pasteit
