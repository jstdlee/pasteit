#include "ui/privacy_view.hpp"

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include <imgui.h>
#endif

#include <map>

namespace pastit {

void open_anonymize_view(AnonymizeViewState& state, std::string source, const AnonymizeOptions& options) {
    state = {};
    state.open = true;
    state.focus_pending = true;
    state.findings = find_pii(source, options);
    state.selected.assign(state.findings.size(), true);
    state.style = static_cast<int>(options.style);
    state.source = std::move(source);
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)

void draw_anonymize_view(AnonymizeViewState& state, const AnonymizeViewHost& host, UiLanguage language) {
    if (!state.open) return;
    if (state.dirty) {
        state.dirty = false;
        std::vector<PiiFinding> chosen;
        for (std::size_t index = 0; index < state.findings.size(); ++index) {
            if (state.selected[index]) chosen.push_back(state.findings[index]);
        }
        state.output = apply_anonymization(state.source, chosen, static_cast<ReplacementStyle>(state.style), host.vault).text;
    }
    const auto title = with_icon(icon::kEyeOff, tr(language, UiTextKey::Anonymize)) + "###anonymize-view";
    if (begin_tool_window(title, &state.open, ImVec2(900.0F, 600.0F), &state.focus_pending)) {
        const auto& p = palette();
        const std::string styles[] = {tr(language, UiTextKey::StylePlaceholder), tr(language, UiTextKey::StyleMask),
                                      tr(language, UiTextKey::StyleFake), tr(language, UiTextKey::StyleRedact)};
        for (int index = 0; index < 4; ++index) {
            if (index) ImGui::SameLine(0.0F, 4.0F);
            const bool active = state.style == index;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, p.accent_soft);
            if (ImGui::Button(styles[index].c_str())) {
                state.style = index;
                state.dirty = true;
            }
            if (active) ImGui::PopStyleColor();
        }
        // Category toggles with counts.
        std::map<PiiCategory, std::pair<std::size_t, std::size_t>> counts;  // selected, total
        for (std::size_t index = 0; index < state.findings.size(); ++index) {
            auto& entry = counts[state.findings[index].category];
            entry.second++;
            if (state.selected[index]) entry.first++;
        }
        for (const auto& [category, count] : counts) {
            bool all = count.first == count.second;
            const auto label = pii_category_label(category) + " " + std::to_string(count.second) + "##cat" + pii_category_name(category);
            const float width = ImGui::GetFrameHeight() + ImGui::CalcTextSize(label.c_str(), nullptr, true).x + 24.0F;
            ImGui::SameLine(0.0F, 10.0F);
            if (ImGui::GetCursorPosX() + width > ImGui::GetContentRegionMax().x) ImGui::NewLine();
            if (ImGui::Checkbox(label.c_str(), &all)) {
                for (std::size_t index = 0; index < state.findings.size(); ++index) {
                    if (state.findings[index].category == category) state.selected[index] = all;
                }
                state.dirty = true;
            }
        }
        const float body_height = ImGui::GetContentRegionAvail().y - footer_height();
        const float list_width = 300.0F * ImGui::GetStyle().FontScaleDpi;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
        ImGui::BeginChild("anonymize-findings", ImVec2(list_width, body_height), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (state.findings.empty()) ImGui::TextColored(p.text_muted, "%s", tr(language, UiTextKey::NothingFound).c_str());
        for (std::size_t index = 0; index < state.findings.size(); ++index) {
            const auto& finding = state.findings[index];
            ImGui::PushID(static_cast<int>(index));
            bool keep = state.selected[index];
            if (ImGui::Checkbox("##pick", &keep)) {
                state.selected[index] = keep;
                state.dirty = true;
            }
            ImGui::SameLine();
            pill(pii_category_name(finding.category), keep ? p.warning : p.text_muted);
            ImGui::SameLine();
            auto shown = finding.text.substr(0, 60);
            for (auto& ch : shown) if (ch == '\n') ch = ' ';
            ImGui::TextColored(keep ? p.text : p.text_muted, "%s", shown.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("anonymize-output", ImVec2(0.0F, body_height), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(state.output.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        std::size_t selected = 0;
        for (const bool value : state.selected) selected += value ? 1 : 0;
        const auto status = std::to_string(selected) + " / " + std::to_string(state.findings.size()) + " " +
                            tr(language, UiTextKey::Replaced) + (state.status.empty() ? "" : "  \xC2\xB7  " + state.status);
        const int clicked = footer_buttons({{tr(language, UiTextKey::ReplaceClipboard)},
                                            {with_icon(icon::kCopy, tr(language, UiTextKey::CopyResult)), true}},
                                           status);
        if (clicked == 0 && host.replace_clipboard) {
            host.replace_clipboard(state.output);
            state.status = tr(language, UiTextKey::Copied);
        }
        if (clicked == 1 && host.data.copy_text) {
            host.data.copy_text(state.output);
            state.status = tr(language, UiTextKey::Copied);
        }
    }
    ImGui::End();
}

#endif

}  // namespace pastit
