#pragma once

#include "privacy/anonymizer.hpp"
#include "ui/data_views.hpp"
#include "ui/localization.hpp"

#include <string>
#include <vector>

namespace pastit {

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
};

void open_anonymize_view(AnonymizeViewState& state, std::string source, const AnonymizeOptions& options);

#if defined(PASTIT_HAS_DESKTOP_DEPS)
struct AnonymizeViewHost {
    DataViewHost data;
    std::function<void(std::string_view)> replace_clipboard;
    PlaceholderVault* vault = nullptr;
};

// Shows every finding highlighted in a list; unticking keeps that value.
void draw_anonymize_view(AnonymizeViewState& state, const AnonymizeViewHost& host, UiLanguage language);
#endif

}  // namespace pastit
