#include "ui/ui_state.hpp"

#include "util/json.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace pasteit {

UiLayoutState load_ui_layout(const std::filesystem::path& file) {
    UiLayoutState state;
    std::ifstream input(file, std::ios::binary);
    if (!input) return state;
    std::stringstream buffer;
    buffer << input.rdbuf();
    const auto root = parse_json(buffer.str());
    if (!root) return state;
    if (const auto* sidebar = root->get("settings_sidebar")) {
        state.settings_sidebar = std::clamp(static_cast<float>(sidebar->number().value_or(state.settings_sidebar)), 120.0F, 320.0F);
    }
    if (const auto* width = root->get("popup_width")) {
        state.popup_width = std::clamp(static_cast<int>(width->number().value_or(state.popup_width)), 480, 1600);
    }
    return state;
}

bool save_ui_layout(const std::filesystem::path& file, const UiLayoutState& state) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output << "{\n\"settings_sidebar\":" << state.settings_sidebar << ",\n\"popup_width\":" << state.popup_width << "\n}\n";
    return static_cast<bool>(output);
}

}  // namespace pasteit
