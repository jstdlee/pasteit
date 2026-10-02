#pragma once

#include "ui/localization.hpp"
#include "util/text_diff.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace pasteit {

// Side-by-side comparison of the clipboard text and an action's result.
struct DiffViewState {
    bool open = false;
    bool focus_pending = false;
    std::string title;  // the action's label
    std::string left;
    std::string right;
    TextDiff diff;
    std::string status;
};

void open_diff_view(DiffViewState& state, std::string title, std::string left, std::string right);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
// Footer offers Copy result and Preview in input (on_preview).
void draw_diff_view(DiffViewState& state, UiLanguage language, const std::function<void(std::string_view)>& copy_text,
                    const std::function<void()>& on_preview);
#endif

}  // namespace pasteit
