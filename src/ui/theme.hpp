#pragma once

#include "config/app_settings.hpp"
#include "decision/action_category.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(PASTIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pastit {

// Path shown to users: home directory as "~", middle elided to max_chars.
std::string display_path(std::string_view path, std::size_t max_chars = 64);

#if defined(PASTIT_HAS_DESKTOP_DEPS)

struct UiPalette {
    ImVec4 background;
    ImVec4 surface;
    ImVec4 surface_hover;
    ImVec4 border;
    ImVec4 text;
    ImVec4 text_muted;
    ImVec4 accent;
    ImVec4 accent_soft;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
};

struct UiFonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    float body = 16.0F;
    float small = 13.0F;
    float heading = 18.0F;
};

// Loads the regular and bold variant of the first available UI font.
UiFonts load_ui_fonts(const std::vector<std::filesystem::path>& candidates);
void apply_theme(UiTheme theme, float dpi_scale);
const UiPalette& palette();
const UiFonts& ui_fonts();

ImVec4 category_color(ActionCategory category);
void draw_category_icon(ImDrawList* draw, ImVec2 center, float size, ActionCategory category, ImU32 color);

// Rounded pill label; returns its width.
float pill(std::string_view text, ImVec4 color, bool filled = false);

struct ActionCardModel {
    std::string_view label;
    std::string_view detail;
    ActionCategory category = ActionCategory::Convert;
    double probability = 0.0;
    int shortcut = 0;  // 1-9, 0 for none
    bool selected = false;
    bool enabled = true;
};

inline constexpr float kActionCardHeight = 52.0F;

// Draws one ranked action as a card; returns true when clicked.
bool action_card(const char* id, const ActionCardModel& model);

#endif

}  // namespace pastit
