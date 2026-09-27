#include "ui/image_annotation_panel.hpp"
#include "ui/theme.hpp"
#include "ui/localization.hpp"

#include "ui/icons.hpp"
#include "ui/imgui_widgets.hpp"
#include "util/path_utf8.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cfloat>
#include <chrono>
#include <filesystem>
#include <sstream>

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
#include <imgui.h>
#endif

namespace pasteit {
namespace {

std::filesystem::path svg_export_path(std::filesystem::path output) {
    output.replace_extension(".svg");
    return output;
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
struct ToolInfo { AnnotationPanelTool tool; const char* label; ImGuiKey key; const char* hint; };
constexpr ToolInfo kTools[] = {
    {AnnotationPanelTool::Select, "Select", ImGuiKey_S, "S"},
    {AnnotationPanelTool::Pen, "Pen", ImGuiKey_F, "F"},
    {AnnotationPanelTool::Line, "Line", ImGuiKey_W, "W"},
    {AnnotationPanelTool::Arrow, "Arrow", ImGuiKey_A, "A"},
    {AnnotationPanelTool::Rectangle, "Rectangle", ImGuiKey_R, "R"},
    {AnnotationPanelTool::Circle, "Circle", ImGuiKey_C, "C"},
    {AnnotationPanelTool::Text, "Text", ImGuiKey_T, "T"},
};

constexpr AnnotationColor kSwatches[] = {
    {230, 40, 40, 255}, {255, 170, 0, 255}, {40, 170, 70, 255},
    {40, 110, 230, 255}, {20, 20, 20, 255}, {255, 255, 255, 255},
};

std::filesystem::path default_export_path(const ImageAnnotationPanelState& state) {
    auto directory = state.export_directory;
    if (directory.empty()) directory = state.document.original_image().parent_path();
    if (directory.empty()) directory = std::filesystem::temp_directory_path();
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char name[64]{};
    std::strftime(name, sizeof(name), "annotation-%Y%m%d-%H%M%S.svg", &local);
    return directory / name;
}

ImVec2 to_imvec2(const AnnotationPoint& point, const ImVec2& origin, float scale) {
    return ImVec2(origin.x + point.x * scale, origin.y + point.y * scale);
}

AnnotationPoint from_mouse(const ImVec2& mouse, const ImVec2& origin, float scale, int width, int height) {
    return AnnotationPoint{std::clamp((mouse.x - origin.x) / scale, 0.0F, static_cast<float>(width)),
                           std::clamp((mouse.y - origin.y) / scale, 0.0F, static_cast<float>(height))};
}

ImU32 color_u32(const AnnotationColor& color, float alpha = 1.0F) {
    return IM_COL32(color.r, color.g, color.b, static_cast<int>(color.a * alpha));
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream stream(text);
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    if (lines.empty()) lines.emplace_back();
    return lines;
}

// Screen-space box of a comment label, shared by drawing and hit-testing.
std::pair<ImVec2, ImVec2> comment_rect(const AnnotationComment& comment, const ImVec2& origin, float scale) {
    ImFont* font = ImGui::GetFont();
    const float size = comment.font_size * scale;
    const float padding = size * 0.45F;
    const float line_height = size * 1.3F;
    const auto lines = split_lines(comment.text);
    float width = 0.0F;
    for (const auto& line : lines) width = std::max(width, font->CalcTextSizeA(size, FLT_MAX, 0.0F, line.c_str()).x);
    const auto top_left = to_imvec2(comment.anchor, origin, scale);
    return {top_left, ImVec2(top_left.x + width + padding * 2.0F,
                             top_left.y + line_height * static_cast<float>(lines.size()) + padding * 2.0F - (line_height - size))};
}

void draw_comment(ImDrawList* draw_list, const AnnotationComment& comment, const ImVec2& origin, float scale) {
    ImFont* font = ImGui::GetFont();
    const float size = comment.font_size * scale;
    const float padding = size * 0.45F;
    const float line_height = size * 1.3F;
    const auto lines = split_lines(comment.text);
    const auto [top_left, bottom_right] = comment_rect(comment, origin, scale);
    draw_list->AddRectFilled(top_left, bottom_right, color_u32(comment.background, 0.92F), size * 0.25F);
    draw_list->AddRect(top_left, bottom_right, color_u32(comment.foreground), size * 0.25F, 0, 1.5F);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        draw_list->AddText(font, size, ImVec2(top_left.x + padding, top_left.y + padding + line_height * static_cast<float>(index)),
                           color_u32(comment.foreground), lines[index].c_str());
    }
}

void draw_stroke(ImDrawList* draw_list, const AnnotationStroke& stroke, const ImVec2& origin, float scale) {
    const auto color = color_u32(stroke.color);
    const auto width = std::max(1.0F, stroke.width * scale);
    if (stroke.points.size() >= 2 && stroke.tool != AnnotationTool::Pen) {
        const auto start = to_imvec2(stroke.points[0], origin, scale);
        const auto end = to_imvec2(stroke.points[1], origin, scale);
        switch (stroke.tool) {
            case AnnotationTool::Rectangle:
                draw_list->AddRect(ImVec2(std::min(start.x, end.x), std::min(start.y, end.y)),
                                   ImVec2(std::max(start.x, end.x), std::max(start.y, end.y)), color, 0.0F, 0, width);
                return;
            case AnnotationTool::Circle:
                draw_list->AddEllipse(ImVec2((start.x + end.x) * 0.5F, (start.y + end.y) * 0.5F),
                                      ImVec2(std::abs(end.x - start.x) * 0.5F, std::abs(end.y - start.y) * 0.5F),
                                      color, 0.0F, 0, width);
                return;
            case AnnotationTool::Line:
            case AnnotationTool::Arrow: {
                draw_list->AddLine(start, end, color, width);
                if (stroke.tool == AnnotationTool::Arrow) {
                    const auto angle = std::atan2(end.y - start.y, end.x - start.x);
                    const float size = std::max(12.0F, stroke.width * 4.0F) * scale;
                    draw_list->AddTriangleFilled(end,
                        ImVec2(end.x - std::cos(angle - 0.45F) * size, end.y - std::sin(angle - 0.45F) * size),
                        ImVec2(end.x - std::cos(angle + 0.45F) * size, end.y - std::sin(angle + 0.45F) * size), color);
                }
                return;
            }
            case AnnotationTool::Pen:
                break;
        }
    }
    for (std::size_t index = 1; index < stroke.points.size(); ++index) {
        draw_list->AddLine(to_imvec2(stroke.points[index - 1], origin, scale),
                           to_imvec2(stroke.points[index], origin, scale), color, width);
    }
    if (stroke.points.size() == 1) draw_list->AddCircleFilled(to_imvec2(stroke.points[0], origin, scale), width * 0.5F, color);
}

void draw_overlay(ImDrawList* draw_list, const AnnotationOverlay& overlay, const ImVec2& origin, float scale) {
    if (overlay.kind == AnnotationOverlayKind::Comment) {
        draw_comment(draw_list, overlay.comment, origin, scale);
    } else {
        draw_stroke(draw_list, overlay.stroke, origin, scale);
    }
}

float segment_distance(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = dx * dx + dy * dy;
    const float t = length > 0.0F ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.0F, 1.0F) : 0.0F;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Screen-space bounds of an overlay (for the selection outline).
std::pair<ImVec2, ImVec2> overlay_bounds(const AnnotationOverlay& overlay, const ImVec2& origin, float scale) {
    if (overlay.kind == AnnotationOverlayKind::Comment) return comment_rect(overlay.comment, origin, scale);
    ImVec2 min(FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX);
    for (const auto& point : overlay.stroke.points) {
        const auto at = to_imvec2(point, origin, scale);
        min = ImVec2(std::min(min.x, at.x), std::min(min.y, at.y));
        max = ImVec2(std::max(max.x, at.x), std::max(max.y, at.y));
    }
    const float pad = std::max(4.0F, overlay.stroke.width * scale * 0.5F + 3.0F);
    return {ImVec2(min.x - pad, min.y - pad), ImVec2(max.x + pad, max.y + pad)};
}

// Topmost overlay under the mouse, or -1. Shapes are picked near their
// outline (or inside for small ones), lines and pen strokes near the path.
int hit_test(const AnnotationDocument& document, ImVec2 mouse, const ImVec2& origin, float scale) {
    const auto& overlays = document.overlays();
    for (int index = static_cast<int>(overlays.size()) - 1; index >= 0; --index) {
        const auto& overlay = overlays[static_cast<std::size_t>(index)];
        const float tolerance = 6.0F + (overlay.kind == AnnotationOverlayKind::Stroke ? overlay.stroke.width * scale * 0.5F : 0.0F);
        if (overlay.kind == AnnotationOverlayKind::Comment) {
            const auto [min, max] = comment_rect(overlay.comment, origin, scale);
            if (mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y) return index;
            continue;
        }
        const auto& stroke = overlay.stroke;
        if (stroke.points.empty()) continue;
        if ((stroke.tool == AnnotationTool::Rectangle || stroke.tool == AnnotationTool::Circle) && stroke.points.size() >= 2) {
            const auto a = to_imvec2(stroke.points[0], origin, scale);
            const auto b = to_imvec2(stroke.points[1], origin, scale);
            const ImVec2 min(std::min(a.x, b.x), std::min(a.y, b.y)), max(std::max(a.x, b.x), std::max(a.y, b.y));
            const bool inside = mouse.x >= min.x - tolerance && mouse.x <= max.x + tolerance &&
                                mouse.y >= min.y - tolerance && mouse.y <= max.y + tolerance;
            if (!inside) continue;
            if (stroke.tool == AnnotationTool::Rectangle) {
                const bool near_edge = std::min({std::abs(mouse.x - min.x), std::abs(mouse.x - max.x),
                                                 std::abs(mouse.y - min.y), std::abs(mouse.y - max.y)}) <= tolerance;
                if (near_edge || (max.x - min.x) < 40.0F || (max.y - min.y) < 40.0F) return index;
            } else {
                const float rx = std::max(1.0F, (max.x - min.x) * 0.5F), ry = std::max(1.0F, (max.y - min.y) * 0.5F);
                const float nx = (mouse.x - (min.x + rx)) / rx, ny = (mouse.y - (min.y + ry)) / ry;
                const float radial = std::sqrt(nx * nx + ny * ny);
                if (std::abs(radial - 1.0F) * std::min(rx, ry) <= tolerance || std::min(rx, ry) < 20.0F) return index;
            }
            continue;
        }
        if (stroke.points.size() == 1) {
            if (std::hypot(mouse.x - to_imvec2(stroke.points[0], origin, scale).x,
                           mouse.y - to_imvec2(stroke.points[0], origin, scale).y) <= tolerance) return index;
            continue;
        }
        for (std::size_t point = 1; point < stroke.points.size(); ++point) {
            if (segment_distance(mouse, to_imvec2(stroke.points[point - 1], origin, scale),
                                 to_imvec2(stroke.points[point], origin, scale)) <= tolerance) return index;
        }
    }
    return -1;
}

AnnotationTool stroke_tool(AnnotationPanelTool tool) {
    switch (tool) {
        case AnnotationPanelTool::Line: return AnnotationTool::Line;
        case AnnotationPanelTool::Arrow: return AnnotationTool::Arrow;
        case AnnotationPanelTool::Rectangle: return AnnotationTool::Rectangle;
        case AnnotationPanelTool::Circle: return AnnotationTool::Circle;
        default: return AnnotationTool::Pen;
    }
}

// Places the draft once and ends the edit session; the next session gets a
// new widget ID so the old input's buffer cannot be committed again.
void commit_text(ImageAnnotationPanelState& state) {
    if (!state.text_editing) return;
    if (!state.text_draft.empty()) {
        state.document.add_comment(state.text_draft, state.text_anchor, state.color, state.font_size);
    }
    state.text_editing = false;
    state.text_draft.clear();
    ++state.text_session;
}
#endif

}  // namespace

bool ImageAnnotationPanelState::start_export(const std::filesystem::path& output, Exporter exporter) {
    if (pending_export.has_value()) {
        status_text = "Annotation export is already running";
        return false;
    }
    status_text = "Saving annotated SVG...";
    const auto snapshot = document;
    const auto svg_output = svg_export_path(output);
    pending_export.emplace(std::async(std::launch::async,
        [snapshot, svg_output, exporter = std::move(exporter)] { return exporter(snapshot, svg_output); }));
    return true;
}

void ImageAnnotationPanelState::poll_export() {
    if (!pending_export || pending_export->wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) return;
    try {
        const auto result = pending_export->get();
        status_text = result.success ? "Saved annotated SVG: " + path_to_utf8_string(result.output_path)
                                     : "Annotation export unavailable: " + result.error;
        if (result.success) last_export_path = result.output_path;
    } catch (const std::exception& error) {
        status_text = std::string{"Annotation export unavailable: "} + error.what();
    }
    pending_export.reset();
}

FastActionPanelModel build_image_annotation_panel_model(const ImageAnnotationPanelState& state) {
    std::ostringstream detail;
    detail << "Image: " << path_to_utf8_string(state.document.original_image()) << '\n'
           << "Overlays: " << state.document.overlays().size() << '\n'
           << "Export: SVG only; PNG/JPG export is not available in this build.\n";
    if (!state.status_text.empty()) {
        detail << state.status_text << '\n';
    }
    FastActionPanelModel model;
    model.viewport = independent_panel_viewport("PasteIt Image Annotation##" + path_to_utf8_string(state.document.original_image()),
                                                {820.0F, 680.0F});
    model.toolbar = {
        {.id = "pen", .label = "Pen", .value = {}},
        {.id = "line", .label = "Line", .value = {}},
        {.id = "rectangle", .label = "Rectangle", .value = {}},
        {.id = "arrow", .label = "Arrow", .value = {}},
        {.id = "comment", .label = "Comment", .value = {}},
        {.id = "undo", .label = "Undo", .value = {}},
        {.id = "clear", .label = "Clear", .value = {}},
        {.id = "export_svg", .label = "Save annotated SVG", .value = {}},
        {.id = "copy_temp_path", .label = "Copy temporary image path", .value = path_to_utf8_string(state.document.original_image())},
    };
    model.status_text = state.status_text;
    model.primary_text = detail.str();
    model.annotation_section_expanded = true;
    return model;
}

#if defined(PASTEIT_HAS_DESKTOP_DEPS)
void draw_image_annotation_panel(ImageAnnotationPanelState& state,
                                 unsigned int texture_id,
                                 int image_width,
                                 int image_height) {
    auto model = build_image_annotation_panel_model(state);
    state.poll_export();
    if (state.output_path.empty()) state.output_path = path_to_utf8_string(default_export_path(state));
    if (begin_tool_window(model.viewport.title, &state.open,
                          ImVec2(model.viewport.initial_width, model.viewport.initial_height), &state.focus_pending)) {
        auto& io = ImGui::GetIO();
        // Tool shortcuts while this window is focused and nothing is being typed.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt) {
            for (const auto& info : kTools) {
                if (ImGui::IsKeyPressed(info.key, false)) state.active_tool = info.tool;
            }
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
            io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            (void)state.document.undo();
            state.selected = -1;
        }
        if (state.selected >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            (void)state.document.erase_overlay(static_cast<std::size_t>(state.selected));
            state.selected = -1;
        }
        if (state.active_tool != AnnotationPanelTool::Select) state.selected = -1;
        if (state.selected >= static_cast<int>(state.document.overlays().size())) state.selected = -1;

        const float row = ImGui::GetFrameHeightWithSpacing();
        const float bottom_height = row * 4.0F + ImGui::GetTextLineHeightWithSpacing() + 12.0F;
        ImGui::BeginChild("annotation-canvas", ImVec2(0.0F, -bottom_height), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        const auto available = ImGui::GetContentRegionAvail();
        const auto scale = std::max(0.05F, std::min(available.x / static_cast<float>(image_width),
                                                    available.y / static_cast<float>(image_height)));
        const auto size = ImVec2(static_cast<float>(image_width) * scale, static_cast<float>(image_height) * scale);
        const auto cursor = ImGui::GetCursorScreenPos();
        const auto origin = ImVec2(cursor.x + std::max(0.0F, (available.x - size.x) * 0.5F),
                                   cursor.y + std::max(0.0F, (available.y - size.y) * 0.5F));
        // An invisible button owns the mouse over the image, so press-and-drag
        // draws instead of moving the window.
        ImGui::SetCursorScreenPos(origin);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("annotation-surface", size);
        const bool activated = ImGui::IsItemActivated();
        const bool active = ImGui::IsItemActive();
        const bool released = ImGui::IsItemDeactivated();
        if (ImGui::IsItemHovered()) {
            const bool over_object = state.active_tool == AnnotationPanelTool::Select &&
                                     (state.moving || hit_test(state.document, io.MousePos, origin, scale) >= 0);
            ImGui::SetMouseCursor(state.active_tool == AnnotationPanelTool::Text ? ImGuiMouseCursor_TextInput
                                  : state.active_tool == AnnotationPanelTool::Select ? (over_object ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Arrow)
                                  : ImGuiMouseCursor_Hand);
        }
        auto* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddImage((ImTextureID)(intptr_t)texture_id, origin, ImVec2(origin.x + size.x, origin.y + size.y));
        for (const auto& overlay : state.document.overlays()) draw_overlay(draw_list, overlay, origin, scale);

        const auto mouse = from_mouse(io.MousePos, origin, scale, image_width, image_height);
        if (activated) {
            if (state.active_tool == AnnotationPanelTool::Select) {
                commit_text(state);
                state.selected = hit_test(state.document, io.MousePos, origin, scale);
                state.moving = state.selected >= 0;
                state.move_last = {(io.MousePos.x - origin.x) / scale, (io.MousePos.y - origin.y) / scale};
            } else if (state.active_tool == AnnotationPanelTool::Text) {
                commit_text(state);
                state.text_editing = true;
                state.text_focus_pending = true;
                state.text_anchor = mouse;
            } else {
                commit_text(state);
                state.drawing = true;
                state.drag_start = mouse;
                state.pending_pen = {mouse};
            }
        }
        if (state.moving && active) {
            // Unclamped delta so objects can be dragged to the image edge.
            const AnnotationPoint now{(io.MousePos.x - origin.x) / scale, (io.MousePos.y - origin.y) / scale};
            (void)state.document.move_overlay(static_cast<std::size_t>(state.selected), now.x - state.move_last.x, now.y - state.move_last.y);
            state.move_last = now;
        }
        if (state.moving && released) state.moving = false;
        if (state.selected >= 0) {
            const auto [min, max] = overlay_bounds(state.document.overlays()[static_cast<std::size_t>(state.selected)], origin, scale);
            draw_list->AddRect(ImVec2(min.x - 2, min.y - 2), ImVec2(max.x + 2, max.y + 2), ImGui::GetColorU32(palette().accent), 3.0F, 0, 1.5F);
            for (const auto corner : {min, ImVec2(max.x, min.y), max, ImVec2(min.x, max.y)}) {
                draw_list->AddRectFilled(ImVec2(corner.x - 4, corner.y - 4), ImVec2(corner.x + 4, corner.y + 4), ImGui::GetColorU32(palette().accent));
            }
        }
        if (state.drawing && active) {
            const auto& last = state.pending_pen.back();
            if (state.active_tool != AnnotationPanelTool::Pen) {
                state.pending_pen = {state.drag_start, mouse};
            } else if (std::hypot(mouse.x - last.x, mouse.y - last.y) * scale >= 1.5F) {
                state.pending_pen.push_back(mouse);
            }
            AnnotationStroke preview{.tool = stroke_tool(state.active_tool), .points = state.pending_pen,
                                     .width = state.stroke_width, .color = state.color};
            draw_stroke(draw_list, preview, origin, scale);
        }
        if (state.drawing && released) {
            state.drawing = false;
            const bool tiny = std::hypot(mouse.x - state.drag_start.x, mouse.y - state.drag_start.y) * scale < 3.0F;
            if (state.active_tool == AnnotationPanelTool::Pen) {
                state.pending_pen.push_back(mouse);
                state.document.add_stroke({.tool = AnnotationTool::Pen, .points = state.pending_pen,
                                           .width = state.stroke_width, .color = state.color});
            } else if (!tiny) {
                state.document.add_stroke({.tool = stroke_tool(state.active_tool), .points = {state.drag_start, mouse},
                                           .width = state.stroke_width, .color = state.color});
            }
            state.pending_pen.clear();
        }
        // Inline text editor at the clicked point: Enter places it, Ctrl+Enter
        // adds a line, Escape cancels.
        if (state.text_editing) {
            const auto at = to_imvec2(state.text_anchor, origin, scale);
            ImGui::SetCursorScreenPos(at);
            if (state.text_focus_pending) {
                ImGui::SetKeyboardFocusHere();
                state.text_focus_pending = false;
            }
            const float field_width = std::clamp(origin.x + size.x - at.x, 160.0F, 320.0F);
            ImGui::PushID(state.text_session);
            const bool entered = input_text_string("##annotation-text", state.text_draft, true,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine,
                ImGui::GetTextLineHeightWithSpacing() * 2.2F, field_width);
            const bool left = ImGui::IsItemDeactivated();
            ImGui::PopID();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !entered) {
                state.text_draft.clear();
                commit_text(state);  // nothing to place; just ends the session
            } else if (entered || left) {
                commit_text(state);
            }
        }
        ImGui::EndChild();

        // Row 1: tools, colour, size.
        for (const auto& info : kTools) {
            if (info.tool != kTools[0].tool) ImGui::SameLine(0.0F, 4.0F);
            const bool selected = state.active_tool == info.tool;
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, palette().accent_soft);
            if (ImGui::Button(info.label)) state.active_tool = info.tool;
            if (selected) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  (%s)", info.label, info.hint);
        }
        ImGui::SameLine(0.0F, 14.0F);
        for (std::size_t index = 0; index < std::size(kSwatches); ++index) {
            const auto& swatch = kSwatches[index];
            if (index) ImGui::SameLine(0.0F, 3.0F);
            const bool selected = swatch.r == state.color.r && swatch.g == state.color.g && swatch.b == state.color.b;
            ImGui::PushID(static_cast<int>(index));
            const float side = ImGui::GetFrameHeight();
            if (ImGui::InvisibleButton("swatch", ImVec2(side, side))) {
                state.color = swatch;
                if (state.selected >= 0) (void)state.document.set_overlay_color(static_cast<std::size_t>(state.selected), swatch);
            }
            const auto min = ImGui::GetItemRectMin();
            const auto max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x + 3, min.y + 3), ImVec2(max.x - 3, max.y - 3), color_u32(swatch), 4.0F);
            ImGui::GetWindowDrawList()->AddRect(min, max, selected ? ImGui::GetColorU32(palette().accent) : IM_COL32(128, 128, 128, 90), 5.0F, 0,
                                                selected ? 2.0F : 1.0F);
            ImGui::PopID();
        }

        // Row 2: size, edit.
        ImGui::SetNextItemWidth(130.0F);
        if (state.active_tool == AnnotationPanelTool::Text) {
            ImGui::SliderFloat("##annotation-font", &state.font_size, 10.0F, 72.0F, "text %.0f px");
        } else {
            ImGui::SliderFloat("##annotation-width", &state.stroke_width, 1.0F, 16.0F, "width %.0f");
        }
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kUndo, tr(UiTextKey::Undo)).c_str())) (void)state.document.undo();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ctrl+Z");
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kTrash, tr(UiTextKey::Clear)).c_str())) state.document.clear();
        ImGui::SameLine();
        if (state.active_tool == AnnotationPanelTool::Select) {
            ImGui::TextColored(palette().text_muted, "%s", state.selected >= 0
                ? "Drag to move \xC2\xB7 Delete removes \xC2\xB7 a colour recolours"
                : "Click an object to select it, then drag to move");
        } else {
            ImGui::TextColored(palette().text_muted, "%zu marks \xC2\xB7 S F W A R C T switch tools", state.document.overlays().size());
        }

        // Row 3: output file and Save; row 4: what to do with the saved file.
        const auto save_label = with_icon(icon::kSave, tr(UiTextKey::SaveAnnotatedSvg));
        const auto& style = ImGui::GetStyle();
        const float save_width = ImGui::CalcTextSize(save_label.c_str(), nullptr, true).x + style.FramePadding.x * 2.0F;
        input_text_string("##annotation-output", state.output_path, false, 0, 0.0F,
                          std::max(140.0F, ImGui::GetContentRegionAvail().x - save_width - style.ItemSpacing.x));
        ImGui::SameLine();
        ImGui::BeginDisabled(state.export_running() || state.document.original_image().empty());
        if (ImGui::Button(save_label.c_str())) {
            commit_text(state);
            state.start_export(path_from_utf8_string(state.output_path));
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(state.last_export_path.empty());
        if (ImGui::Button(with_icon(icon::kOpen, "Open").c_str()) && state.open_path) (void)state.open_path(state.last_export_path);
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kFolderOpen, "Folder").c_str()) && state.open_path) {
            (void)state.open_path(state.last_export_path.parent_path());
        }
        ImGui::SameLine();
        if (ImGui::Button(with_icon(icon::kCopy, "Copy path").c_str()) && state.copy_text) {
            state.copy_text(path_to_utf8_string(state.last_export_path));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (!state.status_text.empty()) copyable_text(state.status_text, true);
    }
    ImGui::End();
}
#endif

}  // namespace pasteit
