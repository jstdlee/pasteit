#include "ui/pipeline_view.hpp"

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include "ui/icons.hpp"
#include "ui/imgui_widgets.hpp"
#include "ui/theme.hpp"

#include <imgui.h>
#endif

#include <algorithm>

namespace pastit {

void open_pipeline_view(PipelineViewState& state, std::string input, std::string command) {
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

#if defined(PASTIT_HAS_DESKTOP_DEPS)

namespace {

std::size_t count_lines(const std::string& text) {
    return text.empty() ? 0 : static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
}

}  // namespace

void draw_pipeline_view(PipelineViewState& state, const PipelineOptions& options, const PipelineViewHost& host,
                        UiLanguage language) {
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
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (input_text_string("##pipeline-command", state.command)) state.edited = now;
        // Quick inserts append a stage.
        static const char* quick[] = {"sort", "sort -nr", "uniq -c", "wc -l", "head -n 10", "grep -i ''", "cut -d , -f 1",
                                      "sed 's/a/b/g'", "tr a-z A-Z", "column", "anonymize"};
        for (std::size_t index = 0; index < std::size(quick); ++index) {
            if (index) ImGui::SameLine(0.0F, 4.0F);
            if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(quick[index]).x + 24.0F > ImGui::GetContentRegionMax().x) ImGui::NewLine();
            if (ImGui::SmallButton(quick[index])) {
                state.command += state.command.empty() ? quick[index] : std::string{" | "} + quick[index];
                state.edited = now;
            }
            if (ImGui::IsItemHovered()) {
                const std::string name{quick[index], std::string_view{quick[index]}.find(' ')};
                ImGui::SetTooltip("%s", pipeline_command_help(name).c_str());
            }
        }
        for (const auto& [name, expansion] : options.custom_commands) {
            ImGui::SameLine(0.0F, 4.0F);
            if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(name.c_str()).x + 24.0F > ImGui::GetContentRegionMax().x) ImGui::NewLine();
            ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
            if (ImGui::SmallButton((name + "##custom").c_str())) {
                state.command += (state.command.empty() ? "" : " | ") + name;
                state.edited = now;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", expansion.c_str());
        }
        for (const auto& tool : options.allowed_tools) {
            ImGui::SameLine(0.0F, 4.0F);
            if (ImGui::GetCursorPosX() + ImGui::CalcTextSize(tool.c_str()).x + 24.0F > ImGui::GetContentRegionMax().x) ImGui::NewLine();
            ImGui::PushStyleColor(ImGuiCol_Text, p.warning);
            if (ImGui::SmallButton(tool.c_str())) {
                state.command += (state.command.empty() ? "" : " | ") + tool + (tool == "jq" ? " '.'" : " '{ print }'");
                state.edited = now;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(language, UiTextKey::ExternalTool).c_str());
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

}  // namespace pastit
