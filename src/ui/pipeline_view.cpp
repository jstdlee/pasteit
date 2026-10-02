#include "ui/pipeline_view.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include "ui/icons.hpp"
#include "ui/imgui_widgets.hpp"
#include "ui/theme.hpp"

#include <imgui.h>
#endif

#include <algorithm>

namespace pasteit {

void open_pipeline_view(PipelineViewState& state, std::string input, std::string command) {
    if (state.pending) {
        state.abandoned.push_back(std::move(*state.pending));
        state.pending.reset();
    }
    state.active_stage = -1;
    state.open = true;
    state.focus_pending = true;
    state.input = std::move(input);
    state.command = command.empty() ? "sort | uniq -c | sort -nr" : std::move(command);
    state.ran_command.clear();
    state.has_result = false;
    state.status.clear();
    state.recipe_name.clear();
    state.edited = std::chrono::steady_clock::now() - std::chrono::seconds(1);
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)

namespace {

bool blank(const std::string& text) {
    return text.find_first_not_of(" \t") == std::string::npos;
}

std::size_t count_lines(const std::string& text) {
    return text.empty() ? 0 : static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
}

}  // namespace

void draw_pipeline_view(PipelineViewState& state, const PipelineOptions& options, const PipelineViewHost& host,
                        UiLanguage language) {
    std::erase_if(state.abandoned, [](std::future<PipelineResult>& run) {
        return run.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready;
    });
    if (!state.open) return;
    // Collect a finished run, then start the next one once edits settle.
    if (state.pending && state.pending->wait_for(std::chrono::milliseconds{0}) == std::future_status::ready) {
        state.result = state.pending->get();
        state.pending.reset();
        state.has_result = true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!state.pending && state.command != state.ran_command && now - state.edited > std::chrono::milliseconds{300}) {
        state.ran_command = state.command;
        state.pending.emplace(std::async(std::launch::async, [input = state.input, command = state.command, options] {
            return run_pipeline(input, command, options);
        }));
    }

    const auto title = with_icon(icon::kPipeline, tr(language, UiTextKey::Pipeline)) + "###pipeline-view";
    if (begin_tool_window(title, &state.open, ImVec2(880.0F, 600.0F), &state.focus_pending)) {
        const auto& p = palette();
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(p.text_muted, "%s", tr(language, UiTextKey::PipelineHelp).c_str());
        ImGui::PopTextWrapPos();
        // The whole command stays editable; the stage rows below edit the
        // same text one stage at a time.
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (input_text_hint("##pipeline-command", tr(language, UiTextKey::PipelineCommandHint).c_str(), state.command)) state.edited = now;

        auto stages = split_pipeline_text(state.command);
        bool stages_changed = false;
        std::optional<std::size_t> remove, move_up, move_down, insert_after;
        const float icon_width = ImGui::CalcTextSize(ui_fonts().icons ? icon::kPlus : "Delete").x;
        const float row_buttons = (icon_width + ImGui::GetStyle().FramePadding.x * 2.0F + 4.0F) * 4.0F + 12.0F;
        for (std::size_t index = 0; index < stages.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(state.active_stage == static_cast<int>(index) ? p.accent : p.text_muted, "%zu", index + 1);
            ImGui::SameLine(0.0F, 8.0F);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - row_buttons);
            if (input_text_hint("##stage", "", stages[index])) stages_changed = true;
            if (ImGui::IsItemActive() || ImGui::IsItemFocused()) state.active_stage = static_cast<int>(index);
            ImGui::SameLine(0.0F, 4.0F);
            if (index == 0) ImGui::BeginDisabled();
            if (icon_button("up", icon::kChevronUp, tr(language, UiTextKey::MoveUp))) move_up = index;
            if (index == 0) ImGui::EndDisabled();
            ImGui::SameLine(0.0F, 4.0F);
            if (index + 1 == stages.size()) ImGui::BeginDisabled();
            if (icon_button("down", icon::kChevronDown, tr(language, UiTextKey::MoveDown))) move_down = index;
            if (index + 1 == stages.size()) ImGui::EndDisabled();
            ImGui::SameLine(0.0F, 4.0F);
            if (icon_button("insert", icon::kPlus, tr(language, UiTextKey::InsertStage))) insert_after = index;
            ImGui::SameLine(0.0F, 4.0F);
            if (icon_button("remove", icon::kTrash, tr(language, UiTextKey::Delete))) remove = index;
            ImGui::PopID();
        }
        if (ImGui::Button(with_icon(icon::kPlus, tr(language, UiTextKey::AddStage)).c_str())) {
            stages.emplace_back();
            state.active_stage = static_cast<int>(stages.size()) - 1;
            stages_changed = true;
        }
        if (move_up) { std::swap(stages[*move_up], stages[*move_up - 1]); stages_changed = true; }
        if (move_down) { std::swap(stages[*move_down], stages[*move_down + 1]); stages_changed = true; }
        if (insert_after) {
            stages.insert(stages.begin() + static_cast<std::ptrdiff_t>(*insert_after) + 1, std::string{});
            state.active_stage = static_cast<int>(*insert_after) + 1;
            stages_changed = true;
        }
        if (remove) {
            stages.erase(stages.begin() + static_cast<std::ptrdiff_t>(*remove));
            state.active_stage = -1;
            stages_changed = true;
        }
        // Templates: fill the active stage when empty, otherwise insert after
        // it (or append). The inserted text stays editable like any stage.
        const auto insert_stage = [&](const std::string& text) {
            const bool has_active = state.active_stage >= 0 && state.active_stage < static_cast<int>(stages.size());
            if (has_active && blank(stages[static_cast<std::size_t>(state.active_stage)])) {
                stages[static_cast<std::size_t>(state.active_stage)] = text;
            } else if (!has_active && !stages.empty() && blank(stages.back())) {
                stages.back() = text;
                state.active_stage = static_cast<int>(stages.size()) - 1;
            } else {
                const auto at = has_active ? static_cast<std::size_t>(state.active_stage) + 1 : stages.size();
                stages.insert(stages.begin() + static_cast<std::ptrdiff_t>(at), text);
                state.active_stage = static_cast<int>(at);
            }
            stages_changed = true;
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(p.text_muted, "%s", tr(language, UiTextKey::StageTemplates).c_str());
        const auto chip = [&](const std::string& label, const std::string& text, const std::string& tooltip, const ImVec4* color) {
            ImGui::SameLine(0.0F, 4.0F);
            if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(label.c_str()).x + 28.0F > ImGui::GetContentRegionMax().x) ImGui::NewLine();
            if (color) ImGui::PushStyleColor(ImGuiCol_Text, *color);
            // Rounded chips, a full frame tall so they are easy to hit.
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0F);
            if (ImGui::Button((label + "##chip").c_str())) insert_stage(text);
            ImGui::PopStyleVar();
            if (color) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip.c_str());
        };
        static const char* quick[] = {"sort", "sort -nr", "uniq -c", "wc -l", "head -n 10", "grep -i ''", "cut -d , -f 1",
                                      "sed 's/a/b/g'", "tr a-z A-Z", "column", "anonymize"};
        for (const char* text : quick) {
            const std::string_view view{text};
            chip(text, text, pipeline_command_help(view.substr(0, view.find(' '))), nullptr);
        }
        for (const auto& [name, expansion] : options.custom_commands) chip(name, name, expansion, &p.accent);
        for (const auto& tool : options.allowed_tools) {
            chip(tool, tool + (tool == "jq" ? " '.'" : " '{ print }'"), tr(language, UiTextKey::ExternalTool), &p.warning);
        }
        if (stages_changed) {
            state.command = join_pipeline_stages(stages);
            state.edited = now;
        }

        // Input and output side by side.
        const float body_height = ImGui::GetContentRegionAvail().y - footer_height() - ImGui::GetTextLineHeightWithSpacing();
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
        ImGui::TextColored(p.text_muted, "%s  (%zu)", tr(language, UiTextKey::PipelineInput).c_str(), count_lines(state.input));
        ImGui::SameLine(half + ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().WindowPadding.x);
        const auto output_label = tr(language, UiTextKey::PipelineOutput);
        if (state.pending) {
            ImGui::TextColored(p.warning, "%s  %s", output_label.c_str(), tr(language, UiTextKey::StatusRanking).c_str());
        } else if (state.has_result && state.result.ok) {
            ImGui::TextColored(p.text_muted, "%s  (%zu, %lld ms%s)", output_label.c_str(), count_lines(state.result.output),
                               static_cast<long long>(state.result.elapsed.count()), state.result.truncated ? ", truncated" : "");
        } else {
            ImGui::TextColored(p.text_muted, "%s", output_label.c_str());
        }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
        ImGui::BeginChild("pipeline-input", ImVec2(half, body_height), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(state.input.data(), state.input.data() + std::min<std::size_t>(state.input.size(), 256 * 1024));
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("pipeline-output", ImVec2(0.0F, body_height), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (state.has_result && !state.result.ok) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextColored(p.danger, "%s", state.result.error.c_str());
            ImGui::PopTextWrapPos();
        } else if (state.has_result) {
            ImGui::TextUnformatted(state.result.output.data(), state.result.output.data() + state.result.output.size());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        const bool ready = state.has_result && state.result.ok && !state.pending;
        const int clicked = footer_buttons({{with_icon(icon::kPlus, tr(language, UiTextKey::SaveAsRecipe)), false, ready},
                                            {tr(language, UiTextKey::ReplaceClipboard), false, ready},
                                            {with_icon(icon::kCopy, tr(language, UiTextKey::CopyResult)), true, ready}},
                                           state.status);
        if (clicked == 0) ImGui::OpenPopup("save-recipe");
        if (clicked == 1 && host.replace_clipboard) {
            host.replace_clipboard(state.result.output);
            state.status = tr(language, UiTextKey::Copied);
        }
        if (clicked == 2 && host.data.copy_text) {
            host.data.copy_text(state.result.output);
            state.status = tr(language, UiTextKey::Copied);
        }
        if (ImGui::BeginPopup("save-recipe")) {
            ImGui::TextUnformatted(tr(language, UiTextKey::RecipeName).c_str());
            ImGui::SetNextItemWidth(260.0F);
            input_text_string("##recipe-name", state.recipe_name);
            const bool can_save = !state.recipe_name.empty();
            if (!can_save) ImGui::BeginDisabled();
            if (primary_button(tr(language, UiTextKey::Save)) && host.save_recipe) {
                host.save_recipe(PipelineRecipe{.id = "", .name = state.recipe_name, .command = state.command,
                                                .applies_to = "lines", .enabled = true, .built_in = false});
                state.status = tr(language, UiTextKey::Saved);
                ImGui::CloseCurrentPopup();
            }
            if (!can_save) ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

#endif

}  // namespace pasteit
