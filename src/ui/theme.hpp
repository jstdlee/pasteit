#pragma once

#include "config/app_settings.hpp"
#include "decision/action_category.hpp"

#include <filesystem>
#include <initializer_list>
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
    bool icons = false;  // Lucide glyphs merged into both fonts
    float body = 16.0F;
    float small = 13.0F;
    float heading = 18.0F;
};

// Loads the regular and bold variant of the first available UI font and
// merges the Lucide icon font (next to the executable) into both.
UiFonts load_ui_fonts(const std::vector<std::filesystem::path>& candidates,
                      const std::filesystem::path& icon_font = {});
const char* category_icon(ActionCategory category);
// Text with a leading icon when the icon font is available.
std::string with_icon(const char* glyph, std::string_view text);
void apply_theme(UiTheme theme, float dpi_scale);
const UiPalette& palette();
const UiFonts& ui_fonts();

ImVec4 category_color(ActionCategory category);
void draw_category_icon(ImDrawList* draw, ImVec2 center, float size, ActionCategory category, ImU32 color);

// Draws an icon glyph in a size x size cell and advances the cursor; returns
// false (drawing nothing) when the icon font is unavailable.
bool icon_cell(const char* glyph, ImVec4 color, float size = 18.0F);
// Small icon button with a tooltip; falls back to the text label.
bool icon_button(const char* id, const char* glyph, const std::string& tooltip);

// Rounded pill label; returns its width.
float pill(std::string_view text, ImVec4 color, bool filled = false);

// Specific glyph for an action kind, or its category glyph.
const char* action_icon(ActionKind kind);

struct ActionCardModel {
    const char* glyph = nullptr;  // overrides the category icon when set
    std::string_view label;
    std::string_view detail;
    ActionCategory category = ActionCategory::Convert;
    double probability = 0.0;
    int shortcut = 0;  // 1-9, 0 for none
    bool selected = false;
    bool enabled = true;
};

inline constexpr float kActionCardHeight = 52.0F;

// --- Shared sub-window layout -------------------------------------------
// Opens an independent, top-most sub-window: centered and focused when
// focus_pending is set, sized on first appearance, never smaller than a
// usable minimum, and closed by Esc while focused. Always pair with
// ImGui::End(), like ImGui::Begin.
bool begin_tool_window(const std::string& title, bool* open, ImVec2 size, bool* focus_pending = nullptr,
                       ImGuiWindowFlags extra_flags = 0);

struct FooterButton {
    std::string label;
    bool primary = false;
    bool enabled = true;
};
// Height to reserve below scrolling bodies: BeginChild(..., ImVec2(0, -footer_height())).
float footer_height();
// Right-aligned button row at the bottom; the primary button uses the
// accent color. Returns the clicked index or -1.
int footer_buttons(std::initializer_list<FooterButton> buttons, std::string_view status = {});
bool primary_button(const std::string& label);
void section_heading(const char* glyph, std::string_view title, std::string_view help = {});

// Two-column form: labels on the left, full-width fields on the right.
bool begin_form(const char* id, float label_width = 190.0F);
void form_row(std::string_view label, std::string_view help = {});
void end_form();

// Draws one ranked action as a card; returns true when clicked.
bool action_card(const char* id, const ActionCardModel& model);

#endif

}  // namespace pastit
