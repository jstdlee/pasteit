#pragma once

#include "ui/localization.hpp"

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace pasteit {

// One keyboard shortcut; Help › Shortcuts, the palette and tooltips read
// the same list.
struct ShortcutInfo {
    const char* keys;
    UiTextKey action;
    UiTextKey group;
};
std::span<const ShortcutInfo> app_shortcuts();

struct HelpConcept {
    const char* glyph;
    UiTextKey title;
    UiTextKey body;
    UiTextKey where;
};
std::span<const HelpConcept> help_concepts();

struct GlossaryTerm {
    UiTextKey term;
    UiTextKey definition;
    const char* show_me;  // command id that brings the term's UI into view, or nullptr
};
std::span<const GlossaryTerm> glossary_terms();

struct HelpViewState {
    bool open = false;
    bool focus_pending = false;
    int tab = 0;  // 0 concepts, 1 glossary, 2 shortcuts
    std::string search;
};

// Text for copying the shortcut list ("Ctrl+P  Search actions…").
std::string shortcuts_as_text(UiLanguage language);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
// F1 window. `run_command` runs a palette command id ("Show me").
void draw_help_view(HelpViewState& state, UiLanguage language, const std::function<void(std::string_view)>& copy_text,
                    const std::function<void(const std::string&)>& run_command);
#endif

}  // namespace pasteit
