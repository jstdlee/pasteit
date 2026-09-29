#include "ui/theme.hpp"

#include "ui/icons.hpp"
#include "ui/multi_viewport.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdlib>

namespace pasteit {

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

#if defined(PASTEIT_HAS_DESKTOP_DEPS)

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

UiFonts load_ui_fonts(const std::vector<std::filesystem::path>& candidates, const std::filesystem::path& icon_font) {
    UiFonts fonts;
    auto& atlas = *ImGui::GetIO().Fonts;
    std::error_code icon_error;
    const bool has_icons = !icon_font.empty() && std::filesystem::is_regular_file(icon_font, icon_error);
    // A merged font attaches to the most recently added base font, so each
    // base font is followed by its own icon merge.
    const auto merge_icons = [&] {
        if (!has_icons) return false;
        ImFontConfig config;
        config.MergeMode = true;
        config.GlyphOffset = ImVec2(0.0F, 2.0F);
        return atlas.AddFontFromFileTTF(path_to_utf8_string(icon_font).c_str(), fonts.body, &config) != nullptr;
    };
    for (const auto& path : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) continue;
        ImFontConfig config;
        config.OversampleH = 2;
        fonts.regular = atlas.AddFontFromFileTTF(path_to_utf8_string(path).c_str(), fonts.body, &config);
        if (fonts.regular == nullptr) continue;
        fonts.icons = merge_icons();
        if (const auto bold = bold_variant(path); !bold.empty()) {
            fonts.bold = atlas.AddFontFromFileTTF(path_to_utf8_string(bold).c_str(), fonts.body, &config);
            if (fonts.bold != nullptr) fonts.icons = merge_icons() && fonts.icons;
        }
        break;
    }
    if (fonts.regular == nullptr) {
        fonts.regular = atlas.AddFontDefault();
        fonts.icons = merge_icons();
    }
    if (fonts.bold == nullptr) fonts.bold = fonts.regular;
    ImGui::GetIO().FontDefault = fonts.regular;
    current_fonts = fonts;
    return fonts;
}

const char* category_icon(ActionCategory category) {
    switch (category) {
        case ActionCategory::Paste: return icon::kPaste;
        case ActionCategory::Open: return icon::kOpen;
        case ActionCategory::Save: return icon::kSave;
        case ActionCategory::Convert: return icon::kConvert;
        case ActionCategory::Extract: return icon::kExtract;
        case ActionCategory::Network: return icon::kNetwork;
        case ActionCategory::Code: return icon::kCode;
        case ActionCategory::Ai: return icon::kAi;
        case ActionCategory::Media: return icon::kMedia;
    }
    return icon::kInfo;
}

const char* action_icon(ActionKind kind) {
    switch (kind) {
        case ActionKind::ViewTable: return icon::kTable;
        case ActionKind::PreviewMarkdown: return icon::kBook;
        case ActionKind::Graph:
        case ActionKind::NumberStatistics: return icon::kChart;
        case ActionKind::RunPipeline: return icon::kPipeline;
        case ActionKind::AnonymizeText: return icon::kEyeOff;
        case ActionKind::RestorePlaceholders: return icon::kEye;
        case ActionKind::SummarizePage: return icon::kScanText;
        case ActionKind::OpenTerminalAtPath: return icon::kTerminal;
        case ActionKind::CopyPath:
        case ActionKind::CopyFileName:
        case ActionKind::CopyParentPath:
        case ActionKind::CopyTemporaryImagePath: return icon::kCopy;
        case ActionKind::RevealPath:
        case ActionKind::CopyPathToDirectory:
        case ActionKind::MovePath: return icon::kFolderOpen;
        case ActionKind::OpenUrl:
        case ActionKind::CleanUrl:
        case ActionKind::CopyMarkdownLink: return icon::kLink;
        case ActionKind::PrettyJson:
        case ActionKind::MinifyJson:
        case ActionKind::JsonToYaml:
        case ActionKind::JsonToCsv:
        case ActionKind::CopyJsonPaths: return icon::kBraces;
        case ActionKind::HashSha256:
        case ActionKind::HashSha512:
        case ActionKind::GenerateUuid: return icon::kHash;
        case ActionKind::CopyColorHex:
        case ActionKind::CopyColorRgb:
        case ActionKind::CopyColorHsl: return icon::kPalette;
        case ActionKind::ConvertTimezone:
        case ActionKind::ToUnixTimestamp:
        case ActionKind::CopyNormalizedDateTime: return icon::kClock;
        case ActionKind::DecodeJwt: return icon::kLock;
        case ActionKind::ExtractContactInfo:
        case ActionKind::CopyContactVCard:
        case ActionKind::SaveContactVCard: return icon::kUser;
        case ActionKind::TextStatistics:
        case ActionKind::SortLines:
        case ActionKind::DedupeLines: return icon::kListOrdered;
        case ActionKind::SaveCodeFile: return icon::kFileCode;
        case ActionKind::TextToHex:
        case ActionKind::HexToText:
        case ActionKind::TextToBinary:
        case ActionKind::BinaryToText: return icon::kBraces;
        case ActionKind::NumberBases:
        case ActionKind::NumberToHex:
        case ActionKind::NumberToDecimal:
        case ActionKind::NumberToBinary: return icon::kHash;
        case ActionKind::SubnetDetails:
        case ActionKind::SplitSubnet:
        case ActionKind::MaskDetails:
        case ActionKind::MaskToPrefix:
        case ActionKind::MaskToNetmask: return icon::kLayers;
        default: return category_icon(action_category(kind));
    }
}

std::string with_icon(const char* glyph, std::string_view text) {
    if (!current_fonts.icons || glyph == nullptr) return std::string{text};
    return std::string{glyph} + "  " + std::string{text};
}

const UiFonts& ui_fonts() {
    return current_fonts;
}

const UiPalette& palette() {
    return current_palette;
}

// The look follows GPU HUD: a dark glass panel with a thin accent outline,
// controls drawn as translucent white (dark) or black (light) washes that
// take the accent colour on hover, and a green accent for state. Light mode
// keeps the same shapes and alphas on a pale panel.
void apply_theme(UiTheme theme, float dpi_scale) {
    const bool light = theme == UiTheme::Light;
    current_palette = light ? UiPalette{
        .background = rgb(0xF3F5F4),
        .surface = rgb(0xFFFFFF),
        .surface_hover = rgb(0xE9EDEB),
        .border = rgb(0xD9DFDC),
        .text = rgb(0x1A1F1C),
        .text_muted = rgb(0x5E6A64),
        .accent = rgb(0x1E9E55),
        .accent_soft = rgb(0x1E9E55, 0.14F),
        .success = rgb(0x1E9E55),
        .warning = rgb(0xC98A12),
        .danger = rgb(0xD64545),
    } : UiPalette{
        .background = rgb(0x0F1217),
        .surface = rgb(0x171A1F),
        .surface_hover = rgb(0x22262B),
        .border = rgb(0x2A2E34),
        .text = rgb(0xECEEF1),
        .text_muted = rgb(0xA4ABB5),
        .accent = rgb(0x4CC778),
        .accent_soft = rgb(0x4CC778, 0.18F),
        .success = rgb(0x4CC778),
        .warning = rgb(0xF2AD38),
        .danger = rgb(0xED4D47),
    };
    const auto& p = current_palette;
    // Control washes: white over the dark panel, black over the light one.
    const auto wash = [light](float alpha) { return light ? ImVec4(0, 0, 0, alpha * 0.75F) : ImVec4(1, 1, 1, alpha); };
    const auto tint = [&p](float alpha) { return with_alpha(p.accent, alpha); };

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.WindowPadding = ImVec2(12.0F, 10.0F);
    style.FramePadding = ImVec2(9.0F, 5.0F);
    style.ItemSpacing = ImVec2(8.0F, 6.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 5.0F);
    style.CellPadding = ImVec2(8.0F, 4.0F);
    style.ScrollbarSize = 9.0F;
    style.GrabMinSize = 10.0F;
    style.WindowRounding = 10.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 5.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 5.0F;
    style.GrabRounding = 4.0F;
    style.TabRounding = 5.0F;
    // Only windows get an outline (the accent hairline); controls are flat.
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
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
    c[ImGuiCol_PopupBg] = light ? rgb(0xFFFFFF, 0.98F) : ImVec4(0.08F, 0.09F, 0.11F, 0.97F);
    c[ImGuiCol_Border] = tint(0.35F);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = wash(0.08F);
    c[ImGuiCol_FrameBgHovered] = wash(0.14F);
    c[ImGuiCol_FrameBgActive] = tint(0.35F);
    c[ImGuiCol_TitleBg] = p.background;
    c[ImGuiCol_TitleBgActive] = p.surface;
    c[ImGuiCol_TitleBgCollapsed] = p.background;
    c[ImGuiCol_MenuBarBg] = p.surface;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = wash(0.14F);
    c[ImGuiCol_ScrollbarGrabHovered] = tint(0.45F);
    c[ImGuiCol_ScrollbarGrabActive] = tint(0.70F);
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accent;
    c[ImGuiCol_Button] = wash(0.08F);
    c[ImGuiCol_ButtonHovered] = tint(0.45F);
    c[ImGuiCol_ButtonActive] = tint(0.70F);
    c[ImGuiCol_Header] = tint(0.30F);
    c[ImGuiCol_HeaderHovered] = tint(0.45F);
    c[ImGuiCol_HeaderActive] = tint(0.55F);
    c[ImGuiCol_Separator] = wash(0.10F);
    c[ImGuiCol_SeparatorHovered] = tint(0.60F);
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = tint(0.25F);
    c[ImGuiCol_ResizeGripHovered] = tint(0.55F);
    c[ImGuiCol_ResizeGripActive] = p.accent;
    c[ImGuiCol_InputTextCursor] = p.accent;
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = wash(0.08F);
    c[ImGuiCol_TabSelected] = wash(0.08F);
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = wash(0.06F);
    c[ImGuiCol_TabDimmedSelectedOverline] = tint(0.5F);
    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotLinesHovered] = rgb(0x739EFA);
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = mix(p.accent, p.text, 0.2F);
    c[ImGuiCol_TableHeaderBg] = wash(0.06F);
    c[ImGuiCol_TableBorderStrong] = wash(0.10F);
    c[ImGuiCol_TableBorderLight] = wash(0.06F);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = wash(0.05F);
    c[ImGuiCol_TextSelectedBg] = tint(0.35F);
    c[ImGuiCol_DragDropTarget] = p.accent;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, light ? 0.2F : 0.5F);
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
    if (current_fonts.icons) {
        const char* glyph = category_icon(category);
        ImFont* font = current_fonts.regular;
        const float font_size = size * 1.15F;
        const ImVec2 extent = font->CalcTextSizeA(font_size, 1000.0F, 0.0F, glyph);
        draw->AddText(font, font_size, ImVec2(center.x - extent.x * 0.5F, center.y - extent.y * 0.5F - 1.0F), color, glyph);
        return;
    }
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

bool icon_cell(const char* glyph, ImVec4 color, float size) {
    if (!current_fonts.icons || glyph == nullptr) return false;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float font_size = size * 0.95F;
    const ImVec2 extent = current_fonts.regular->CalcTextSizeA(font_size, 1000.0F, 0.0F, glyph);
    ImGui::GetWindowDrawList()->AddText(current_fonts.regular, font_size,
                                        ImVec2(origin.x + (size - extent.x) * 0.5F, origin.y + (size - extent.y) * 0.5F),
                                        ImGui::GetColorU32(color), glyph);
    ImGui::Dummy(ImVec2(size, size));
    return true;
}

bool icon_button(const char* id, const char* glyph, const std::string& tooltip) {
    const auto label = std::string{current_fonts.icons && glyph != nullptr ? glyph : tooltip.c_str()} + "##" + id;
    const bool clicked = ImGui::SmallButton(label.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip.c_str());
    return clicked;
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
    const ImVec2 badge_center(badge_min.x + badge * 0.5F, badge_min.y + badge * 0.5F);
    if (model.glyph != nullptr && current_fonts.icons) {
        const float font_size = badge * 0.5F * 1.15F;
        const ImVec2 extent = current_fonts.regular->CalcTextSizeA(font_size, 1000.0F, 0.0F, model.glyph);
        draw->AddText(current_fonts.regular, font_size,
                      ImVec2(badge_center.x - extent.x * 0.5F, badge_center.y - extent.y * 0.5F - 1.0F),
                      ImGui::GetColorU32(with_alpha(tint, alpha)), model.glyph);
    } else {
        draw_category_icon(draw, badge_center, badge * 0.5F, model.category, ImGui::GetColorU32(with_alpha(tint, alpha)));
    }

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
        const float bar_y = end.y - 14.0F * scale;
        draw_meter(draw, ImVec2(right - bar_width, bar_y), ImVec2(right, bar_y + 6.0F * scale), fraction,
                   model.selected ? p.accent : p.text_muted, alpha);
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

bool begin_tool_window(const std::string& title, bool* open, ImVec2 size, bool* focus_pending, ImGuiWindowFlags extra_flags) {
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const bool focus = focus_pending != nullptr && *focus_pending;
    if (focus) {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    }
    const auto window_class = independent_window_class();
    ImGui::SetNextWindowClass(&window_class);
    ImGui::SetNextWindowSize(ImVec2(size.x * scale, size.y * scale), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0F * scale, 160.0F * scale), ImVec2(FLT_MAX, FLT_MAX));
    const bool visible = ImGui::Begin(title.c_str(), open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | extra_flags);
    if (visible && focus) {
        ImGui::SetWindowFocus();
        *focus_pending = false;
    }
    if (visible && open != nullptr && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        *open = false;
    }
    return visible;
}

float footer_height() {
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0F + 1.0F;
}

bool primary_button(const std::string& label) {
    const auto& p = current_palette;
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(p.accent, p.text, 0.15F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(p.accent, p.background, 0.2F));
    ImGui::PushStyleColor(ImGuiCol_Text, p.background);
    const bool clicked = ImGui::Button(label.c_str());
    ImGui::PopStyleColor(4);
    return clicked;
}

int footer_buttons(std::initializer_list<FooterButton> buttons, std::string_view status) {
    const auto& style = ImGui::GetStyle();
    float width = 0.0F;
    for (const auto& button : buttons) {
        width += ImGui::CalcTextSize(button.label.c_str(), nullptr, true).x + style.FramePadding.x * 2.0F + style.ItemSpacing.x;
    }
    const float bottom = ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight();
    if (ImGui::GetCursorPosY() < bottom - style.ItemSpacing.y) ImGui::SetCursorPosY(bottom - style.ItemSpacing.y);
    ImGui::Separator();
    if (!status.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(current_palette.text_muted, "%.*s", static_cast<int>(status.size()), status.data());
        ImGui::SameLine();
    }
    const float right = ImGui::GetWindowContentRegionMax().x - width + style.ItemSpacing.x;
    if (ImGui::GetCursorPosX() < right) ImGui::SetCursorPosX(right);
    int clicked = -1;
    int index = 0;
    for (const auto& button : buttons) {
        if (index != 0) ImGui::SameLine();
        if (!button.enabled) ImGui::BeginDisabled();
        const bool pressed = button.primary ? primary_button(button.label) : ImGui::Button(button.label.c_str());
        if (!button.enabled) ImGui::EndDisabled();
        if (pressed) clicked = index;
        ++index;
    }
    return clicked;
}

void draw_meter(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction, ImVec4 fill, float alpha) {
    const bool light = current_palette.background.x > 0.5F;
    const float rounding = (max.y - min.y) * 0.35F;
    draw->AddRectFilled(min, max, ImGui::GetColorU32(light ? ImVec4(0, 0, 0, 0.08F * alpha) : ImVec4(1, 1, 1, 0.10F * alpha)),
                        rounding);
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    if (fraction <= 0.0F) return;
    const float width = std::max((max.x - min.x) * fraction, rounding * 2.0F);
    draw->AddRectFilled(min, ImVec2(min.x + width, max.y), ImGui::GetColorU32(with_alpha(fill, fill.w * alpha)), rounding);
}

void meter(float fraction, std::string_view overlay, float height, bool load) {
    const auto& p = current_palette;
    const float width = ImGui::GetContentRegionAvail().x;
    if (height <= 0.0F) height = ImGui::GetFrameHeight() * 0.92F;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec4 fill = !load || fraction < 0.70F ? p.accent : fraction < 0.90F ? p.warning : p.danger;
    auto* draw = ImGui::GetWindowDrawList();
    draw_meter(draw, pos, ImVec2(pos.x + width, pos.y + height), fraction, fill);
    if (!overlay.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(overlay.data(), overlay.data() + overlay.size());
        const ImVec2 text(pos.x + (width - size.x) * 0.5F, pos.y + (height - size.y) * 0.5F);
        // Drop shadow keeps the label readable over the fill.
        draw->AddText(ImVec2(text.x + 1.0F, text.y + 1.0F), IM_COL32(0, 0, 0, 160), overlay.data(), overlay.data() + overlay.size());
        draw->AddText(text, ImGui::GetColorU32(ImGuiCol_Text), overlay.data(), overlay.data() + overlay.size());
    }
    ImGui::Dummy(ImVec2(width, height));
}

bool nav_item(const char* id, std::string_view label, bool selected) {
    const auto& p = current_palette;
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float height = ImGui::GetFrameHeight() + 6.0F * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("nav", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + width, pos.y + height);
    const float rounding = 6.0F * scale;
    const bool light = p.background.x > 0.5F;
    if (selected) {
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(with_alpha(p.accent, held ? 0.30F : 0.20F)), rounding);
    } else if (hovered) {
        const float alpha = held ? 0.12F : 0.07F;
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(light ? ImVec4(0, 0, 0, alpha) : ImVec4(1, 1, 1, alpha)), rounding);
    }
    const ImVec2 text_size = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const ImVec2 text_pos(pos.x + 10.0F * scale, pos.y + (height - text_size.y) * 0.5F);
    const ImVec4 clip(pos.x, pos.y, end.x - 4.0F * scale, end.y);
    draw->AddText(nullptr, 0.0F, text_pos, ImGui::GetColorU32(selected ? p.accent : p.text), label.data(),
                  label.data() + label.size(), 0.0F, &clip);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return clicked;
}

void separator_heading(std::string_view title) {
    ImGui::PushStyleColor(ImGuiCol_Text, current_palette.accent);
    ImGui::SeparatorText(std::string{title}.c_str());
    ImGui::PopStyleColor();
}

void section_heading(const char* glyph, std::string_view title, std::string_view help) {
    ImGui::PushFont(current_fonts.bold, current_fonts.heading);
    const auto text = with_icon(glyph, title);
    ImGui::TextColored(current_palette.accent, "%s", text.c_str());
    ImGui::PopFont();
    if (!help.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(current_palette.text_muted, "%.*s", static_cast<int>(help.size()), help.data());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
}

bool begin_form(const char* id, float label_width) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width * ImGui::GetStyle().FontScaleDpi);
    ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void form_row(std::string_view label, std::string_view help) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    if (!help.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%.*s", static_cast<int>(help.size()), help.data());
    }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void end_form() {
    ImGui::EndTable();
}

#endif

}  // namespace pasteit
