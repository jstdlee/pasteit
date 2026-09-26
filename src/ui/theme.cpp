#include "ui/theme.hpp"

#include "util/path_utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace pastit {

std::string display_path(std::string_view path, std::size_t max_chars) {
    std::string out{path};
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (home != nullptr && *home != '\0') {
        const std::string_view prefix{home};
        if (out.starts_with(prefix) && (out.size() == prefix.size() || out[prefix.size()] == '/' ||
                                        out[prefix.size()] == '\\')) {
            out = "~" + out.substr(prefix.size());
        }
    }
    if (out.size() <= max_chars || max_chars < 8) return out;
    // Keep the start and the (more informative) end; cut on UTF-8 boundaries.
    const std::size_t keep_tail = max_chars * 2 / 3;
    std::size_t head = max_chars - keep_tail - 1;
    std::size_t tail = out.size() - keep_tail;
    while (head > 0 && (static_cast<unsigned char>(out[head]) & 0xC0U) == 0x80U) --head;
    while (tail < out.size() && (static_cast<unsigned char>(out[tail]) & 0xC0U) == 0x80U) ++tail;
    return out.substr(0, head) + "\xE2\x80\xA6" + out.substr(tail);
}

#if defined(PASTIT_HAS_DESKTOP_DEPS)

namespace {

UiPalette current_palette;
UiFonts current_fonts;

constexpr ImVec4 rgb(int hex, float alpha = 1.0F) {
    return ImVec4(static_cast<float>((hex >> 16) & 0xFF) / 255.0F, static_cast<float>((hex >> 8) & 0xFF) / 255.0F,
                  static_cast<float>(hex & 0xFF) / 255.0F, alpha);
}

ImVec4 with_alpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

std::filesystem::path bold_variant(const std::filesystem::path& regular) {
    auto name = path_to_utf8_string(regular.filename());
    for (const auto& [from, to] : {std::pair<std::string, std::string>{"Regular", "Bold"}, {"msyh.", "msyhbd."},
                                   {"arial.", "arialbd."}, {"segoeui.", "segoeuib."}}) {
        if (const auto at = name.find(from); at != std::string::npos) {
            auto candidate = regular.parent_path() / path_from_utf8_string(name.replace(at, from.size(), to));
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) return candidate;
            name = path_to_utf8_string(regular.filename());
        }
    }
    return {};
}

}  // namespace

UiFonts load_ui_fonts(const std::vector<std::filesystem::path>& candidates) {
    UiFonts fonts;
    auto& atlas = *ImGui::GetIO().Fonts;
    for (const auto& path : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) continue;
        ImFontConfig config;
        config.OversampleH = 2;
        fonts.regular = atlas.AddFontFromFileTTF(path_to_utf8_string(path).c_str(), fonts.body, &config);
        if (fonts.regular == nullptr) continue;
        if (const auto bold = bold_variant(path); !bold.empty()) {
            fonts.bold = atlas.AddFontFromFileTTF(path_to_utf8_string(bold).c_str(), fonts.body, &config);
        }
        break;
    }
    if (fonts.regular == nullptr) fonts.regular = atlas.AddFontDefault();
    if (fonts.bold == nullptr) fonts.bold = fonts.regular;
    ImGui::GetIO().FontDefault = fonts.regular;
    current_fonts = fonts;
    return fonts;
}

const UiFonts& ui_fonts() {
    return current_fonts;
}

const UiPalette& palette() {
    return current_palette;
}

void apply_theme(UiTheme theme, float dpi_scale) {
    const bool light = theme == UiTheme::Light;
    current_palette = light ? UiPalette{
        .background = rgb(0xF6F7F9),
        .surface = rgb(0xFFFFFF),
        .surface_hover = rgb(0xEEF1F6),
        .border = rgb(0xDDE1E8),
        .text = rgb(0x1C2027),
        .text_muted = rgb(0x667085),
        .accent = rgb(0x3D63F5),
        .accent_soft = rgb(0x3D63F5, 0.12F),
        .success = rgb(0x1F9D6B),
        .warning = rgb(0xC98A12),
        .danger = rgb(0xD64545),
    } : UiPalette{
        .background = rgb(0x15171C),
        .surface = rgb(0x1E2128),
        .surface_hover = rgb(0x272B34),
        .border = rgb(0x2F343E),
        .text = rgb(0xE6E8EC),
        .text_muted = rgb(0x8B93A1),
        .accent = rgb(0x7B96FF),
        .accent_soft = rgb(0x7B96FF, 0.16F),
        .success = rgb(0x3FB984),
        .warning = rgb(0xE0A43B),
        .danger = rgb(0xE5534B),
    };
    const auto& p = current_palette;

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.WindowPadding = ImVec2(16.0F, 14.0F);
    style.FramePadding = ImVec2(10.0F, 6.0F);
    style.ItemSpacing = ImVec2(8.0F, 8.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 6.0F);
    style.CellPadding = ImVec2(8.0F, 5.0F);
    style.ScrollbarSize = 10.0F;
    style.GrabMinSize = 10.0F;
    style.WindowRounding = 10.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.PopupRounding = 8.0F;
    style.ScrollbarRounding = 8.0F;
    style.GrabRounding = 6.0F;
    style.TabRounding = 6.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextPadding = ImVec2(0.0F, 4.0F);
    style.FontSizeBase = current_fonts.body;
    style.ScaleAllSizes(dpi_scale);
    style.FontScaleDpi = dpi_scale;

    auto* c = style.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.text_muted;
    c[ImGuiCol_WindowBg] = p.background;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = p.surface;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = p.surface;
    c[ImGuiCol_FrameBgHovered] = p.surface_hover;
    c[ImGuiCol_FrameBgActive] = mix(p.surface_hover, p.accent, 0.15F);
    c[ImGuiCol_TitleBg] = p.background;
    c[ImGuiCol_TitleBgActive] = p.surface;
    c[ImGuiCol_TitleBgCollapsed] = p.background;
    c[ImGuiCol_MenuBarBg] = p.surface;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.border;
    c[ImGuiCol_ScrollbarGrabHovered] = mix(p.border, p.text_muted, 0.4F);
    c[ImGuiCol_ScrollbarGrabActive] = p.text_muted;
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = mix(p.accent, p.text, 0.2F);
    c[ImGuiCol_Button] = p.surface_hover;
    c[ImGuiCol_ButtonHovered] = mix(p.surface_hover, p.accent, 0.25F);
    c[ImGuiCol_ButtonActive] = mix(p.surface_hover, p.accent, 0.45F);
    c[ImGuiCol_Header] = p.accent_soft;
    c[ImGuiCol_HeaderHovered] = with_alpha(p.accent, 0.22F);
    c[ImGuiCol_HeaderActive] = with_alpha(p.accent, 0.30F);
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = p.accent;
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = with_alpha(p.accent, 0.2F);
    c[ImGuiCol_ResizeGripHovered] = with_alpha(p.accent, 0.5F);
    c[ImGuiCol_ResizeGripActive] = p.accent;
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = p.surface_hover;
    c[ImGuiCol_TabSelected] = p.surface;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = p.surface;
    c[ImGuiCol_TabDimmedSelectedOverline] = with_alpha(p.accent, 0.5F);
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = mix(p.accent, p.text, 0.2F);
    c[ImGuiCol_TableHeaderBg] = p.surface;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = with_alpha(p.border, 0.6F);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = with_alpha(p.surface, 0.5F);
    c[ImGuiCol_TextSelectedBg] = with_alpha(p.accent, 0.35F);
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, light ? 0.2F : 0.45F);
}

ImVec4 category_color(ActionCategory category) {
    switch (category) {
        case ActionCategory::Paste: return rgb(0x5B8DEF);
        case ActionCategory::Open: return rgb(0x2FB5A5);
        case ActionCategory::Save: return rgb(0xE0A43B);
        case ActionCategory::Convert: return rgb(0x9B7BEA);
        case ActionCategory::Extract: return rgb(0x3FB984);
        case ActionCategory::Network: return rgb(0x3BA7E0);
        case ActionCategory::Code: return rgb(0xE57C4B);
        case ActionCategory::Ai: return rgb(0xE0609A);
        case ActionCategory::Media: return rgb(0xC98BE0);
    }
    return current_palette.accent;
}

void draw_category_icon(ImDrawList* draw, ImVec2 center, float size, ActionCategory category, ImU32 color) {
    const float s = size * 0.5F;
    const float t = std::max(1.5F, size / 11.0F);
    const auto at = [&](float x, float y) { return ImVec2(center.x + x * s, center.y + y * s); };
    switch (category) {
        case ActionCategory::Paste:  // clipboard
            draw->AddRect(at(-0.6F, -0.7F), at(0.6F, 0.85F), color, 2.0F, 0, t);
            draw->AddRectFilled(at(-0.3F, -0.9F), at(0.3F, -0.55F), color, 1.5F);
            draw->AddLine(at(-0.3F, -0.1F), at(0.3F, -0.1F), color, t);
            draw->AddLine(at(-0.3F, 0.3F), at(0.15F, 0.3F), color, t);
            break;
        case ActionCategory::Open:  // arrow out of a box
            draw->AddRect(at(-0.75F, -0.45F), at(0.45F, 0.75F), color, 2.0F, 0, t);
            draw->AddLine(at(-0.1F, 0.1F), at(0.8F, -0.8F), color, t);
            draw->AddLine(at(0.25F, -0.8F), at(0.8F, -0.8F), color, t);
            draw->AddLine(at(0.8F, -0.8F), at(0.8F, -0.25F), color, t);
            break;
        case ActionCategory::Save:  // arrow into a tray
            draw->AddLine(at(0.0F, -0.85F), at(0.0F, 0.2F), color, t);
            draw->AddLine(at(-0.4F, -0.2F), at(0.0F, 0.2F), color, t);
            draw->AddLine(at(0.4F, -0.2F), at(0.0F, 0.2F), color, t);
            draw->AddLine(at(-0.8F, 0.35F), at(-0.8F, 0.8F), color, t);
            draw->AddLine(at(-0.8F, 0.8F), at(0.8F, 0.8F), color, t);
            draw->AddLine(at(0.8F, 0.8F), at(0.8F, 0.35F), color, t);
            break;
        case ActionCategory::Convert:  // swap arrows
            draw->AddLine(at(-0.8F, -0.35F), at(0.8F, -0.35F), color, t);
            draw->AddLine(at(0.4F, -0.75F), at(0.8F, -0.35F), color, t);
            draw->AddLine(at(0.8F, 0.35F), at(-0.8F, 0.35F), color, t);
            draw->AddLine(at(-0.4F, 0.75F), at(-0.8F, 0.35F), color, t);
            break;
        case ActionCategory::Extract:  // list
            for (const float y : {-0.55F, 0.0F, 0.55F}) {
                draw->AddCircleFilled(at(-0.65F, y), t * 0.9F, color);
                draw->AddLine(at(-0.3F, y), at(0.8F, y), color, t);
            }
            break;
        case ActionCategory::Network:  // globe
            draw->AddCircle(center, s * 0.85F, color, 24, t);
            draw->AddLine(at(-0.85F, 0.0F), at(0.85F, 0.0F), color, t);
            draw->AddBezierQuadratic(at(0.0F, -0.85F), at(-0.7F, 0.0F), at(0.0F, 0.85F), color, t);
            draw->AddBezierQuadratic(at(0.0F, -0.85F), at(0.7F, 0.0F), at(0.0F, 0.85F), color, t);
            break;
        case ActionCategory::Code:  // < / >
            draw->AddLine(at(-0.35F, -0.6F), at(-0.85F, 0.0F), color, t);
            draw->AddLine(at(-0.85F, 0.0F), at(-0.35F, 0.6F), color, t);
            draw->AddLine(at(0.35F, -0.6F), at(0.85F, 0.0F), color, t);
            draw->AddLine(at(0.85F, 0.0F), at(0.35F, 0.6F), color, t);
            draw->AddLine(at(0.15F, -0.8F), at(-0.15F, 0.8F), color, t);
            break;
        case ActionCategory::Ai: {  // sparkle
            const ImVec2 points[] = {at(0.0F, -0.9F), at(0.22F, -0.22F), at(0.9F, 0.0F), at(0.22F, 0.22F),
                                     at(0.0F, 0.9F), at(-0.22F, 0.22F), at(-0.9F, 0.0F), at(-0.22F, -0.22F)};
            draw->AddConvexPolyFilled(points, 8, color);
            break;
        }
        case ActionCategory::Media:  // picture
            draw->AddRect(at(-0.85F, -0.7F), at(0.85F, 0.7F), color, 2.0F, 0, t);
            draw->AddCircleFilled(at(0.4F, -0.25F), s * 0.16F, color);
            draw->AddTriangleFilled(at(-0.7F, 0.6F), at(-0.2F, -0.1F), at(0.3F, 0.6F), color);
            break;
    }
}

float pill(std::string_view text, ImVec4 color, bool filled) {
    const auto& fonts = current_fonts;
    ImGui::PushFont(fonts.bold, fonts.small);
    const ImVec2 text_size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const ImVec2 padding(8.0F, 2.0F);
    const ImVec2 size(text_size.x + padding.x * 2, text_size.y + padding.y * 2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                        ImGui::GetColorU32(filled ? color : with_alpha(color, 0.16F)), size.y * 0.5F);
    draw->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y),
                  ImGui::GetColorU32(filled ? current_palette.background : color), text.data(), text.data() + text.size());
    ImGui::Dummy(size);
    ImGui::PopFont();
    return size.x;
}

bool action_card(const char* id, const ActionCardModel& model) {
    const auto& p = current_palette;
    const auto& fonts = current_fonts;
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = kActionCardHeight * scale;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("card", ImVec2(width, height)) && model.enabled;
    const bool hovered = ImGui::IsItemHovered() && model.enabled;
    ImGui::PopID();

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + width, pos.y + height);
    const float rounding = 8.0F * scale;
    if (model.selected) {
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(p.accent_soft), rounding);
        draw->AddRectFilled(pos, ImVec2(pos.x + 3.0F * scale, end.y), ImGui::GetColorU32(p.accent), rounding,
                            ImDrawFlags_RoundCornersLeft);
    } else if (hovered) {
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(p.surface_hover), rounding);
    }
    const float alpha = model.enabled ? 1.0F : 0.45F;

    // Category badge.
    const float badge = 32.0F * scale;
    const ImVec2 badge_min(pos.x + 12.0F * scale, pos.y + (height - badge) * 0.5F);
    const auto tint = category_color(model.category);
    draw->AddRectFilled(badge_min, ImVec2(badge_min.x + badge, badge_min.y + badge),
                        ImGui::GetColorU32(with_alpha(tint, 0.16F * alpha)), 8.0F * scale);
    draw_category_icon(draw, ImVec2(badge_min.x + badge * 0.5F, badge_min.y + badge * 0.5F), badge * 0.5F,
                       model.category, ImGui::GetColorU32(with_alpha(tint, alpha)));

    // Right column: shortcut key and confidence.
    const float right = end.x - 12.0F * scale;
    const float bar_width = 56.0F * scale;
    float text_limit = right - bar_width - 16.0F * scale;
    if (model.shortcut > 0) {
        const char key[2] = {static_cast<char>('0' + model.shortcut), '\0'};
        ImGui::PushFont(fonts.bold, fonts.small);
        const float key_size = 20.0F * scale;
        const ImVec2 key_min(right - key_size, pos.y + 8.0F * scale);
        draw->AddRect(key_min, ImVec2(right, key_min.y + key_size), ImGui::GetColorU32(with_alpha(p.text_muted, 0.5F * alpha)),
                      5.0F * scale);
        const ImVec2 key_text = ImGui::CalcTextSize(key);
        draw->AddText(ImVec2(key_min.x + (key_size - key_text.x) * 0.5F, key_min.y + (key_size - key_text.y) * 0.5F),
                      ImGui::GetColorU32(with_alpha(p.text_muted, alpha)), key);
        ImGui::PopFont();
    }
    if (model.probability > 0.0) {
        const float fraction = static_cast<float>(std::clamp(model.probability, 0.0, 1.0));
        const float bar_y = end.y - 13.0F * scale;
        const ImVec2 bar_min(right - bar_width, bar_y);
        draw->AddRectFilled(bar_min, ImVec2(right, bar_y + 4.0F * scale), ImGui::GetColorU32(with_alpha(p.border, alpha)),
                            2.0F * scale);
        draw->AddRectFilled(bar_min, ImVec2(bar_min.x + bar_width * fraction, bar_y + 4.0F * scale),
                            ImGui::GetColorU32(with_alpha(model.selected ? p.accent : p.text_muted, alpha)), 2.0F * scale);
        if (hovered) {
            ImGui::SetTooltip("%.0f%%", model.probability * 100.0);
        }
    }

    // Label and detail, clipped to the space left of the right column.
    const float text_x = badge_min.x + badge + 12.0F * scale;
    const ImVec4 clip(text_x, pos.y, text_limit, end.y);
    ImGui::PushFont(fonts.bold, fonts.body);
    const float label_height = ImGui::GetFontSize();
    const bool has_detail = !model.detail.empty();
    const float label_y = has_detail ? pos.y + 8.0F * scale : pos.y + (height - label_height) * 0.5F;
    draw->AddText(nullptr, 0.0F, ImVec2(text_x, label_y), ImGui::GetColorU32(with_alpha(p.text, alpha)),
                  model.label.data(), model.label.data() + model.label.size(), 0.0F, &clip);
    ImGui::PopFont();
    if (has_detail) {
        ImGui::PushFont(fonts.regular, fonts.small);
        draw->AddText(nullptr, 0.0F, ImVec2(text_x, label_y + label_height + 3.0F * scale),
                      ImGui::GetColorU32(with_alpha(p.text_muted, alpha)), model.detail.data(),
                      model.detail.data() + model.detail.size(), 0.0F, &clip);
        ImGui::PopFont();
    }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}

#endif

}  // namespace pastit
