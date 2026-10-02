#include "ui/help_view.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

#include <algorithm>
#include <cfloat>
#include <iterator>

namespace pasteit {

std::span<const ShortcutInfo> app_shortcuts() {
    static constexpr ShortcutInfo shortcuts[] = {
        {"Ctrl+Alt+F", UiTextKey::ShortcutOpen, UiTextKey::GroupGeneral},
        {"Ctrl+P", UiTextKey::ShortcutSearch, UiTextKey::GroupGeneral},
        {"Ctrl+K", UiTextKey::ShortcutSearch, UiTextKey::GroupGeneral},
        {"Ctrl+J", UiTextKey::ShortcutTasks, UiTextKey::GroupGeneral},
        {"F1", UiTextKey::ShortcutHelp, UiTextKey::GroupGeneral},
        {"Ctrl+/", UiTextKey::ShortcutShortcuts, UiTextKey::GroupGeneral},
        {"Ctrl+,", UiTextKey::ShortcutSettings, UiTextKey::GroupGeneral},
        {"1-8", UiTextKey::ShortcutRunCard, UiTextKey::GroupCards},
        {"\xE2\x86\x91 \xE2\x86\x93", UiTextKey::ShortcutMove, UiTextKey::GroupCards},
        {"Enter", UiTextKey::ShortcutRunSelected, UiTextKey::GroupCards},
        {"Ctrl+L", UiTextKey::ShortcutAskLlm, UiTextKey::GroupCards},
        {"Ctrl+Enter", UiTextKey::ShortcutApplyEdit, UiTextKey::GroupEditing},
        {"Ctrl+Z", UiTextKey::ShortcutUndoMark, UiTextKey::GroupEditing},
        {"Ctrl+Shift+T", UiTextKey::ShortcutTheme, UiTextKey::GroupWindows},
        {"Esc", UiTextKey::ShortcutEscape, UiTextKey::GroupWindows},
    };
    return shortcuts;
}

std::span<const HelpConcept> help_concepts() {
    // In the order a new user meets them.
    static constexpr HelpConcept concepts[] = {
        {icon::kPaste, UiTextKey::ConceptClipboardTitle, UiTextKey::ConceptClipboardBody, UiTextKey::ConceptClipboardWhere},
        {icon::kZap, UiTextKey::ConceptCardsTitle, UiTextKey::ConceptCardsBody, UiTextKey::ConceptCardsWhere},
        {icon::kGauge, UiTextKey::ConceptRankingTitle, UiTextKey::ConceptRankingBody, UiTextKey::ConceptRankingWhere},
        {icon::kEye, UiTextKey::ConceptPreviewTitle, UiTextKey::ConceptPreviewBody, UiTextKey::ConceptPreviewWhere},
        {icon::kPipeline, UiTextKey::ConceptPipelineTitle, UiTextKey::ConceptPipelineBody, UiTextKey::ConceptPipelineWhere},
        {icon::kShield, UiTextKey::ConceptPrivacyTitle, UiTextKey::ConceptPrivacyBody, UiTextKey::ConceptPrivacyWhere},
        {icon::kListCheck, UiTextKey::ConceptTasksTitle, UiTextKey::ConceptTasksBody, UiTextKey::ConceptTasksWhere},
    };
    return concepts;
}

std::span<const GlossaryTerm> glossary_terms() {
    // A–Z by the English term; "Show me" ids are palette commands.
    static constexpr GlossaryTerm terms[] = {
        {UiTextKey::TermActionCard, UiTextKey::TermActionCardDef, "go.smart"},
        {UiTextKey::TermAnonymize, UiTextKey::TermAnonymizeDef, "settings.7"},
        {UiTextKey::TermAskLlm, UiTextKey::TermAskLlmDef, "settings.3"},
        {UiTextKey::TermClef, UiTextKey::TermClefDef, "settings.2"},
        {UiTextKey::TermPalette, UiTextKey::TermPaletteDef, "app.palette"},
        {UiTextKey::TermCompare, UiTextKey::TermCompareDef, "go.smart"},
        {UiTextKey::TermCustomCommand, UiTextKey::TermCustomCommandDef, "settings.6"},
        {UiTextKey::TermGlobalShortcut, UiTextKey::TermGlobalShortcutDef, "settings.1"},
        {UiTextKey::TermJev, UiTextKey::TermJevDef, "settings.2"},
        {UiTextKey::TermFallback, UiTextKey::TermFallbackDef, "go.smart"},
        {UiTextKey::TermPipeline, UiTextKey::TermPipelineDef, "settings.6"},
        {UiTextKey::TermPlaceholder, UiTextKey::TermPlaceholderDef, "settings.7"},
        {UiTextKey::TermPreview, UiTextKey::TermPreviewDef, "go.smart"},
        {UiTextKey::TermPromptTemplate, UiTextKey::TermPromptTemplateDef, "settings.4"},
        {UiTextKey::TermRecipe, UiTextKey::TermRecipeDef, "settings.6"},
        {UiTextKey::TermTaskQueue, UiTextKey::TermTaskQueueDef, "app.tasks"},
    };
    return terms;
}

std::string shortcuts_as_text(UiLanguage language) {
    std::string out;
    for (const auto& shortcut : app_shortcuts()) {
        std::string keys = shortcut.keys;
        keys.resize(std::max<std::size_t>(keys.size(), 14), ' ');
        out += keys + "  " + tr(language, shortcut.action) + "\n";
    }
    return out;
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
namespace {

bool matches(std::string_view text, std::string_view query) {
    if (query.empty()) return true;
    const auto lower = [](char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; };
    return std::search(text.begin(), text.end(), query.begin(), query.end(), [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
}

}  // namespace

void draw_help_view(HelpViewState& state, UiLanguage language, const std::function<void(std::string_view)>& copy_text,
                    const std::function<void(const std::string&)>& run_command) {
    if (!state.open) return;
    const auto& p = palette();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const auto title = with_icon(icon::kHelp, tr(language, UiTextKey::HelpCommand)) + "###help-view";
    if (begin_tool_window(title, &state.open, ImVec2(720.0F, 560.0F), &state.focus_pending)) {
        // Tabs and the search field share the first row.
        const std::string tabs[] = {tr(language, UiTextKey::HelpConcepts), tr(language, UiTextKey::HelpGlossary),
                                    tr(language, UiTextKey::HelpShortcuts)};
        (void)segmented_control("##help-tabs", tabs, state.tab, true);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        char buffer[128];
        const auto copied = state.search.copy(buffer, sizeof buffer - 1);
        buffer[copied] = '\0';
        const auto hint = std::string{ui_fonts().icons ? icon::kSearch : ""} + "  " + tr(language, UiTextKey::HelpSearchHint);
        if (ImGui::InputTextWithHint("##help-search", hint.c_str(), buffer, sizeof buffer)) state.search = buffer;
        ImGui::Spacing();
        ImGui::BeginChild("##help-body", ImVec2(0.0F, state.tab == 2 ? -footer_height() : 0.0F), ImGuiChildFlags_None);
        int shown = 0;
        if (state.tab == 0) {
            for (const auto& concept_entry : help_concepts()) {
                const auto heading = tr(language, concept_entry.title);
                const auto body = tr(language, concept_entry.body);
                const auto where = tr(language, concept_entry.where);
                if (!matches(heading, state.search) && !matches(body, state.search) && !matches(where, state.search)) continue;
                ++shown;
                begin_group(heading.c_str());
                ImGui::PushFont(ui_fonts().bold, ui_fonts().body);
                ImGui::TextColored(p.accent, "%s", with_icon(concept_entry.glyph, heading).c_str());
                ImGui::PopFont();
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextUnformatted(body.c_str());
                ImGui::PushFont(ui_fonts().regular, ui_fonts().small);
                ImGui::TextColored(p.text_muted, "%s: %s", tr(language, UiTextKey::WhereYouSeeIt).c_str(), where.c_str());
                ImGui::PopFont();
                ImGui::PopTextWrapPos();
                end_group();
                ImGui::Spacing();
            }
        } else if (state.tab == 1) {
            // Terms sorted by their name in the current language.
            std::vector<const GlossaryTerm*> terms;
            for (const auto& term : glossary_terms()) terms.push_back(&term);
            std::sort(terms.begin(), terms.end(), [&](const auto* a, const auto* b) { return tr(language, a->term) < tr(language, b->term); });
            begin_group("##glossary");
            for (const auto* term : terms) {
                const auto name = tr(language, term->term);
                const auto definition = tr(language, term->definition);
                if (!matches(name, state.search) && !matches(definition, state.search) &&
                    !matches(tr(UiLanguage::English, term->term), state.search)) {
                    continue;
                }
                if (shown++ > 0) ImGui::Separator();
                ImGui::PushID(name.c_str());
                ImGui::PushFont(ui_fonts().bold, ui_fonts().body);
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopFont();
                if (term->show_me != nullptr && run_command) {
                    const auto label = tr(language, UiTextKey::ShowMe);
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - button_width(label)));
                    if (ImGui::SmallButton(label.c_str())) run_command(term->show_me);
                }
                ImGui::PushTextWrapPos(0.0F);
                ImGui::TextColored(p.text_muted, "%s", definition.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopID();
            }
            end_group();
        } else {
            // Generated from app_shortcuts(), grouped.
            UiTextKey group = UiTextKey::GroupGeneral;
            bool open_group = false;
            for (const auto& shortcut : app_shortcuts()) {
                const auto action = tr(language, shortcut.action);
                if (!matches(action, state.search) && !matches(shortcut.keys, state.search)) continue;
                if (!open_group || shortcut.group != group) {
                    if (open_group) end_group();
                    group = shortcut.group;
                    separator_heading(tr(language, group));
                    begin_group(tr(language, group).c_str());
                    open_group = true;
                } else {
                    ImGui::Separator();
                }
                ++shown;
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(action.c_str());
                ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0F * scale, ImGui::GetContentRegionMax().x - 170.0F * scale));
                key_chips(shortcut.keys);
            }
            if (open_group) end_group();
        }
        if (shown == 0) ImGui::TextColored(p.text_muted, "%s", tr(language, UiTextKey::HelpNoMatches).c_str());
        ImGui::EndChild();
        if (state.tab == 2) {
            const int clicked = footer_buttons({{with_icon(icon::kCopy, tr(language, UiTextKey::CopyShortcuts))}});
            if (clicked == 0 && copy_text) copy_text(shortcuts_as_text(language));
        }
    }
    ImGui::End();
}
#endif

}  // namespace pasteit
