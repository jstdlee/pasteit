#pragma once

#include "config/app_settings.hpp"
#include "decision/action_category.hpp"

#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pasteit {

// Path shown to users: home directory as "~", middle elided to max_chars.
std::string display_path(std::string_view path, std::size_t max_chars = 64);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)

struct UiPalette {
    ImVec4 background;
    ImVec4 surface;
    ImVec4 surface_hover;
    ImVec4 border;
    ImVec4 text;
    ImVec4 text_muted;
    ImVec4 accent;
    ImVec4 accent_soft;
    ImVec4 track;  // segmented-control track, fields and dropdown pills
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
    ImVec4 divider;      // 1 px between rows
    ImVec4 pill;         // raised surfaces: selected segment, push buttons
    ImVec4 pill_border;
    ImVec4 pill_hover;
    ImVec4 pill_active;
    ImVec4 field_hover;
    ImVec4 popup;
    ImVec4 accent_2;     // second accent (gradients, the feature highlight ring)
    bool light = false;  // washes are black over light themes, white over dark ones
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
// System resolves through system_prefers_dark().
void apply_theme(UiTheme theme, float dpi_scale);
// The desktop's light/dark preference: Windows AppsUseLightTheme, the
// freedesktop/GNOME color-scheme on Linux. Dark when unknown.
bool system_prefers_dark();
const UiPalette& palette();
const UiFonts& ui_fonts();

ImVec4 category_color(ActionCategory category);
void draw_category_icon(ImDrawList* draw, ImVec2 center, float size, ActionCategory category, ImU32 color);

// Draws an icon glyph in a size x size cell and advances the cursor; returns
// false (drawing nothing) when the icon font is unavailable.
bool icon_cell(const char* glyph, ImVec4 color, float size = 18.0F);
// Square, borderless toolbar button (frame height) with a tooltip; falls
// back to the text label. `active` shows it pressed in, for toggles.
bool icon_button(const char* id, const char* glyph, const std::string& tooltip, bool active = false,
                 std::string_view keys = {});

// Seconds the pointer rests on a control before its tooltip shows.
inline constexpr float kTooltipDelaySeconds = 2.0F;
// Tooltip with the action's shortcut drawn as key chips: "Search  [Ctrl] [P]".
void tooltip_with_keys(std::string_view text, std::string_view keys = {});
// Inline key chips for "Ctrl+Shift+P"-style shortcuts.
void key_chips(std::string_view keys);
// Width of `count` icon buttons laid out with SameLine().
float icon_buttons_width(int count);

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
    // Text actions: preview the result in the input, or compare it side by side.
    bool previewable = false;
    bool previewing = false;  // the input shows this action's result
    std::string_view preview_tooltip;
    std::string_view restore_tooltip;
    std::string_view compare_tooltip;
};

enum class ActionCardEvent { None, Activate, Preview, Compare };

inline constexpr float kActionCardHeight = 56.0F;

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
// Button width with the macOS minimum push-button width applied.
float button_width(const std::string& label);
// GPU HUD meter: rounded track with an accent fill that turns amber at 70%
// and red at 90% when `load` is set, and optional centred overlay text.
void draw_meter(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction, ImVec4 fill, float alpha = 1.0F);
void meter(float fraction, std::string_view overlay = {}, float height = 0.0F, bool load = false);
// Sidebar entry: full-width rounded highlight on hover/selection, label
// left-aligned and vertically centred.
bool nav_item(const char* id, std::string_view label, bool selected);
// Bold group title placed above a group box.
void separator_heading(std::string_view title);
void section_heading(const char* glyph, std::string_view title, std::string_view help = {});

// macOS segmented control: one raised segment in a rounded track. Returns
// true when the selection changed.
// `compact` is the in-row size: one frame tall, tighter segments.
bool segmented_control(const char* id, std::span<const std::string> labels, int& selected, bool compact = false);
// A pick-one setting: compact segments when every option fits the available
// width, otherwise a dropdown. Returns true when the selection changed.
bool choice_control(const char* id, std::span<const std::string> labels, int& selected);

// Vertical splitter between two panes: drag to resize `size` (logical px,
// clamped to [min_size, max_size]), double-click to reset. Draws a 1 px
// divider that turns accent while hovered or dragged. Returns true while it
// changes the size.
bool vertical_splitter(const char* id, float& size, float min_size, float max_size, float default_size, float height);

// Rounded group box that sizes to its content. Always pair with end_group().
bool begin_group(const char* id);
void end_group();

// macOS switch for an on/off setting; returns true when toggled.
// Text before "##" in label is drawn to the right of the switch (like a
// checkbox label) and is part of the hit target.
bool toggle_switch(const char* label, bool* value);

// BeginCombo with a slim chevron in place of ImGui's filled arrow button.
// Pair with ImGui::EndCombo() when it returns true, like BeginCombo.
bool begin_combo(const char* id, const char* preview, ImGuiComboFlags flags = 0);

// Two-column form inside a group box: labels on the left, full-width fields
// on the right, hairlines between rows.
bool begin_form(const char* id, float label_width = 150.0F);
void form_row(std::string_view label, std::string_view help = {});
void end_form();

// Draws one ranked action as a card and reports a click on it or on its
// preview / compare icons.
ActionCardEvent action_card(const char* id, const ActionCardModel& model);

#endif

}  // namespace pasteit
