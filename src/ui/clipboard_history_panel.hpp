#pragma once

#include "ui/clipboard_history_model.hpp"
#include "ui/localization.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace pasteit {

struct ClipboardTexture {
    std::uintptr_t handle = 0;
    int width = 0;
    int height = 0;
};

using ClipboardTextureLookup = std::function<std::optional<ClipboardTexture>(std::string_view ref)>;

ClipboardHistoryCommand render_clipboard_history_panel(ClipboardHistoryState& state,
                                                       const ClipboardHistoryModel& model,
                                                       UiLanguage language,
                                                       ClipboardTextureLookup texture_lookup = {});

}  // namespace pasteit
