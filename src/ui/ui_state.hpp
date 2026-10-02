#pragma once

#include <filesystem>

namespace pasteit {

// Window layout the user adjusted by hand, kept between runs
// (data/ui-state.json next to the executable).
struct UiLayoutState {
    float settings_sidebar = 158.0F;  // logical px (before DPI scaling)
    int popup_width = 660;            // physical px
    bool operator==(const UiLayoutState&) const = default;
};

UiLayoutState load_ui_layout(const std::filesystem::path& file);
bool save_ui_layout(const std::filesystem::path& file, const UiLayoutState& state);

}  // namespace pasteit
