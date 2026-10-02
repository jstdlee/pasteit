#include "ui/command_palette.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace pasteit;

int main() {
    // Subsequence matching, case-insensitive; misses score -1.
    assert(fuzzy_score("set", "Settings") > 0);
    assert(fuzzy_score("xyz", "Settings") == -1);
    assert(fuzzy_score("SETT", "settings") > 0);
    // Prefix and word starts beat scattered matches.
    assert(fuzzy_score("ct", "Copy text") > fuzzy_score("ct", "Accept"));
    assert(fuzzy_score("theme", "Switch theme") > fuzzy_score("theme", "The menu item"));
    // CJK compares by code point.
    assert(fuzzy_score("设置", "打开设置") > 0 && fuzzy_score("设计", "打开设置") == -1);

    const std::vector<PaletteCommand> commands{
        {.id = "go.settings", .label = "设置", .english = "Settings", .group = "前往"},
        {.id = "theme.switch", .label = "切换主题", .english = "Switch theme", .group = "外观"},
        {.id = "go.history", .label = "剪贴板历史", .english = "Clipboard history", .group = "前往"},
    };
    // English queries still find translated commands.
    auto ranked = rank_commands(commands, "theme", {});
    assert(ranked.size() == 1 && commands[ranked[0]].id == "theme.switch");
    ranked = rank_commands(commands, "历史", {});
    assert(ranked.size() == 1 && commands[ranked[0]].id == "go.history");
    // Empty query: recent first, then the registry order.
    ranked = rank_commands(commands, "", {"go.history"});
    assert(ranked.size() == 3 && ranked[0] == 2 && ranked[1] == 0 && ranked[2] == 1);

    // Long entries need the query as one piece, not scattered letters.
    const std::vector<PaletteCommand> history{
        {.id = "h1", .label = "Hi team, please call Jane at +1 555 0100 or email her", .english = "Hi team, please call Jane at +1 555 0100 or email her"},
        {.id = "a1", .label = "Copy as YAML", .english = "Copy as YAML"}};
    ranked = rank_commands(history, "yaml", {});
    assert(ranked.size() == 1 && history[ranked[0]].id == "a1");
    assert(rank_commands(history, "please call", {}).size() == 1);

    CommandPaletteState state;
    for (int index = 0; index < 10; ++index) remember_command(state, "c" + std::to_string(index));
    remember_command(state, "c5");
    assert(state.recent.size() == 8 && state.recent.front() == "c5");
}
