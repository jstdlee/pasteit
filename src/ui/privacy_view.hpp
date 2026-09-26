#pragma once

#include "privacy/anonymizer.hpp"
#include "ui/data_views.hpp"
#include "ui/localization.hpp"

#include <string>
#include <vector>

namespace pasteit {

struct AnonymizeViewState {
    bool open = false;
    bool focus_pending = false;
    std::string source;
    std::vector<PiiFinding> findings;
    std::vector<bool> selected;  // per finding
    int style = 0;               // ReplacementStyle
    bool dirty = true;
    std::string output;
    std::string status;
    int prompt_choice = -1;  // index into the host's templates; -1 = custom
    std::string custom_prompt;
};

void open_anonymize_view(AnonymizeViewState& state, std::string source, const AnonymizeOptions& options);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
struct AnonymizeViewHost {
    DataViewHost data;
    std::function<void(std::string_view)> replace_clipboard;
    PlaceholderVault* vault = nullptr;
    // Prompt templates offered for "Ask LLM"; names shown, ids passed back.
    std::vector<std::pair<std::string, std::string>> templates;  // id, name
    // Sends placeholder text; the answer is restored to real values.
    std::function<void(const std::string& template_id, const std::string& custom_prompt, const std::string& text)> ask_llm;
};

// Shows every finding highlighted in a list; unticking keeps that value.
void draw_anonymize_view(AnonymizeViewState& state, const AnonymizeViewHost& host, UiLanguage language);
#endif

}  // namespace pasteit
