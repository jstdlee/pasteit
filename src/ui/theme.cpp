#include "ui/theme.hpp"

#include "ui/icons.hpp"
#include "ui/multi_viewport.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <cstdlib>
#include <cstring>

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
        // Only the Private Use Area: Font Awesome also maps a few icons onto
        // ASCII (+, #, *), which must never stand in for text.
        static const ImWchar icon_ranges[] = {icon::kFirstCodepoint, icon::kLastCodepoint, 0};
        ImFontConfig config;
        config.MergeMode = true;
        config.GlyphRanges = icon_ranges;
        // Slightly smaller than the text, one width for every icon.
        config.ExtraSizeScale = 0.875F;
        config.GlyphMinAdvanceX = fonts.body * 1.1F;
        config.GlyphOffset = ImVec2(0.0F, 1.0F);
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
        case ActionKind::NumberToBinary: return icon::kNumber;
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

// The look follows macOS: system blue accent, neutral controls with a
// hairline border, generous rounding, and settings laid out as rounded
// group boxes on a slightly darker window. Controls stay neutral on hover;
// the accent marks selection, focus and the default button only.
void apply_theme(UiTheme theme, float dpi_scale) {
    const bool light = theme == UiTheme::Light;
    // Tokens: soft grey page, white cards with a hairline, dim text that
    // still passes WCAG AA at small sizes. Dark surfaces get lighter as they
    // come forward: page, card, raised pill.
    current_palette = light ? UiPalette{
        .background = rgb(0xF4F4F6),
        .surface = rgb(0xFFFFFF),
        .surface_hover = rgb(0xE8E8ED),
        .border = rgb(0xE3E3E8),
        .text = rgb(0x212126),
        .text_muted = rgb(0x737379),
        .accent = rgb(0x007AFF),
        .accent_soft = rgb(0x007AFF, 0.13F),
        .track = rgb(0xF1F1F4),
        .success = rgb(0x1F9D4C),
        .warning = rgb(0xC77C02),
        .danger = rgb(0xFF3B30),
    } : UiPalette{
        .background = rgb(0x1C1C1F),
        .surface = rgb(0x252528),
        .surface_hover = rgb(0x323237),
        .border = rgb(0x303034),
        .text = rgb(0xE8E8EC),
        .text_muted = rgb(0x8E8E96),
        .accent = rgb(0x0A84FF),
        .accent_soft = rgb(0x0A84FF, 0.22F),
        .track = rgb(0x2E2E33),
        .success = rgb(0x32D74B),
        .warning = rgb(0xFF9F0A),
        .danger = rgb(0xFF453A),
    };
    const auto& p = current_palette;
    // Neutral washes: white over the dark window, black over the light one.
    const auto wash = [light](float alpha) { return light ? ImVec4(0, 0, 0, alpha * 0.75F) : ImVec4(1, 1, 1, alpha); };
    const auto tint = [&p](float alpha) { return with_alpha(p.accent, alpha); };

    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.WindowPadding = ImVec2(14.0F, 12.0F);
    style.FramePadding = ImVec2(10.0F, 6.0F);
    style.ItemSpacing = ImVec2(8.0F, 7.0F);
    style.ItemInnerSpacing = ImVec2(7.0F, 6.0F);
    style.CellPadding = ImVec2(8.0F, 5.0F);
    style.IndentSpacing = 20.0F;
    style.ScrollbarSize = 10.0F;
    style.GrabMinSize = 14.0F;
    style.WindowRounding = 12.0F;
    style.ChildRounding = 10.0F;
    style.FrameRounding = 7.0F;
    style.PopupRounding = 9.0F;
    style.ScrollbarRounding = 12.0F;
    style.GrabRounding = 7.0F;
    style.TabRounding = 7.0F;
    style.WindowTitleAlign = ImVec2(0.5F, 0.5F);
    style.DisabledAlpha = 0.45F;
    // Hairline borders on windows, group boxes and controls, like AppKit.
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;
    style.TabBarBorderSize = 0.0F;
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
    c[ImGuiCol_PopupBg] = light ? rgb(0xFFFFFF, 0.98F) : rgb(0x2A2A2E, 0.98F);
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    // Fields and dropdowns sit in the track fill; buttons are raised pills.
    c[ImGuiCol_FrameBg] = p.track;
    c[ImGuiCol_FrameBgHovered] = light ? rgb(0xEBEBEF) : rgb(0x343439);
    c[ImGuiCol_FrameBgActive] = p.track;
    c[ImGuiCol_TitleBg] = p.background;
    c[ImGuiCol_TitleBgActive] = p.background;
    c[ImGuiCol_TitleBgCollapsed] = p.background;
    c[ImGuiCol_MenuBarBg] = p.surface;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = wash(0.22F);
    c[ImGuiCol_ScrollbarGrabHovered] = wash(0.32F);
    c[ImGuiCol_ScrollbarGrabActive] = wash(0.42F);
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = mix(p.accent, p.text, 0.15F);
    c[ImGuiCol_Button] = light ? rgb(0xFFFFFF) : rgb(0x3A3A40);
    c[ImGuiCol_ButtonHovered] = light ? rgb(0xF5F5F7) : rgb(0x434349);
    c[ImGuiCol_ButtonActive] = light ? rgb(0xE6E6EB) : rgb(0x4C4C52);
    c[ImGuiCol_Header] = tint(0.20F);
    c[ImGuiCol_HeaderHovered] = tint(0.12F);
    c[ImGuiCol_HeaderActive] = tint(0.28F);
    c[ImGuiCol_Separator] = light ? rgb(0xECECF0) : rgb(0x2C2C30);
    c[ImGuiCol_SeparatorHovered] = tint(0.60F);
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = tint(0.45F);
    c[ImGuiCol_ResizeGripActive] = p.accent;
    c[ImGuiCol_InputTextCursor] = p.accent;
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = wash(0.06F);
    c[ImGuiCol_TabSelected] = light ? rgb(0xFFFFFF) : wash(0.12F);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = wash(0.06F);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotLinesHovered] = mix(p.accent, p.text, 0.2F);
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = mix(p.accent, p.text, 0.2F);
    c[ImGuiCol_TableHeaderBg] = wash(0.04F);
    c[ImGuiCol_TableBorderStrong] = wash(0.10F);
    c[ImGuiCol_TableBorderLight] = light ? rgb(0xECECF0) : rgb(0x2C2C30);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = wash(0.03F);
    c[ImGuiCol_TextSelectedBg] = tint(0.30F);
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

bool icon_button(const char* id, const char* glyph, const std::string& tooltip, bool active) {
    const auto& p = current_palette;
    const bool has_glyph = current_fonts.icons && glyph != nullptr;
    const auto label = std::string{has_glyph ? glyph : tooltip.c_str()} + "##" + id;
    // Toolbar style: a dim glyph that darkens on hover over a soft square,
    // no fill or border otherwise; an active toggle sits in an accent wash
    // with an accent glyph.
    ImGui::PushStyleColor(ImGuiCol_Button, active ? p.accent_soft : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? with_alpha(p.accent, p.accent_soft.w + 0.08F) : p.surface_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? with_alpha(p.accent, p.accent_soft.w + 0.14F) : p.border);
    // The glyph is drawn after the button so its colour can follow the hover state.
    ImGui::PushStyleColor(ImGuiCol_Text, has_glyph ? ImVec4(0, 0, 0, 0) : p.text);
    const float side = ImGui::GetFrameHeight();
    const bool clicked = ImGui::Button(label.c_str(), has_glyph ? ImVec2(side, side) : ImVec2(0.0F, side));
    ImGui::PopStyleColor(5);
    if (has_glyph) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 extent = current_fonts.regular->CalcTextSizeA(ImGui::GetFontSize(), 1000.0F, 0.0F, glyph);
        const ImVec4 tint = active ? p.accent : ImGui::IsItemHovered() || ImGui::IsItemActive() ? p.text : p.text_muted;
        const float alpha = ImGui::GetStyle().Alpha;  // honours BeginDisabled
        ImGui::GetWindowDrawList()->AddText(current_fonts.regular, ImGui::GetFontSize(),
            ImVec2(std::round(min.x + (side - extent.x) * 0.5F), std::round(min.y + (side - extent.y) * 0.5F)),
            ImGui::GetColorU32(with_alpha(tint, tint.w * alpha)), glyph);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip.c_str());
    return clicked;
}

float icon_buttons_width(int count) {
    if (count <= 0) return 0.0F;
    return ImGui::GetFrameHeight() * static_cast<float>(count) + ImGui::GetStyle().ItemSpacing.x * static_cast<float>(count - 1);
}

float pill(std::string_view text, ImVec4 color, bool filled) {
    const auto& fonts = current_fonts;
    ImGui::PushFont(fonts.bold, fonts.small);
    const ImVec2 text_size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const ImVec2 padding(9.0F, 3.0F);
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

ActionCardEvent action_card(const char* id, const ActionCardModel& model) {
    const auto& p = current_palette;
    const auto& fonts = current_fonts;
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = kActionCardHeight * scale;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    // The preview / compare icons sit on top of the card and take their own clicks.
    if (model.previewable) ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton("card", ImVec2(width, height)) && model.enabled;
    const ImVec2 after_card = ImGui::GetCursorScreenPos();
    const bool pointer_inside = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + width, pos.y + height)) &&
                                ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool hovered = (ImGui::IsItemHovered() || (model.previewable && pointer_inside)) && model.enabled;

    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + width, pos.y + height);
    const float rounding = 10.0F * scale;
    if (model.selected) {
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(p.accent_soft), rounding);
        draw->AddRect(pos, end, ImGui::GetColorU32(with_alpha(p.accent, 0.45F)), rounding, 0, 1.0F);
    } else if (hovered) {
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(p.surface_hover), rounding);
    }
    const float alpha = model.enabled ? 1.0F : 0.45F;

    // Category badge.
    const float badge = 34.0F * scale;
    const ImVec2 badge_min(pos.x + 12.0F * scale, pos.y + (height - badge) * 0.5F);
    const auto tint = category_color(model.category);
    draw->AddRectFilled(badge_min, ImVec2(badge_min.x + badge, badge_min.y + badge),
                        ImGui::GetColorU32(with_alpha(tint, 0.16F * alpha)), 9.0F * scale);
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
    // Preview | compare, left of the shortcut column; shown on hover, on the
    // selected card, and while this card's result is in the input.
    ActionCardEvent event = clicked ? ActionCardEvent::Activate : ActionCardEvent::None;
    if (model.previewable && model.enabled) {
        const float side = ImGui::GetFrameHeight();
        const float gap = 2.0F * scale;
        const float icons_left = right - bar_width - 12.0F * scale - side * 2.0F - gap;
        text_limit = icons_left - 8.0F * scale;
        if (hovered || model.selected || model.previewing) {
            ImGui::SetCursorScreenPos(ImVec2(icons_left, pos.y + (height - side) * 0.5F));
            const bool restore = model.previewing;
            if (icon_button("preview", restore ? icon::kUndo : icon::kEye,
                            std::string{restore ? model.restore_tooltip : model.preview_tooltip}, restore)) {
                event = ActionCardEvent::Preview;
            }
            ImGui::SameLine(0.0F, gap);
            if (icon_button("compare", icon::kCompare, std::string{model.compare_tooltip})) event = ActionCardEvent::Compare;
            ImGui::SetCursorScreenPos(after_card);
        }
    }

    if (model.shortcut > 0) {
        const char key[2] = {static_cast<char>('0' + model.shortcut), '\0'};
        ImGui::PushFont(fonts.bold, fonts.small);
        const float key_size = 22.0F * scale;
        const ImVec2 key_min(right - key_size, pos.y + 8.0F * scale);
        draw->AddRectFilled(key_min, ImVec2(right, key_min.y + key_size),
                            ImGui::GetColorU32(with_alpha(p.text_muted, 0.10F * alpha)), 6.0F * scale);
        draw->AddRect(key_min, ImVec2(right, key_min.y + key_size), ImGui::GetColorU32(with_alpha(p.text_muted, 0.35F * alpha)),
                      6.0F * scale);
        const ImVec2 key_text = ImGui::CalcTextSize(key);
        draw->AddText(ImVec2(key_min.x + (key_size - key_text.x) * 0.5F, key_min.y + (key_size - key_text.y) * 0.5F),
                      ImGui::GetColorU32(with_alpha(p.text_muted, alpha)), key);
        ImGui::PopFont();
    }
    if (model.probability > 0.0) {
        const float fraction = static_cast<float>(std::clamp(model.probability, 0.0, 1.0));
        const float bar_y = end.y - 13.0F * scale;
        draw_meter(draw, ImVec2(right - bar_width, bar_y), ImVec2(right, bar_y + 4.0F * scale), fraction,
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
    const float label_y = has_detail ? pos.y + 9.0F * scale : pos.y + (height - label_height) * 0.5F;
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
    ImGui::PopID();
    return event;
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

float button_width(const std::string& label) {
    const float text = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0F;
    return std::max(text, 84.0F * ImGui::GetStyle().FontScaleDpi);
}

bool primary_button(const std::string& label) {
    const auto& p = current_palette;
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(p.accent, ImVec4(1, 1, 1, 1), 0.12F));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(p.accent, ImVec4(0, 0, 0, 1), 0.15F));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool clicked = ImGui::Button(label.c_str(), ImVec2(button_width(label), 0.0F));
    ImGui::PopStyleColor(5);
    return clicked;
}

int footer_buttons(std::initializer_list<FooterButton> buttons, std::string_view status) {
    const auto& style = ImGui::GetStyle();
    float width = 0.0F;
    for (const auto& button : buttons) {
        width += button_width(button.label) + style.ItemSpacing.x;
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
        // A disabled default button loses its accent rather than showing a faded one.
        const bool pressed = button.primary && button.enabled
                                 ? primary_button(button.label)
                                 : ImGui::Button(button.label.c_str(), ImVec2(button_width(button.label), 0.0F));
        if (!button.enabled) ImGui::EndDisabled();
        if (pressed) clicked = index;
        ++index;
    }
    return clicked;
}

void draw_meter(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction, ImVec4 fill, float alpha) {
    const bool light = current_palette.background.x > 0.5F;
    const float rounding = (max.y - min.y) * 0.5F;
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
    const float height = ImGui::GetFrameHeight() + 4.0F * scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("nav", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImGui::PopID();
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + width, pos.y + height);
    const float rounding = 7.0F * scale;
    const bool light = p.background.x > 0.5F;
    if (selected) {
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(held ? mix(p.accent, ImVec4(0, 0, 0, 1), 0.15F) : p.accent), rounding);
    } else if (hovered) {
        const float alpha = held ? 0.12F : 0.07F;
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(light ? ImVec4(0, 0, 0, alpha) : ImVec4(1, 1, 1, alpha)), rounding);
    }
    const ImVec2 text_size = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    const ImVec2 text_pos(pos.x + 10.0F * scale, pos.y + (height - text_size.y) * 0.5F);
    const ImVec4 clip(pos.x, pos.y, end.x - 4.0F * scale, end.y);
    draw->AddText(nullptr, 0.0F, text_pos, ImGui::GetColorU32(selected ? ImVec4(1, 1, 1, 1) : p.text), label.data(),
                  label.data() + label.size(), 0.0F, &clip);
    return clicked;
}

void separator_heading(std::string_view title) {
    // Section title above a card: small grey capitals (ASCII only; other
    // scripts stay as written), a little air above, close to its card.
    const float scale = ImGui::GetStyle().FontScaleDpi;
    std::string caps{title};
    for (auto& ch : caps) {
        if (static_cast<unsigned char>(ch) < 0x80) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    ImGui::Dummy(ImVec2(0.0F, 8.0F * scale));
    ImGui::PushFont(current_fonts.regular, current_fonts.small);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0F * scale);
    ImGui::TextColored(current_palette.text_muted, "%s", caps.c_str());
    ImGui::PopFont();
}

void section_heading(const char* glyph, std::string_view title, std::string_view help) {
    ImGui::PushFont(current_fonts.regular, current_fonts.heading * 1.15F);
    if (current_fonts.icons && glyph != nullptr) {
        ImGui::TextColored(current_palette.text_muted, "%s", glyph);
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x * 1.5F);
    }
    ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::PopFont();
    if (!help.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(current_palette.text_muted, "%.*s", static_cast<int>(help.size()), help.data());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
}

static float segmented_width(std::span<const std::string> labels, float pad_x, float inset) {
    float total = inset * 2.0F;
    for (const auto& label : labels) total += ImGui::CalcTextSize(label.c_str()).x + pad_x * 2.0F;
    return total;
}

bool segmented_control(const char* id, std::span<const std::string> labels, int& selected, bool compact) {
    const auto& p = current_palette;
    const bool light = p.background.x > 0.5F;
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float inset = 2.0F * scale;
    const float pad_x = (compact ? 11.0F : 14.0F) * scale;
    const float height = compact ? ImGui::GetFrameHeight() : ImGui::GetFrameHeight() + inset * 2.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float total = segmented_width(labels, pad_x, inset);

    auto* draw = ImGui::GetWindowDrawList();
    const float rounding = 8.0F * scale;
    draw->AddRectFilled(origin, ImVec2(origin.x + total, origin.y + height), ImGui::GetColorU32(p.track), rounding);
    bool changed = false;
    float x = origin.x + inset;
    ImGui::PushID(id);
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        const auto& label = labels[static_cast<std::size_t>(index)];
        const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        const float width = text_size.x + pad_x * 2.0F;
        const ImVec2 min(x, origin.y + inset);
        const ImVec2 max(x + width, origin.y + height - inset);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(index);
        if (ImGui::InvisibleButton("segment", ImVec2(width, max.y - min.y)) && selected != index) {
            selected = index;
            changed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool active = selected == index;
        const bool held = ImGui::IsItemActive();
        if (active) {
            // Raised pill: 1 px soft shadow, the face, a hairline.
            draw->AddRectFilled(ImVec2(min.x, min.y + 1.0F * scale), ImVec2(max.x, max.y + 1.0F * scale),
                                ImGui::GetColorU32(ImVec4(0, 0, 0, light ? 0.06F : 0.25F)), rounding - inset);
            draw->AddRectFilled(min, max, ImGui::GetColorU32(light ? rgb(0xFFFFFF) : rgb(0x3A3A40)), rounding - inset);
            draw->AddRect(min, max, ImGui::GetColorU32(light ? p.border : rgb(0x45454B)), rounding - inset);
        } else if (hovered || held) {
            draw->AddRectFilled(min, max, ImGui::GetColorU32(light ? ImVec4(0, 0, 0, held ? 0.08F : 0.04F)
                                                                    : ImVec4(1, 1, 1, held ? 0.09F : 0.05F)),
                                rounding - inset);
        }
        draw->AddText(ImVec2(min.x + pad_x, min.y + (max.y - min.y - text_size.y) * 0.5F),
                      ImGui::GetColorU32(active || hovered ? p.text : p.text_muted), label.c_str());
            x += width;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(total, height));
    return changed;
}

bool choice_control(const char* id, std::span<const std::string> labels, int& selected) {
    const float scale = ImGui::GetStyle().FontScaleDpi;
    if (segmented_width(labels, 11.0F * scale, 2.0F * scale) <= ImGui::GetContentRegionAvail().x) {
        return segmented_control(id, labels, selected, true);
    }
    bool changed = false;
    const auto& preview = labels[static_cast<std::size_t>(std::clamp(selected, 0, static_cast<int>(labels.size()) - 1))];
    if (begin_combo(id, preview.c_str())) {
        for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
            if (ImGui::Selectable(labels[static_cast<std::size_t>(index)].c_str(), selected == index) && selected != index) {
                selected = index;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool begin_group(const char* id) {
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, current_palette.surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F * scale, 8.0F * scale));
    const bool visible = ImGui::BeginChild(id, ImVec2(0.0F, 0.0F),
                                           ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                               ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return visible;
}

void end_group() {
    ImGui::EndChild();
}

bool toggle_switch(const char* label, bool* value) {
    const auto& p = current_palette;
    const bool light = p.background.x > 0.5F;
    const float height = std::round(ImGui::GetFrameHeight() * 0.8F);
    const float width = std::round(height * 1.75F);
    // Text before "##" is shown to the right and clicks like the switch.
    const char* hidden = std::strstr(label, "##");
    const char* label_end = hidden != nullptr ? hidden : label + std::strlen(label);
    const bool has_text = label_end != label;
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float text_width = has_text ? ImGui::CalcTextSize(label, label_end).x : 0.0F;
    // Centre the switch on the row's frame height so it lines up with labels.
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 pos(cursor.x, cursor.y + (ImGui::GetFrameHeight() - height) * 0.5F);
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(width + (has_text ? gap + text_width : 0.0F), ImGui::GetFrameHeight()));
    if (clicked) *value = !*value;
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    const float radius = height * 0.5F;
    const ImVec4 off = light ? ImVec4(0, 0, 0, hovered ? 0.16F : 0.12F) : ImVec4(1, 1, 1, hovered ? 0.20F : 0.16F);
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(*value ? p.accent : off), radius);
    const float knob_x = *value ? pos.x + width - radius : pos.x + radius;
    const ImVec2 knob(knob_x, pos.y + radius);
    const float alpha = ImGui::GetStyle().Alpha;  // dimmed inside BeginDisabled
    draw->AddCircleFilled(ImVec2(knob.x, knob.y + 0.5F), radius - 1.5F, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.16F)));
    draw->AddCircleFilled(knob, radius - 2.0F, ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)));
    if (has_text) {
        draw->AddText(ImVec2(cursor.x + width + gap, cursor.y + ImGui::GetStyle().FramePadding.y),
                      ImGui::GetColorU32(ImGuiCol_Text), label, label_end);
    }
    return clicked;
}

bool begin_combo(const char* id, const char* preview, ImGuiComboFlags flags) {
    if (!current_fonts.icons || (flags & ImGuiComboFlags_NoPreview) != 0) return ImGui::BeginCombo(id, preview, flags);
    // Measure the frame first: once open, BeginCombo has moved on to the popup window.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const float font_size = ImGui::GetFontSize();
    const float pad = ImGui::GetStyle().FramePadding.x;
    // The preview is drawn here too, clipped short of the chevron.
    const bool open = ImGui::BeginCombo(id, nullptr, flags | ImGuiComboFlags_NoArrowButton);
    const ImVec2 extent = current_fonts.regular->CalcTextSizeA(font_size, 1000.0F, 0.0F, icon::kChevronDown);
    const float chevron_x = pos.x + width - pad - extent.x;
    draw->AddText(current_fonts.regular, font_size, ImVec2(chevron_x, pos.y + (height - extent.y) * 0.5F),
                  ImGui::GetColorU32(current_palette.text_muted), icon::kChevronDown);
    if (preview != nullptr) {
        const ImVec4 clip(pos.x, pos.y, chevron_x - pad * 0.5F, pos.y + height);
        draw->AddText(nullptr, 0.0F, ImVec2(pos.x + pad, pos.y + (height - font_size) * 0.5F),
                      ImGui::GetColorU32(ImGuiCol_Text), preview, nullptr, 0.0F, &clip);
    }
    return open;
}

bool begin_form(const char* id, float label_width) {
    begin_group(id);
    // Taller rows with hairlines between them, like a macOS settings group.
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0F * ImGui::GetStyle().FontScaleDpi, 7.0F * ImGui::GetStyle().FontScaleDpi));
    const bool open = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH);
    ImGui::PopStyleVar();
    if (!open) {
        end_group();
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width * ImGui::GetStyle().FontScaleDpi);
    ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void form_row(std::string_view label, std::string_view help) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (help.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
    } else {
        // Title, then one grey line saying what it does; cut with an
        // ellipsis, the whole sentence on hover.
        ImGui::TextUnformatted(label.data(), label.data() + label.size());
        ImGui::PushFont(current_fonts.regular, current_fonts.small);
        const float width = ImGui::GetContentRegionAvail().x;
        std::string line{help};
        bool cut = false;
        while (line.size() > 8 && ImGui::CalcTextSize(line.c_str()).x > width) {
            std::size_t end = line.size() - 1;
            while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xC0U) == 0x80U) --end;  // UTF-8 boundary
            line.resize(end);
            cut = true;
        }
        if (cut) line += "\xE2\x80\xA6";
        ImGui::TextColored(current_palette.text_muted, "%s", line.c_str());
        ImGui::PopFont();
        if (cut && ImGui::IsItemHovered()) ImGui::SetTooltip("%.*s", static_cast<int>(help.size()), help.data());
    }
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void end_form() {
    ImGui::EndTable();
    end_group();
}

#endif

}  // namespace pasteit
