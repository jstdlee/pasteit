#include "ui/command_palette.hpp"

#include "ui/icons.hpp"
#include "ui/theme.hpp"

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdint>

namespace pasteit {
namespace {

// Code points, ASCII folded to lower case; other scripts compare as they are.
std::vector<std::uint32_t> folded(std::string_view text) {
    std::vector<std::uint32_t> out;
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        length = std::min(length, text.size() - index);
        std::uint32_t point = length == 1 ? lead : lead & (0xFFU >> (length + 1));
        for (std::size_t k = 1; k < length; ++k) point = (point << 6U) | (static_cast<unsigned char>(text[index + k]) & 0x3FU);
        if (point >= 'A' && point <= 'Z') point += 'a' - 'A';
        out.push_back(point);
        index += length;
    }
    return out;
}

bool word_boundary(std::uint32_t previous) {
    return previous == ' ' || previous == '-' || previous == '_' || previous == '/' || previous == '.' || previous == ':' ||
           previous == '(' || previous == 0x203A;  // ›
}

}  // namespace

int fuzzy_score(std::string_view query, std::string_view text) {
    const auto needle = folded(query);
    const auto haystack = folded(text);
    if (needle.empty()) return 0;
    int score = 0;
    std::size_t at = 0;
    std::size_t previous_match = std::string_view::npos;
    for (const auto point : needle) {
        if (point == ' ') continue;  // spaces in the query only separate words
        while (at < haystack.size() && haystack[at] != point) ++at;
        if (at == haystack.size()) return -1;
        score += 10;
        if (at == 0) score += 15;
        else if (word_boundary(haystack[at - 1])) score += 8;
        if (previous_match != std::string_view::npos && at == previous_match + 1) score += 6;
        previous_match = at;
        ++at;
    }
    // The query as one contiguous piece beats letters spread across words.
    std::vector<std::uint32_t> compact;
    for (const auto point : needle) {
        if (point != ' ') compact.push_back(point);
    }
    if (const auto found = std::search(haystack.begin(), haystack.end(), compact.begin(), compact.end()); found != haystack.end()) {
        score += 40;
        if (found == haystack.begin() || word_boundary(*(found - 1))) score += 20;
    }
    // Shorter texts with the same matches rank first.
    score -= static_cast<int>(std::min<std::size_t>(haystack.size(), 60)) / 6;
    return score;
}

std::vector<std::size_t> rank_commands(const std::vector<PaletteCommand>& commands, std::string_view query,
                                       const std::vector<std::string>& recent) {
    std::vector<std::size_t> order;
    if (query.find_first_not_of(' ') == std::string_view::npos) {
        for (const auto& id : recent) {
            const auto found = std::find_if(commands.begin(), commands.end(), [&](const auto& command) { return command.id == id; });
            if (found != commands.end()) order.push_back(static_cast<std::size_t>(found - commands.begin()));
        }
        for (std::size_t index = 0; index < commands.size(); ++index) {
            if (std::find(order.begin(), order.end(), index) == order.end()) order.push_back(index);
        }
        return order;
    }
    std::vector<std::pair<int, std::size_t>> scored;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const auto& command = commands[index];
        int best = std::max(fuzzy_score(query, command.label), fuzzy_score(query, command.english));
        if (const int in_group = fuzzy_score(query, command.group + " " + command.label); in_group >= 0) best = std::max(best, in_group - 5);
        if (const int in_detail = fuzzy_score(query, command.detail); in_detail >= 0) best = std::max(best, in_detail / 2);
        if (best >= 0) scored.emplace_back(best, index);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& left, const auto& right) { return left.first > right.first; });
    for (const auto& [score, index] : scored) order.push_back(index);
    return order;
}

void open_command_palette(CommandPaletteState& state) {
    state.open = true;
    state.focus_pending = true;
    state.query.clear();
    state.selected = 0;
}

void remember_command(CommandPaletteState& state, const std::string& id) {
    std::erase(state.recent, id);
    state.recent.insert(state.recent.begin(), id);
    if (state.recent.size() > 8) state.recent.resize(8);
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
std::string draw_command_palette(CommandPaletteState& state, const std::vector<PaletteCommand>& commands, UiLanguage language) {
    if (!state.open) return {};
    const auto& p = palette();
    const auto& style = ImGui::GetStyle();
    const float scale = style.FontScaleDpi;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(560.0F * scale, viewport->WorkSize.x - 32.0F * scale);
    if (state.focus_pending) ImGui::OpenPopup("##command-palette");
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5F, viewport->WorkPos.y + 48.0F * scale),
                            ImGuiCond_Always, ImVec2(0.5F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0F * scale, 10.0F * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0F * scale);
    const bool visible = ImGui::BeginPopup("##command-palette", ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(2);
    if (!visible) {
        state.open = false;
        return {};
    }
    std::string chosen;
    const auto order = rank_commands(commands, state.query, state.recent);
    state.selected = order.empty() ? 0 : std::clamp(state.selected, 0, static_cast<int>(order.size()) - 1);

    // Query field: the search glyph inside, Esc / arrows / Enter handled here.
    if (state.focus_pending) {
        ImGui::SetKeyboardFocusHere();
        state.focus_pending = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0F * scale, 8.0F * scale));
    char buffer[256];
    const auto copied = state.query.copy(buffer, sizeof buffer - 1);
    buffer[copied] = '\0';
    const auto hint = std::string{ui_fonts().icons ? icon::kSearch : ""} + "  " + tr(language, UiTextKey::PaletteHint);
    if (ImGui::InputTextWithHint("##palette-query", hint.c_str(), buffer, sizeof buffer)) {
        state.query = buffer;
        state.selected = 0;
    }
    ImGui::PopStyleVar();
    bool keyboard_moved = false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !order.empty()) {
        state.selected = (state.selected + 1) % static_cast<int>(order.size());
        keyboard_moved = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !order.empty()) {
        state.selected = (state.selected + static_cast<int>(order.size()) - 1) % static_cast<int>(order.size());
        keyboard_moved = true;
    }
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();

    const float row_height = ImGui::GetFrameHeight() + 4.0F * scale;
    const float list_height = std::min(static_cast<float>(order.size()) * row_height + 12.0F * scale * 4.0F, 380.0F * scale);
    if (order.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(p.text_muted, "%s \"%s\"", tr(language, UiTextKey::PaletteNoResults).c_str(), state.query.c_str());
    } else if (ImGui::BeginChild("##palette-results", ImVec2(0.0F, list_height), ImGuiChildFlags_None)) {
        const std::string* current_group = nullptr;
        for (int rank = 0; rank < static_cast<int>(order.size()); ++rank) {
            const auto& command = commands[order[static_cast<std::size_t>(rank)]];
            // Group headings: tiny grey capitals, as in the settings pages.
            if (current_group == nullptr || *current_group != command.group) {
                current_group = &command.group;
                ImGui::Dummy(ImVec2(0.0F, 2.0F * scale));
                ImGui::PushFont(ui_fonts().regular, ui_fonts().small);
                std::string caps = command.group;
                for (auto& ch : caps) {
                    if (static_cast<unsigned char>(ch) < 0x80) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                }
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0F * scale);
                ImGui::TextColored(p.text_muted, "%s", caps.c_str());
                ImGui::PopFont();
            }
            const bool selected = rank == state.selected;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float row_width = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(rank);
            if (ImGui::InvisibleButton("##row", ImVec2(row_width, row_height)) && command.enabled) chosen = command.id;
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x != 0.0F) state.selected = rank;
            ImGui::PopID();
            if (selected && keyboard_moved) ImGui::SetScrollHereY(0.5F);
            auto* draw = ImGui::GetWindowDrawList();
            if (selected) draw->AddRectFilled(pos, ImVec2(pos.x + row_width, pos.y + row_height), ImGui::GetColorU32(p.accent_soft), 7.0F * scale);
            const float text_y = pos.y + (row_height - ImGui::GetFontSize()) * 0.5F;
            float x = pos.x + 10.0F * scale;
            const float alpha = command.enabled ? 1.0F : 0.45F;
            if (command.glyph != nullptr && ui_fonts().icons) {
                ImVec4 tint = selected ? p.accent : p.text_muted;
                tint.w *= alpha;
                draw->AddText(ImVec2(x, text_y), ImGui::GetColorU32(tint), command.glyph);
            }
            x += 26.0F * scale;
            // Shortcut chips at the right; label and detail clipped before them.
            float right = pos.x + row_width - 10.0F * scale;
            if (!command.keys.empty()) {
                float chips = 0.0F;
                ImGui::PushFont(ui_fonts().regular, ui_fonts().small);
                for (std::size_t start = 0; start < command.keys.size();) {
                    auto end = command.keys.find('+', start + 1);
                    if (end == std::string::npos) end = command.keys.size();
                    chips += ImGui::CalcTextSize(command.keys.substr(start, end - start).c_str()).x + 13.0F * scale;
                    start = end + 1;
                }
                ImGui::PopFont();
                right -= chips;
                const ImVec2 keep = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(right, pos.y + (row_height - ImGui::GetFontSize()) * 0.5F));
                key_chips(command.keys);
                ImGui::SetCursorScreenPos(keep);
                right -= 8.0F * scale;
            }
            const ImVec4 clip(x, pos.y, right, pos.y + row_height);
            ImVec4 text_color = p.text;
            text_color.w *= alpha;
            draw->AddText(nullptr, 0.0F, ImVec2(x, text_y), ImGui::GetColorU32(text_color), command.label.c_str(), nullptr, 0.0F, &clip);
            if (!command.detail.empty()) {
                const float label_width = ImGui::CalcTextSize(command.label.c_str()).x;
                ImGui::PushFont(ui_fonts().regular, ui_fonts().small);
                draw->AddText(nullptr, 0.0F, ImVec2(x + label_width + 10.0F * scale, text_y + 2.0F * scale),
                              ImGui::GetColorU32(p.text_muted), command.detail.c_str(), nullptr, 0.0F, &clip);
                ImGui::PopFont();
            }
        }
    }
    // Rows restore the cursor after their key chips; close the layout with an item.
    if (!order.empty()) {
        ImGui::Dummy(ImVec2(0.0F, 0.0F));
        ImGui::EndChild();
    }
    if (enter && !order.empty()) {
        const auto& command = commands[order[static_cast<std::size_t>(state.selected)]];
        if (command.enabled) chosen = command.id;
    }
    if (!chosen.empty()) {
        remember_command(state, chosen);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return chosen;
}
#endif

}  // namespace pasteit
