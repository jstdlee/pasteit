#pragma once

#include "ui/localization.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pasteit {

// One entry of the command registry. The palette, the utility-cluster
// tooltips and Help › Shortcuts read the same list.
struct PaletteCommand {
    std::string id;
    std::string label;    // in the UI language
    std::string english;  // also matched, so English queries work in any locale
    std::string group;    // section heading, in the UI language
    const char* glyph = nullptr;
    std::string keys;     // "Ctrl+," style; empty when there is none
    std::string detail;   // grey second text
    bool enabled = true;
};

// Case-insensitive subsequence match: -1 when query is not a subsequence of
// text; otherwise higher is better (prefix, word starts, contiguous runs).
int fuzzy_score(std::string_view query, std::string_view text);

// Indexes of the commands to show. An empty query lists the recent commands
// first, then the rest in registry order; otherwise the matches, best first.
std::vector<std::size_t> rank_commands(const std::vector<PaletteCommand>& commands, std::string_view query,
                                       const std::vector<std::string>& recent);

struct CommandPaletteState {
    bool open = false;
    bool focus_pending = false;
    std::string query;
    int selected = 0;
    std::vector<std::string> recent;  // newest first
};

void open_command_palette(CommandPaletteState& state);
// Moves id to the front of the recent list (at most 8 kept).
void remember_command(CommandPaletteState& state, const std::string& id);

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
// Draws the palette when open; returns the id of the command to run, or "".
std::string draw_command_palette(CommandPaletteState& state, const std::vector<PaletteCommand>& commands, UiLanguage language);
#endif

}  // namespace pasteit
